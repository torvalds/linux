/* Generic dominator tree walker
   Copyright (C) 2003, 2004, 2005 Free Software Foundation, Inc.
   Contributed by Diego Novillo <dnovillo@redhat.com>

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "basic-block.h"
#include "tree-flow.h"
#include "domwalk.h"
#include "ggc.h"

/* This file implements a generic walker for dominator trees. 

  To understand the dominator walker one must first have a grasp of dominators,
  immediate dominators and the dominator tree.

  Dominators
    A block B1 is said to dominate B2 if every path from the entry to B2 must
    pass through B1.  Given the dominance relationship, we can proceed to
    compute immediate dominators.  Note it is not important whether or not
    our definition allows a block to dominate itself.

  Immediate Dominators:
    Every block in the CFG has no more than one immediate dominator.  The
    immediate dominator of block BB must dominate BB and must not dominate
    any other dominator of BB and must not be BB itself.

  Dominator tree:
    If we then construct a tree where each node is a basic block and there
    is an edge from each block's immediate dominator to the block itself, then
    we have a dominator tree.


  [ Note this walker can also walk the post-dominator tree, which is
    defined in a similar manner.  i.e., block B1 is said to post-dominate
    block B2 if all paths from B2 to the exit block must pass through
    B1.  ]

  For example, given the CFG

                   1
                   |
                   2
                  / \
                 3   4
                    / \
       +---------->5   6
       |          / \ /
       |    +--->8   7
       |    |   /    |
       |    +--9    11
       |      /      |
       +--- 10 ---> 12
	  
  
  We have a dominator tree which looks like

                   1
                   |
                   2
                  / \
                 /   \
                3     4
                   / / \ \
                   | | | |
                   5 6 7 12
                   |   |
                   8   11
                   |
                   9
                   |
                  10
  
  
  
  The dominator tree is the basis for a number of analysis, transformation
  and optimization algorithms that operate on a semi-global basis.
  
  The dominator walker is a generic routine which visits blocks in the CFG
  via a depth first search of the dominator tree.  In the example above
  the dominator walker might visit blocks in the following order
  1, 2, 3, 4, 5, 8, 9, 10, 6, 7, 11, 12.
  
  The dominator walker has a number of callbacks to perform actions
  during the walk of the dominator tree.  There are two callbacks
  which walk statements, one before visiting the dominator children,
  one after visiting the dominator children.  There is a callback 
  before and after each statement walk callback.  In addition, the
  dominator walker manages allocation/deallocation of data structures
  which are local to each block visited.
  
  The dominator walker is meant to provide a generic means to build a pass
  which can analyze or transform/optimize a function based on walking
  the dominator tree.  One simply fills in the dominator walker data
  structure with the appropriate callbacks and calls the walker.
  
  We currently use the dominator walker to prune the set of variables
  which might need PHI nodes (which can greatly improve compile-time
  performance in some cases).
  
  We also use the dominator walker to rewrite the function into SSA form
  which reduces code duplication since the rewriting phase is inherently
  a walk of the dominator tree.

  And (of course), we use the dominator walker to drive a our dominator
  optimizer, which is a semi-global optimizer.

  TODO:

    Walking statements is based on the block statement iterator abstraction,
    which is currently an abstraction over walking tree statements.  Thus
    the dominator walker is currently only useful for trees.  */

/* Recursively walk the dominator tree.

   WALK_DATA contains a set of callbacks to perform pass-specific
   actions during the dominator walk as well as a stack of block local
   data maintained during the dominator walk.

   BB is the basic block we are currently visiting.  */

