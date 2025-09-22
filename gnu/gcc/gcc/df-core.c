/* Allocation for dataflow support routines.
   Copyright (C) 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.
   Originally contributed by Michael P. Hayes 
             (m.hayes@elec.canterbury.ac.nz, mhayes@redhat.com)
   Major rewrite contributed by Danny Berlin (dberlin@dberlin.org)
             and Kenneth Zadeck (zadeck@naturalbridge.com).

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
02110-1301, USA.  
*/

/*
OVERVIEW:

The files in this collection (df*.c,df.h) provide a general framework
for solving dataflow problems.  The global dataflow is performed using
a good implementation of iterative dataflow analysis.

The file df-problems.c provides problem instance for the most common
dataflow problems: reaching defs, upward exposed uses, live variables,
uninitialized variables, def-use chains, and use-def chains.  However,
the interface allows other dataflow problems to be defined as well.


USAGE:

Here is an example of using the dataflow routines.

      struct df *df;

      df = df_init (init_flags);
      
      df_add_problem (df, problem, flags);

      df_set_blocks (df, blocks);

      df_rescan_blocks (df, blocks);

      df_analyze (df);

      df_dump (df, stderr);

      df_finish (df);



DF_INIT simply creates a poor man's object (df) that needs to be
passed to all the dataflow routines.  df_finish destroys this object
and frees up any allocated memory.

There are three flags that can be passed to df_init, each of these
flags controls the scanning of the rtl:

DF_HARD_REGS means that the scanning is to build information about
both pseudo registers and hardware registers.  Without this
information, the problems will be solved only on pseudo registers.
DF_EQUIV_NOTES marks the uses present in EQUIV/EQUAL notes.
DF_SUBREGS return subregs rather than the inner reg.


DF_ADD_PROBLEM adds a problem, defined by an instance to struct
df_problem, to the set of problems solved in this instance of df.  All
calls to add a problem for a given instance of df must occur before
the first call to DF_RESCAN_BLOCKS, DF_SET_BLOCKS or DF_ANALYZE.

For all of the problems defined in df-problems.c, there are
convenience functions named DF_*_ADD_PROBLEM.


Problems can be dependent on other problems.  For instance, solving
def-use or use-def chains is dependent on solving reaching
definitions. As long as these dependencies are listed in the problem
definition, the order of adding the problems is not material.
Otherwise, the problems will be solved in the order of calls to
df_add_problem.  Note that it is not necessary to have a problem.  In
that case, df will just be used to do the scanning.



DF_SET_BLOCKS is an optional call used to define a region of the
function on which the analysis will be performed.  The normal case is
to analyze the entire function and no call to df_set_blocks is made.

When a subset is given, the analysis behaves as if the function only
contains those blocks and any edges that occur directly between the
blocks in the set.  Care should be taken to call df_set_blocks right
before the call to analyze in order to eliminate the possibility that
optimizations that reorder blocks invalidate the bitvector.



DF_RESCAN_BLOCKS is an optional call that causes the scanner to be
 (re)run over the set of blocks passed in.  If blocks is NULL, the entire
function (or all of the blocks defined in df_set_blocks) is rescanned.
If blocks contains blocks that were not defined in the call to
df_set_blocks, these blocks are added to the set of blocks.


DF_ANALYZE causes all of the defined problems to be (re)solved.  It
does not cause blocks to be (re)scanned at the rtl level unless no
prior call is made to df_rescan_blocks.  When DF_ANALYZE is completes,
the IN and OUT sets for each basic block contain the computer
information.  The DF_*_BB_INFO macros can be used to access these
bitvectors.


DF_DUMP can then be called to dump the information produce to some
file.



DF_FINISH causes all of the datastructures to be cleaned up and freed.
The df_instance is also freed and its pointer should be NULLed.




Scanning produces a `struct df_ref' data structure (ref) is allocated
for every register reference (def or use) and this records the insn
and bb the ref is found within.  The refs are linked together in
chains of uses and defs for each insn and for each register.  Each ref
also has a chain field that links all the use refs for a def or all
the def refs for a use.  This is used to create use-def or def-use
chains.

Different optimizations have different needs.  Ultimately, only
register allocation and schedulers should be using the bitmaps
produced for the live register and uninitialized register problems.
The rest of the backend should be upgraded to using and maintaining
the linked information such as def use or use def chains.



PHILOSOPHY:

While incremental bitmaps are not worthwhile to maintain, incremental
chains may be perfectly reasonable.  The fastest way to build chains
from scratch or after significant modifications is to build reaching
definitions (RD) and build the chains from this.

However, general algorithms for maintaining use-def or def-use chains
are not practical.  The amount of work to recompute the chain any
chain after an arbitrary change is large.  However, with a modest
amount of work it is generally possible to have the application that
uses the chains keep them up to date.  The high level knowledge of
what is really happening is essential to crafting efficient
incremental algorithms.

As for the bit vector problems, there is no interface to give a set of
blocks over with to resolve the iteration.  In general, restarting a
dataflow iteration is difficult and expensive.  Again, the best way to
keep the dataflow information up to data (if this is really what is
needed) it to formulate a problem specific solution.

There are fine grained calls for creating and deleting references from
instructions in df-scan.c.  However, these are not currently connected
to the engine that resolves the dataflow equations.


DATA STRUCTURES:

The basic object is a DF_REF (reference) and this may either be a 
DEF (definition) or a USE of a register.

These are linked into a variety of lists; namely reg-def, reg-use,
insn-def, insn-use, def-use, and use-def lists.  For example, the
reg-def lists contain all the locations that define a given register
while the insn-use lists contain all the locations that use a
register.

Note that the reg-def and reg-use chains are generally short for
pseudos and long for the hard registers.

ACCESSING REFS:

There are 4 ways to obtain access to refs:

1) References are divided into two categories, REAL and ARTIFICIAL.

   REAL refs are associated with instructions.  They are linked into
   either in the insn's defs list (accessed by the DF_INSN_DEFS or
   DF_INSN_UID_DEFS macros) or the insn's uses list (accessed by the
   DF_INSN_USES or DF_INSN_UID_USES macros).  These macros produce a
   ref (or NULL), the rest of the list can be obtained by traversal of
   the NEXT_REF field (accessed by the DF_REF_NEXT_REF macro.)  There
   is no significance to the ordering of the uses or refs in an
   instruction.

   ARTIFICIAL refs are associated with basic blocks.  The heads of
   these lists can be accessed by calling get_artificial_defs or
   get_artificial_uses for the particular basic block.  Artificial
   defs and uses are only there if DF_HARD_REGS was specified when the
   df instance was created.
 
   Artificial defs and uses occur both at the beginning and ends of blocks.

     For blocks that area at the destination of eh edges, the
     artificial uses and defs occur at the beginning.  The defs relate
     to the registers specified in EH_RETURN_DATA_REGNO and the uses
     relate to the registers specified in ED_USES.  Logically these
     defs and uses should really occur along the eh edge, but there is
     no convenient way to do this.  Artificial edges that occur at the
     beginning of the block have the DF_REF_AT_TOP flag set.

     Artificial uses occur at the end of all blocks.  These arise from
     the hard registers that are always live, such as the stack
     register and are put there to keep the code from forgetting about
     them.

     Artificial defs occur at the end of the entry block.  These arise
     from registers that are live at entry to the function.

2) All of the uses and defs associated with each pseudo or hard
   register are linked in a bidirectional chain.  These are called
   reg-use or reg_def chains.

   The first use (or def) for a register can be obtained using the
   DF_REG_USE_GET macro (or DF_REG_DEF_GET macro).  Subsequent uses
   for the same regno can be obtained by following the next_reg field
   of the ref.

   In previous versions of this code, these chains were ordered.  It
   has not been practical to continue this practice.

3) If def-use or use-def chains are built, these can be traversed to
   get to other refs.

4) An array of all of the uses (and an array of all of the defs) can
   be built.  These arrays are indexed by the value in the id
   structure.  These arrays are only lazily kept up to date, and that
   process can be expensive.  To have these arrays built, call
   df_reorganize_refs.   Note that the values in the id field of a ref
   may change across calls to df_analyze or df_reorganize refs.

   If the only use of this array is to find all of the refs, it is
   better to traverse all of the registers and then traverse all of
   reg-use or reg-def chains.



NOTES:
 
Embedded addressing side-effects, such as POST_INC or PRE_INC, generate
both a use and a def.  These are both marked read/write to show that they
are dependent. For example, (set (reg 40) (mem (post_inc (reg 42))))
will generate a use of reg 42 followed by a def of reg 42 (both marked
read/write).  Similarly, (set (reg 40) (mem (pre_dec (reg 41))))
generates a use of reg 41 then a def of reg 41 (both marked read/write),
even though reg 41 is decremented before it is used for the memory
address in this second example.

A set to a REG inside a ZERO_EXTRACT, or a set to a non-paradoxical SUBREG
for which the number of word_mode units covered by the outer mode is
smaller than that covered by the inner mode, invokes a read-modify-write.
operation.  We generate both a use and a def and again mark them
read/write.

Paradoxical subreg writes do not leave a trace of the old content, so they
are write-only operations.  
*/


#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "tm_p.h"
#include "insn-config.h"
#include "recog.h"
#include "function.h"
#include "regs.h"
#include "output.h"
#include "alloc-pool.h"
#include "flags.h"
#include "hard-reg-set.h"
#include "basic-block.h"
#include "sbitmap.h"
#include "bitmap.h"
#include "timevar.h"
#include "df.h"
#include "tree-pass.h"

static struct df *ddf = NULL;
struct df *shared_df = NULL;

static void *df_get_bb_info (struct dataflow *, unsigned int);
static void df_set_bb_info (struct dataflow *, unsigned int, void *);
/*----------------------------------------------------------------------------
  Functions to create, destroy and manipulate an instance of df.
----------------------------------------------------------------------------*/


/* Initialize dataflow analysis and allocate and initialize dataflow
   memory.  */

struct df *
df_init (int flags)
{
  struct df *df = XCNEW (struct df);

  /* This is executed once per compilation to initialize platform
     specific data structures. */
  df_hard_reg_init ();
  
  /* All df instance must define the scanning problem.  */
  df_scan_add_problem (df, flags);
  ddf = df;
  return df;
}

/* Add PROBLEM to the DF instance.  */

struct dataflow *
df_add_problem (struct df *df, struct df_problem *problem, int flags)
{
  struct dataflow *dflow;

  /* First try to add the dependent problem. */
  if (problem->dependent_problem_fun)
    (problem->dependent_problem_fun) (df, 0);

  /* Check to see if this problem has already been defined.  If it
     has, just return that instance, if not, add it to the end of the
     vector.  */
  dflow = df->problems_by_index[problem->id];
  if (dflow)
    return dflow;

  /* Make a new one and add it to the end.  */
  dflow = XCNEW (struct dataflow);
  dflow->flags = flags;
  dflow->df = df;
  dflow->problem = problem;
  df->problems_in_order[df->num_problems_defined++] = dflow;
  df->problems_by_index[dflow->problem->id] = dflow;

  return dflow;
}