void
walk_dominator_tree (struct dom_walk_data *walk_data, basic_block bb)
{
  void *bd = NULL;
  basic_block dest;
  block_stmt_iterator bsi;
  bool is_interesting;
  basic_block *worklist = XNEWVEC (basic_block, n_basic_blocks * 2);
  int sp = 0;

  while (true)
    {
      /* Don't worry about unreachable blocks.  */
      if (EDGE_COUNT (bb->preds) > 0 || bb == ENTRY_BLOCK_PTR)
	{
	  /* If block BB is not interesting to the caller, then none of the
	     callbacks that walk the statements in BB are going to be
	     executed.  */
	  is_interesting = walk_data->interesting_blocks == NULL
	                   || TEST_BIT (walk_data->interesting_blocks,
					bb->index);

	  /* Callback to initialize the local data structure.  */
	  if (walk_data->initialize_block_local_data)
	    {
	      bool recycled;

	      /* First get some local data, reusing any local data pointer we may
	         have saved.  */
	      if (VEC_length (void_p, walk_data->free_block_data) > 0)
		{
		  bd = VEC_pop (void_p, walk_data->free_block_data);
		  recycled = 1;
		}
	      else
		{
		  bd = xcalloc (1, walk_data->block_local_data_size);
		  recycled = 0;
		}

	      /* Push the local data into the local data stack.  */
	      VEC_safe_push (void_p, heap, walk_data->block_data_stack, bd);

	      /* Call the initializer.  */
	      walk_data->initialize_block_local_data (walk_data, bb,
						      recycled);

	    }

	  /* Callback for operations to execute before we have walked the
	     dominator children, but before we walk statements.  */
	  if (walk_data->before_dom_children_before_stmts)
	    (*walk_data->before_dom_children_before_stmts) (walk_data, bb);

	  /* Statement walk before walking dominator children.  */
	  if (is_interesting && walk_data->before_dom_children_walk_stmts)
	    {
	      if (walk_data->walk_stmts_backward)
		for (bsi = bsi_last (bb); !bsi_end_p (bsi); bsi_prev (&bsi))
		  (*walk_data->before_dom_children_walk_stmts) (walk_data, bb,
								bsi);
	      else
		for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
		  (*walk_data->before_dom_children_walk_stmts) (walk_data, bb,
								bsi);
	    }

	  /* Callback for operations to execute before we have walked the
	     dominator children, and after we walk statements.  */
	  if (walk_data->before_dom_children_after_stmts)
	    (*walk_data->before_dom_children_after_stmts) (walk_data, bb);

	  /* Mark the current BB to be popped out of the recursion stack
	     once childs are processed.  */
	  worklist[sp++] = bb;
	  worklist[sp++] = NULL;

	  for (dest = first_dom_son (walk_data->dom_direction, bb);
	       dest; dest = next_dom_son (walk_data->dom_direction, dest))
	    worklist[sp++] = dest;
	}
      /* NULL is used to signalize pop operation in recursion stack.  */
      while (sp > 0 && !worklist[sp - 1])
	{
	  --sp;
	  bb = worklist[--sp];
	  is_interesting = walk_data->interesting_blocks == NULL
	                   || TEST_BIT (walk_data->interesting_blocks,
				        bb->index);
	  /* Callback for operations to execute after we have walked the
	     dominator children, but before we walk statements.  */
	  if (walk_data->after_dom_children_before_stmts)
	    (*walk_data->after_dom_children_before_stmts) (walk_data, bb);

	  /* Statement walk after walking dominator children.  */
	  if (is_interesting && walk_data->after_dom_children_walk_stmts)
	    {
	      if (walk_data->walk_stmts_backward)
		for (bsi = bsi_last (bb); !bsi_end_p (bsi); bsi_prev (&bsi))
		  (*walk_data->after_dom_children_walk_stmts) (walk_data, bb,
							       bsi);
	      else
		for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
		  (*walk_data->after_dom_children_walk_stmts) (walk_data, bb,
							       bsi);
	    }

	  /* Callback for operations to execute after we have walked the
	     dominator children and after we have walked statements.  */
	  if (walk_data->after_dom_children_after_stmts)
	    (*walk_data->after_dom_children_after_stmts) (walk_data, bb);

	  if (walk_data->initialize_block_local_data)
	    {
	      /* And finally pop the record off the block local data stack.  */
	      bd = VEC_pop (void_p, walk_data->block_data_stack);
	      /* And save the block data so that we can re-use it.  */
	      VEC_safe_push (void_p, heap, walk_data->free_block_data, bd);
	    }
	}
      if (sp)
	bb = worklist[--sp];
      else
	break;
    }
  free (worklist);
}

void
init_walk_dominator_tree (struct dom_walk_data *walk_data)
{
  walk_data->free_block_data = NULL;
  walk_data->block_data_stack = NULL;
}

void
fini_walk_dominator_tree (struct dom_walk_data *walk_data)
{
  if (walk_data->initialize_block_local_data)
    {
      while (VEC_length (void_p, walk_data->free_block_data) > 0)
	free (VEC_pop (void_p, walk_data->free_block_data));
    }

  VEC_free (void_p, heap, walk_data->free_block_data);
  VEC_free (void_p, heap, walk_data->block_data_stack);
}