/* Set the MASK flags in the DFLOW problem.  The old flags are
   returned.  If a flag is not allowed to be changed this will fail if
   checking is enabled.  */
int 
df_set_flags (struct dataflow *dflow, int mask)
{
  int old_flags = dflow->flags;

  gcc_assert (!(mask & (~dflow->problem->changeable_flags)));

  dflow->flags |= mask;

  return old_flags;
}

/* Clear the MASK flags in the DFLOW problem.  The old flags are
   returned.  If a flag is not allowed to be changed this will fail if
   checking is enabled.  */
int 
df_clear_flags (struct dataflow *dflow, int mask)
{
  int old_flags = dflow->flags;

  gcc_assert (!(mask & (~dflow->problem->changeable_flags)));

  dflow->flags &= !mask;

  return old_flags;
}

/* Set the blocks that are to be considered for analysis.  If this is
   not called or is called with null, the entire function in
   analyzed.  */

void 
df_set_blocks (struct df *df, bitmap blocks)
{
  if (blocks)
    {
      if (df->blocks_to_analyze)
	{
	  int p;
	  bitmap diff = BITMAP_ALLOC (NULL);
	  bitmap_and_compl (diff, df->blocks_to_analyze, blocks);
	  for (p = df->num_problems_defined - 1; p >= 0 ;p--)
	    {
	      struct dataflow *dflow = df->problems_in_order[p];
	      if (dflow->problem->reset_fun)
		dflow->problem->reset_fun (dflow, df->blocks_to_analyze);
	      else if (dflow->problem->free_bb_fun)
		{
		  bitmap_iterator bi;
		  unsigned int bb_index;
		  
		  EXECUTE_IF_SET_IN_BITMAP (diff, 0, bb_index, bi)
		    {
		      basic_block bb = BASIC_BLOCK (bb_index);
		      if (bb)
			{
			  dflow->problem->free_bb_fun
			    (dflow, bb, df_get_bb_info (dflow, bb_index));
			  df_set_bb_info (dflow, bb_index, NULL); 
			}
		    }
		}
	    }

	  BITMAP_FREE (diff);
	}
      else
	{
	  /* If we have not actually run scanning before, do not try
	     to clear anything.  */
	  struct dataflow *scan_dflow = df->problems_by_index [DF_SCAN];
	  if (scan_dflow->problem_data)
	    {
	      bitmap blocks_to_reset = NULL;
	      int p;
	      for (p = df->num_problems_defined - 1; p >= 0 ;p--)
		{
		  struct dataflow *dflow = df->problems_in_order[p];
		  if (dflow->problem->reset_fun)
		    {
		      if (!blocks_to_reset)
			{
			  basic_block bb;
			  blocks_to_reset = BITMAP_ALLOC (NULL);
			  FOR_ALL_BB(bb)
			    {
			      bitmap_set_bit (blocks_to_reset, bb->index); 
			    }
			}
		      dflow->problem->reset_fun (dflow, blocks_to_reset);
		    }
		}
	      if (blocks_to_reset)
		BITMAP_FREE (blocks_to_reset);
	    }
	  df->blocks_to_analyze = BITMAP_ALLOC (NULL);
	}
      bitmap_copy (df->blocks_to_analyze, blocks);
    }
  else
    {
      if (df->blocks_to_analyze)
	{
	  BITMAP_FREE (df->blocks_to_analyze);
	  df->blocks_to_analyze = NULL;
	}
    }
}


/* Free all of the per basic block dataflow from all of the problems.
   This is typically called before a basic block is deleted and the
   problem will be reanalyzed.  */

void
df_delete_basic_block (struct df *df, int bb_index)
{
  basic_block bb = BASIC_BLOCK (bb_index);
  int i;
  
  for (i = 0; i < df->num_problems_defined; i++)
    {
      struct dataflow *dflow = df->problems_in_order[i];
      if (dflow->problem->free_bb_fun)
	dflow->problem->free_bb_fun 
	  (dflow, bb, df_get_bb_info (dflow, bb_index)); 
    }
}


/* Free all the dataflow info and the DF structure.  This should be
   called from the df_finish macro which also NULLs the parm.  */

void
df_finish1 (struct df *df)
{
  int i;

  for (i = 0; i < df->num_problems_defined; i++)
    df->problems_in_order[i]->problem->free_fun (df->problems_in_order[i]); 

  free (df);
}


/*----------------------------------------------------------------------------
   The general data flow analysis engine.
----------------------------------------------------------------------------*/


/* Hybrid search algorithm from "Implementation Techniques for
   Efficient Data-Flow Analysis of Large Programs".  */

static void
df_hybrid_search_forward (basic_block bb, 
			  struct dataflow *dataflow,
			  bool single_pass)
{
  int result_changed;
  int i = bb->index;
  edge e;
  edge_iterator ei;

  SET_BIT (dataflow->visited, bb->index);
  gcc_assert (TEST_BIT (dataflow->pending, bb->index));
  RESET_BIT (dataflow->pending, i);

  /*  Calculate <conf_op> of predecessor_outs.  */
  if (EDGE_COUNT (bb->preds) > 0)
    FOR_EACH_EDGE (e, ei, bb->preds)
      {
	if (!TEST_BIT (dataflow->considered, e->src->index))
	  continue;
	
	dataflow->problem->con_fun_n (dataflow, e);
      }
  else if (dataflow->problem->con_fun_0)
    dataflow->problem->con_fun_0 (dataflow, bb);
  
  result_changed = dataflow->problem->trans_fun (dataflow, i);
  
  if (!result_changed || single_pass)
    return;
  
  FOR_EACH_EDGE (e, ei, bb->succs)
    {
      if (e->dest->index == i)
	continue;
      if (!TEST_BIT (dataflow->considered, e->dest->index))
	continue;
      SET_BIT (dataflow->pending, e->dest->index);
    }
  
  FOR_EACH_EDGE (e, ei, bb->succs)
    {
      if (e->dest->index == i)
	continue;
      
      if (!TEST_BIT (dataflow->considered, e->dest->index))
	continue;
      if (!TEST_BIT (dataflow->visited, e->dest->index))
	df_hybrid_search_forward (e->dest, dataflow, single_pass);
    }
}

static void
df_hybrid_search_backward (basic_block bb,
			   struct dataflow *dataflow,
			   bool single_pass)
{
  int result_changed;
  int i = bb->index;
  edge e;
  edge_iterator ei;
  
  SET_BIT (dataflow->visited, bb->index);
  gcc_assert (TEST_BIT (dataflow->pending, bb->index));
  RESET_BIT (dataflow->pending, i);

  /*  Calculate <conf_op> of predecessor_outs.  */
  if (EDGE_COUNT (bb->succs) > 0)
    FOR_EACH_EDGE (e, ei, bb->succs)					
      {								
	if (!TEST_BIT (dataflow->considered, e->dest->index))		
	  continue;							
	
	dataflow->problem->con_fun_n (dataflow, e);
      }								
  else if (dataflow->problem->con_fun_0)
    dataflow->problem->con_fun_0 (dataflow, bb);

  result_changed = dataflow->problem->trans_fun (dataflow, i);
  
  if (!result_changed || single_pass)
    return;
  
  FOR_EACH_EDGE (e, ei, bb->preds)
    {								
      if (e->src->index == i)
	continue;
      
      if (!TEST_BIT (dataflow->considered, e->src->index))
	continue;

      SET_BIT (dataflow->pending, e->src->index);
    }								
  
  FOR_EACH_EDGE (e, ei, bb->preds)
    {
      if (e->src->index == i)
	continue;

      if (!TEST_BIT (dataflow->considered, e->src->index))
	continue;
      
      if (!TEST_BIT (dataflow->visited, e->src->index))
	df_hybrid_search_backward (e->src, dataflow, single_pass);
    }
}


/* This function will perform iterative bitvector dataflow described
   by DATAFLOW, producing the in and out sets.  Only the part of the
   cfg induced by blocks in DATAFLOW->order is taken into account.

   SINGLE_PASS is true if you just want to make one pass over the
   blocks.  */

void
df_iterative_dataflow (struct dataflow *dataflow,
		       bitmap blocks_to_consider, bitmap blocks_to_init, 
		       int *blocks_in_postorder, int n_blocks, 
		       bool single_pass)
{
  unsigned int idx;
  int i;
  sbitmap visited = sbitmap_alloc (last_basic_block);
  sbitmap pending = sbitmap_alloc (last_basic_block);
  sbitmap considered = sbitmap_alloc (last_basic_block);
  bitmap_iterator bi;

  dataflow->visited = visited;
  dataflow->pending = pending;
  dataflow->considered = considered;

  sbitmap_zero (visited);
  sbitmap_zero (pending);
  sbitmap_zero (considered);

  gcc_assert (dataflow->problem->dir);

  EXECUTE_IF_SET_IN_BITMAP (blocks_to_consider, 0, idx, bi)
    {
      SET_BIT (considered, idx);
    }

  for (i = 0; i < n_blocks; i++)
    {
      idx = blocks_in_postorder[i];
      SET_BIT (pending, idx);
    };

  dataflow->problem->init_fun (dataflow, blocks_to_init);

  while (1)
    {

      /* For forward problems, you want to pass in reverse postorder
         and for backward problems you want postorder.  This has been
         shown to be as good as you can do by several people, the
         first being Mathew Hecht in his phd dissertation.

	 The nodes are passed into this function in postorder.  */

      if (dataflow->problem->dir == DF_FORWARD)
	{
	  for (i = n_blocks - 1 ; i >= 0 ; i--)
	    {
	      idx = blocks_in_postorder[i];
	      
	      if (TEST_BIT (pending, idx) && !TEST_BIT (visited, idx))
		df_hybrid_search_forward (BASIC_BLOCK (idx), dataflow, single_pass);
	    }
	}
      else
	{
	  for (i = 0; i < n_blocks; i++)
	    {
	      idx = blocks_in_postorder[i];
	      
	      if (TEST_BIT (pending, idx) && !TEST_BIT (visited, idx))
		df_hybrid_search_backward (BASIC_BLOCK (idx), dataflow, single_pass);
	    }
	}

      if (sbitmap_first_set_bit (pending) == -1)
	break;

      sbitmap_zero (visited);
    }

  sbitmap_free (pending);
  sbitmap_free (visited);
  sbitmap_free (considered);
}


/* Remove the entries not in BLOCKS from the LIST of length LEN, preserving
   the order of the remaining entries.  Returns the length of the resulting
   list.  */

static unsigned
df_prune_to_subcfg (int list[], unsigned len, bitmap blocks)
{
  unsigned act, last;

  for (act = 0, last = 0; act < len; act++)
    if (bitmap_bit_p (blocks, list[act]))
      list[last++] = list[act];

  return last;
}


/* Execute dataflow analysis on a single dataflow problem. 

   There are three sets of blocks passed in: 

   BLOCKS_TO_CONSIDER are the blocks whose solution can either be
   examined or will be computed.  For calls from DF_ANALYZE, this is
   the set of blocks that has been passed to DF_SET_BLOCKS.  For calls
   from DF_ANALYZE_SIMPLE_CHANGE_SOME_BLOCKS, this is the set of
   blocks in the fringe (the set of blocks passed in plus the set of
   immed preds and succs of those blocks).

   BLOCKS_TO_INIT are the blocks whose solution will be changed by
   this iteration.  For calls from DF_ANALYZE, this is the set of
   blocks that has been passed to DF_SET_BLOCKS.  For calls from
   DF_ANALYZE_SIMPLE_CHANGE_SOME_BLOCKS, this is the set of blocks
   passed in.

   BLOCKS_TO_SCAN are the set of blocks that need to be rescanned.
   For calls from DF_ANALYZE, this is the accumulated set of blocks
   that has been passed to DF_RESCAN_BLOCKS since the last call to
   DF_ANALYZE.  For calls from DF_ANALYZE_SIMPLE_CHANGE_SOME_BLOCKS,
   this is the set of blocks passed in.
 
                   blocks_to_consider    blocks_to_init    blocks_to_scan
   full redo       all                   all               all
   partial redo    all                   all               sub
   small fixup     fringe                sub               sub
*/

void
df_analyze_problem (struct dataflow *dflow, 
		    bitmap blocks_to_consider, 
		    bitmap blocks_to_init,
		    bitmap blocks_to_scan,
		    int *postorder, int n_blocks, bool single_pass)
{
  /* (Re)Allocate the datastructures necessary to solve the problem.  */ 
  if (dflow->problem->alloc_fun)
    dflow->problem->alloc_fun (dflow, blocks_to_scan, blocks_to_init);

  /* Set up the problem and compute the local information.  This
     function is passed both the blocks_to_consider and the
     blocks_to_scan because the RD and RU problems require the entire
     function to be rescanned if they are going to be updated.  */
  if (dflow->problem->local_compute_fun)
    dflow->problem->local_compute_fun (dflow, blocks_to_consider, blocks_to_scan);

  /* Solve the equations.  */
  if (dflow->problem->dataflow_fun)
    dflow->problem->dataflow_fun (dflow, blocks_to_consider, blocks_to_init,
				  postorder, n_blocks, single_pass);

  /* Massage the solution.  */
  if (dflow->problem->finalize_fun)
    dflow->problem->finalize_fun (dflow, blocks_to_consider);
}


/* Analyze dataflow info for the basic blocks specified by the bitmap
   BLOCKS, or for the whole CFG if BLOCKS is zero.  */

void
df_analyze (struct df *df)
{
  int *postorder = XNEWVEC (int, last_basic_block);
  bitmap current_all_blocks = BITMAP_ALLOC (NULL);
  int n_blocks;
  int i;
  bool everything;

  n_blocks = post_order_compute (postorder, true);

  if (n_blocks != n_basic_blocks)
    delete_unreachable_blocks ();

  for (i = 0; i < n_blocks; i++)
    bitmap_set_bit (current_all_blocks, postorder[i]);

  /* No one called df_rescan_blocks, so do it.  */
  if (!df->blocks_to_scan)
    df_rescan_blocks (df, NULL);

  /* Make sure that we have pruned any unreachable blocks from these
     sets.  */
  bitmap_and_into (df->blocks_to_scan, current_all_blocks);

  if (df->blocks_to_analyze)
    {
      everything = false;
      bitmap_and_into (df->blocks_to_analyze, current_all_blocks);
      n_blocks = df_prune_to_subcfg (postorder, n_blocks, df->blocks_to_analyze);
      BITMAP_FREE (current_all_blocks);
    }
  else
    {
      everything = true;
      df->blocks_to_analyze = current_all_blocks;
      current_all_blocks = NULL;
    }

  /* Skip over the DF_SCAN problem. */
  for (i = 1; i < df->num_problems_defined; i++)
    df_analyze_problem (df->problems_in_order[i], 
			df->blocks_to_analyze, df->blocks_to_analyze, 
			df->blocks_to_scan,
			postorder, n_blocks, false);

  if (everything)
    {
      BITMAP_FREE (df->blocks_to_analyze);
      df->blocks_to_analyze = NULL;
    }

  BITMAP_FREE (df->blocks_to_scan);
  df->blocks_to_scan = NULL;
  free (postorder);
}



/*----------------------------------------------------------------------------
   Functions to support limited incremental change.
----------------------------------------------------------------------------*/


/* Get basic block info.  */

static void *
df_get_bb_info (struct dataflow *dflow, unsigned int index)
{
  return (struct df_scan_bb_info *) dflow->block_info[index];
}


/* Set basic block info.  */

static void
df_set_bb_info (struct dataflow *dflow, unsigned int index, 
		void *bb_info)
{
  dflow->block_info[index] = bb_info;
}


/* Called from the rtl_compact_blocks to reorganize the problems basic
   block info.  */

void 
df_compact_blocks (struct df *df)
{
  int i, p;
  basic_block bb;
  void **problem_temps;
  int size = last_basic_block *sizeof (void *);
  problem_temps = xmalloc (size);

  for (p = 0; p < df->num_problems_defined; p++)
    {
      struct dataflow *dflow = df->problems_in_order[p];
      if (dflow->problem->free_bb_fun)
	{
	  df_grow_bb_info (dflow);
	  memcpy (problem_temps, dflow->block_info, size);

	  /* Copy the bb info from the problem tmps to the proper
	     place in the block_info vector.  Null out the copied
	     item.  */
	  i = NUM_FIXED_BLOCKS;
	  FOR_EACH_BB (bb) 
	    {
	      df_set_bb_info (dflow, i, problem_temps[bb->index]);
	      problem_temps[bb->index] = NULL;
	      i++;
	    }
	  memset (dflow->block_info + i, 0, 
		  (last_basic_block - i) *sizeof (void *));

	  /* Free any block infos that were not copied (and NULLed).
	     These are from orphaned blocks.  */
	  for (i = NUM_FIXED_BLOCKS; i < last_basic_block; i++)
	    {
	      basic_block bb = BASIC_BLOCK (i); 
	      if (problem_temps[i] && bb)
		dflow->problem->free_bb_fun
		  (dflow, bb, problem_temps[i]);
	    }
	}
    }

  free (problem_temps);

  i = NUM_FIXED_BLOCKS;
  FOR_EACH_BB (bb) 
    {
      SET_BASIC_BLOCK (i, bb);
      bb->index = i;
      i++;
    }

  gcc_assert (i == n_basic_blocks);

  for (; i < last_basic_block; i++)
    SET_BASIC_BLOCK (i, NULL);
}


/* Shove NEW_BLOCK in at OLD_INDEX.  Called from if-cvt to hack a
   block.  There is no excuse for people to do this kind of thing.  */

void 
df_bb_replace (struct df *df, int old_index, basic_block new_block)
{
  int p;

  for (p = 0; p < df->num_problems_defined; p++)
    {
      struct dataflow *dflow = df->problems_in_order[p];
      if (dflow->block_info)
	{
	  void *temp;

	  df_grow_bb_info (dflow);

	  /* The old switcheroo.  */

	  temp = df_get_bb_info (dflow, old_index);
	  df_set_bb_info (dflow, old_index, 
			  df_get_bb_info (dflow, new_block->index));
	  df_set_bb_info (dflow, new_block->index, temp);
	}
    }

  SET_BASIC_BLOCK (old_index, new_block);
  new_block->index = old_index;
}

/*----------------------------------------------------------------------------
   PUBLIC INTERFACES TO QUERY INFORMATION.
----------------------------------------------------------------------------*/


/* Return last use of REGNO within BB.  */

struct df_ref *
df_bb_regno_last_use_find (struct df *df, basic_block bb, unsigned int regno)
{
  rtx insn;
  struct df_ref *use;
  unsigned int uid;

  FOR_BB_INSNS_REVERSE (bb, insn)
    {
      if (!INSN_P (insn))
	continue;

      uid = INSN_UID (insn);
      for (use = DF_INSN_UID_GET (df, uid)->uses; use; use = use->next_ref)
	if (DF_REF_REGNO (use) == regno)
	  return use;
    }
  return NULL;
}


/* Return first def of REGNO within BB.  */

struct df_ref *
df_bb_regno_first_def_find (struct df *df, basic_block bb, unsigned int regno)
{
  rtx insn;
  struct df_ref *def;
  unsigned int uid;

  FOR_BB_INSNS (bb, insn)
    {
      if (!INSN_P (insn))
	continue;

      uid = INSN_UID (insn);
      for (def = DF_INSN_UID_GET (df, uid)->defs; def; def = def->next_ref)
	if (DF_REF_REGNO (def) == regno)
	  return def;
    }
  return NULL;
}


/* Return last def of REGNO within BB.  */

struct df_ref *
df_bb_regno_last_def_find (struct df *df, basic_block bb, unsigned int regno)
{
  rtx insn;
  struct df_ref *def;
  unsigned int uid;

  FOR_BB_INSNS_REVERSE (bb, insn)
    {
      if (!INSN_P (insn))
	continue;

      uid = INSN_UID (insn);
      for (def = DF_INSN_UID_GET (df, uid)->defs; def; def = def->next_ref)
	if (DF_REF_REGNO (def) == regno)
	  return def;
    }

  return NULL;
}

/* Return true if INSN defines REGNO.  */

bool
df_insn_regno_def_p (struct df *df, rtx insn, unsigned int regno)
{
  unsigned int uid;
  struct df_ref *def;

  uid = INSN_UID (insn);
  for (def = DF_INSN_UID_GET (df, uid)->defs; def; def = def->next_ref)
    if (DF_REF_REGNO (def) == regno)
      return true;
  
  return false;
}


/* Finds the reference corresponding to the definition of REG in INSN.
   DF is the dataflow object.  */

struct df_ref *
df_find_def (struct df *df, rtx insn, rtx reg)
{
  unsigned int uid;
  struct df_ref *def;

  if (GET_CODE (reg) == SUBREG)
    reg = SUBREG_REG (reg);
  gcc_assert (REG_P (reg));

  uid = INSN_UID (insn);
  for (def = DF_INSN_UID_GET (df, uid)->defs; def; def = def->next_ref)
    if (rtx_equal_p (DF_REF_REAL_REG (def), reg))
      return def;

  return NULL;
}


/* Return true if REG is defined in INSN, zero otherwise.  */ 

bool
df_reg_defined (struct df *df, rtx insn, rtx reg)
{
  return df_find_def (df, insn, reg) != NULL;
}
  

/* Finds the reference corresponding to the use of REG in INSN.
   DF is the dataflow object.  */
  
struct df_ref *
df_find_use (struct df *df, rtx insn, rtx reg)
{
  unsigned int uid;
  struct df_ref *use;

  if (GET_CODE (reg) == SUBREG)
    reg = SUBREG_REG (reg);
  gcc_assert (REG_P (reg));

  uid = INSN_UID (insn);
  for (use = DF_INSN_UID_GET (df, uid)->uses; use; use = use->next_ref)
    if (rtx_equal_p (DF_REF_REAL_REG (use), reg))
      return use; 

  return NULL;
}


/* Return true if REG is referenced in INSN, zero otherwise.  */ 

bool
df_reg_used (struct df *df, rtx insn, rtx reg)
{
  return df_find_use (df, insn, reg) != NULL;
}
  

/*----------------------------------------------------------------------------
   Debugging and printing functions.
----------------------------------------------------------------------------*/

/* Dump dataflow info.  */
void
df_dump (struct df *df, FILE *file)
{
  int i;

  if (!df || !file)
    return;

  fprintf (file, "\n\n%s\n", current_function_name ());
  fprintf (file, "\nDataflow summary:\n");
  fprintf (file, "def_info->bitmap_size = %d, use_info->bitmap_size = %d\n",
	   df->def_info.bitmap_size, df->use_info.bitmap_size);

  for (i = 0; i < df->num_problems_defined; i++)
    df->problems_in_order[i]->problem->dump_fun (df->problems_in_order[i], file); 

  fprintf (file, "\n");
}


void
df_refs_chain_dump (struct df_ref *ref, bool follow_chain, FILE *file)
{
  fprintf (file, "{ ");
  while (ref)
    {
      fprintf (file, "%c%d(%d) ",
	       DF_REF_REG_DEF_P (ref) ? 'd' : 'u',
	       DF_REF_ID (ref),
	       DF_REF_REGNO (ref));
      if (follow_chain)
	df_chain_dump (DF_REF_CHAIN (ref), file);
      ref = ref->next_ref;
    }
  fprintf (file, "}");
}


/* Dump either a ref-def or reg-use chain.  */

void
df_regs_chain_dump (struct df *df ATTRIBUTE_UNUSED, struct df_ref *ref,  FILE *file)
{
  fprintf (file, "{ ");
  while (ref)
    {
      fprintf (file, "%c%d(%d) ",
	       DF_REF_REG_DEF_P (ref) ? 'd' : 'u',
	       DF_REF_ID (ref),
	       DF_REF_REGNO (ref));
      ref = ref->next_reg;
    }
  fprintf (file, "}");
}


static void
df_mws_dump (struct df_mw_hardreg *mws, FILE *file)
{
  while (mws)
    {
      struct df_link *regs = mws->regs;
      fprintf (file, "%c%d(", 
	       (mws->type == DF_REF_REG_DEF) ? 'd' : 'u',
	       DF_REF_REGNO (regs->ref));
      while (regs)
	{
	  fprintf (file, "%d ", DF_REF_REGNO (regs->ref));
	  regs = regs->next;
	}

      fprintf (file, ") "); 
      mws = mws->next;
    }
}


static void 
df_insn_uid_debug (struct df *df, unsigned int uid, 
		   bool follow_chain, FILE *file)
{
  int bbi;

  if (DF_INSN_UID_DEFS (df, uid))
    bbi = DF_REF_BBNO (DF_INSN_UID_DEFS (df, uid));
  else if (DF_INSN_UID_USES(df, uid))
    bbi = DF_REF_BBNO (DF_INSN_UID_USES (df, uid));
  else
    bbi = -1;

  fprintf (file, "insn %d bb %d luid %d",
	   uid, bbi, DF_INSN_UID_LUID (df, uid));

  if (DF_INSN_UID_DEFS (df, uid))
    {
      fprintf (file, " defs ");
      df_refs_chain_dump (DF_INSN_UID_DEFS (df, uid), follow_chain, file);
    }

  if (DF_INSN_UID_USES (df, uid))
    {
      fprintf (file, " uses ");
      df_refs_chain_dump (DF_INSN_UID_USES (df, uid), follow_chain, file);
    }

  if (DF_INSN_UID_MWS (df, uid))
    {
      fprintf (file, " mws ");
      df_mws_dump (DF_INSN_UID_MWS (df, uid), file);
    }
  fprintf (file, "\n");
}


void
df_insn_debug (struct df *df, rtx insn, bool follow_chain, FILE *file)
{
  df_insn_uid_debug (df, INSN_UID (insn), follow_chain, file);
}

void
df_insn_debug_regno (struct df *df, rtx insn, FILE *file)
{
  unsigned int uid;
  int bbi;

  uid = INSN_UID (insn);
  if (DF_INSN_UID_DEFS (df, uid))
    bbi = DF_REF_BBNO (DF_INSN_UID_DEFS (df, uid));
  else if (DF_INSN_UID_USES(df, uid))
    bbi = DF_REF_BBNO (DF_INSN_UID_USES (df, uid));
  else
    bbi = -1;

  fprintf (file, "insn %d bb %d luid %d defs ",
	   uid, bbi, DF_INSN_LUID (df, insn));
  df_regs_chain_dump (df, DF_INSN_UID_DEFS (df, uid), file);
    
  fprintf (file, " uses ");
  df_regs_chain_dump (df, DF_INSN_UID_USES (df, uid), file);
  fprintf (file, "\n");
}

void
df_regno_debug (struct df *df, unsigned int regno, FILE *file)
{
  fprintf (file, "reg %d defs ", regno);
  df_regs_chain_dump (df, DF_REG_DEF_GET (df, regno)->reg_chain, file);
  fprintf (file, " uses ");
  df_regs_chain_dump (df, DF_REG_USE_GET (df, regno)->reg_chain, file);
  fprintf (file, "\n");
}


void
df_ref_debug (struct df_ref *ref, FILE *file)
{
  fprintf (file, "%c%d ",
	   DF_REF_REG_DEF_P (ref) ? 'd' : 'u',
	   DF_REF_ID (ref));
  fprintf (file, "reg %d bb %d insn %d flag %x chain ",
	   DF_REF_REGNO (ref),
	   DF_REF_BBNO (ref),
	   DF_REF_INSN (ref) ? INSN_UID (DF_REF_INSN (ref)) : -1,
	   DF_REF_FLAGS (ref));
  df_chain_dump (DF_REF_CHAIN (ref), file);
  fprintf (file, "\n");
}

/* Functions for debugging from GDB.  */

void
debug_df_insn (rtx insn)
{
  df_insn_debug (ddf, insn, true, stderr);
  debug_rtx (insn);
}


void
debug_df_reg (rtx reg)
{
  df_regno_debug (ddf, REGNO (reg), stderr);
}


void
debug_df_regno (unsigned int regno)
{
  df_regno_debug (ddf, regno, stderr);
}


void
debug_df_ref (struct df_ref *ref)
{
  df_ref_debug (ref, stderr);
}


void
debug_df_defno (unsigned int defno)
{
  df_ref_debug (DF_DEFS_GET (ddf, defno), stderr);
}


void
debug_df_useno (unsigned int defno)
{
  df_ref_debug (DF_USES_GET (ddf, defno), stderr);
}


void
debug_df_chain (struct df_link *link)
{
  df_chain_dump (link, stderr);
  fputc ('\n', stderr);
}
