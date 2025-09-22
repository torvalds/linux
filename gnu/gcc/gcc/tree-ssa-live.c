/* Liveness for SSA trees.
   Copyright (C) 2003, 2004, 2005 Free Software Foundation, Inc.
   Contributed by Andrew MacLeod <amacleod@redhat.com>

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
#include "flags.h"
#include "basic-block.h"
#include "function.h"
#include "diagnostic.h"
#include "bitmap.h"
#include "tree-flow.h"
#include "tree-gimple.h"
#include "tree-inline.h"
#include "varray.h"
#include "timevar.h"
#include "hashtab.h"
#include "tree-dump.h"
#include "tree-ssa-live.h"
#include "toplev.h"
#include "vecprim.h"

static void live_worklist (tree_live_info_p, int *, int);
static tree_live_info_p new_tree_live_info (var_map);
static inline void set_if_valid (var_map, bitmap, tree);
static inline void add_livein_if_notdef (tree_live_info_p, bitmap,
					 tree, basic_block);
static inline void register_ssa_partition (var_map, tree, bool);
static inline void add_conflicts_if_valid (tpa_p, conflict_graph,
					   var_map, bitmap, tree);
static partition_pair_p find_partition_pair (coalesce_list_p, int, int, bool);

/* This is where the mapping from SSA version number to real storage variable
   is tracked.  

   All SSA versions of the same variable may not ultimately be mapped back to
   the same real variable. In that instance, we need to detect the live
   range overlap, and give one of the variable new storage. The vector
   'partition_to_var' tracks which partition maps to which variable.

   Given a VAR, it is sometimes desirable to know which partition that VAR
   represents.  There is an additional field in the variable annotation to
   track that information.  */

/* Create a variable partition map of SIZE, initialize and return it.  */

var_map
init_var_map (int size)
{
  var_map map;

  map = (var_map) xmalloc (sizeof (struct _var_map));
  map->var_partition = partition_new (size);
  map->partition_to_var 
	      = (tree *)xmalloc (size * sizeof (tree));
  memset (map->partition_to_var, 0, size * sizeof (tree));

  map->partition_to_compact = NULL;
  map->compact_to_partition = NULL;
  map->num_partitions = size;
  map->partition_size = size;
  map->ref_count = NULL;
  return map;
}


/* Free memory associated with MAP.  */

void
delete_var_map (var_map map)
{
  free (map->partition_to_var);
  partition_delete (map->var_partition);
  if (map->partition_to_compact)
    free (map->partition_to_compact);
  if (map->compact_to_partition)
    free (map->compact_to_partition);
  if (map->ref_count)
    free (map->ref_count);
  free (map);
}


/* This function will combine the partitions in MAP for VAR1 and VAR2.  It 
   Returns the partition which represents the new partition.  If the two 
   partitions cannot be combined, NO_PARTITION is returned.  */

int
var_union (var_map map, tree var1, tree var2)
{
  int p1, p2, p3;
  tree root_var = NULL_TREE;
  tree other_var = NULL_TREE;

  /* This is independent of partition_to_compact. If partition_to_compact is 
     on, then whichever one of these partitions is absorbed will never have a
     dereference into the partition_to_compact array any more.  */

  if (TREE_CODE (var1) == SSA_NAME)
    p1 = partition_find (map->var_partition, SSA_NAME_VERSION (var1));
  else
    {
      p1 = var_to_partition (map, var1);
      if (map->compact_to_partition)
        p1 = map->compact_to_partition[p1];
      root_var = var1;
    }
  
  if (TREE_CODE (var2) == SSA_NAME)
    p2 = partition_find (map->var_partition, SSA_NAME_VERSION (var2));
  else
    {
      p2 = var_to_partition (map, var2);
      if (map->compact_to_partition)
        p2 = map->compact_to_partition[p2];

      /* If there is no root_var set, or it's not a user variable, set the
	 root_var to this one.  */
      if (!root_var || (DECL_P (root_var) && DECL_IGNORED_P (root_var)))
        {
	  other_var = root_var;
	  root_var = var2;
	}
      else 
	other_var = var2;
    }

  gcc_assert (p1 != NO_PARTITION);
  gcc_assert (p2 != NO_PARTITION);

  if (p1 == p2)
    p3 = p1;
  else
    p3 = partition_union (map->var_partition, p1, p2);

  if (map->partition_to_compact)
    p3 = map->partition_to_compact[p3];

  if (root_var)
    change_partition_var (map, root_var, p3);
  if (other_var)
    change_partition_var (map, other_var, p3);

  return p3;
}


/* Compress the partition numbers in MAP such that they fall in the range 
   0..(num_partitions-1) instead of wherever they turned out during
   the partitioning exercise.  This removes any references to unused
   partitions, thereby allowing bitmaps and other vectors to be much
   denser.  Compression type is controlled by FLAGS.

   This is implemented such that compaction doesn't affect partitioning.
   Ie., once partitions are created and possibly merged, running one
   or more different kind of compaction will not affect the partitions
   themselves.  Their index might change, but all the same variables will
   still be members of the same partition group.  This allows work on reduced
   sets, and no loss of information when a larger set is later desired.

   In particular, coalescing can work on partitions which have 2 or more
   definitions, and then 'recompact' later to include all the single
   definitions for assignment to program variables.  */

void 
compact_var_map (var_map map, int flags)
{
  sbitmap used;
  int tmp, root, root_i;
  unsigned int x, limit, count;
  tree var;
  root_var_p rv = NULL;

  limit = map->partition_size;
  used = sbitmap_alloc (limit);
  sbitmap_zero (used);

  /* Already compressed? Abandon the old one.  */
  if (map->partition_to_compact)
    {
      free (map->partition_to_compact);
      map->partition_to_compact = NULL;
    }
  if (map->compact_to_partition)
    {
      free (map->compact_to_partition);
      map->compact_to_partition = NULL;
    }

  map->num_partitions = map->partition_size;

  if (flags & VARMAP_NO_SINGLE_DEFS)
    rv = root_var_init (map);

  map->partition_to_compact = (int *)xmalloc (limit * sizeof (int));
  memset (map->partition_to_compact, 0xff, (limit * sizeof (int)));

  /* Find out which partitions are actually referenced.  */
  count = 0;
  for (x = 0; x < limit; x++)
    {
      tmp = partition_find (map->var_partition, x);
      if (!TEST_BIT (used, tmp) && map->partition_to_var[tmp] != NULL_TREE)
        {
	  /* It is referenced, check to see if there is more than one version
	     in the root_var table, if one is available.  */
	  if (rv)
	    {
	      root = root_var_find (rv, tmp);
	      root_i = root_var_first_partition (rv, root);
	      /* If there is only one, don't include this in the compaction.  */
	      if (root_var_next_partition (rv, root_i) == ROOT_VAR_NONE)
	        continue;
	    }
	  SET_BIT (used, tmp);
	  count++;
	}
    }

  /* Build a compacted partitioning.  */
  if (count != limit)
    {
      sbitmap_iterator sbi;

      map->compact_to_partition = (int *)xmalloc (count * sizeof (int));
      count = 0;
      /* SSA renaming begins at 1, so skip 0 when compacting.  */
      EXECUTE_IF_SET_IN_SBITMAP (used, 1, x, sbi)
	{
	  map->partition_to_compact[x] = count;
	  map->compact_to_partition[count] = x;
	  var = map->partition_to_var[x];
	  if (TREE_CODE (var) != SSA_NAME)
	    change_partition_var (map, var, count);
	  count++;
	}
    }
  else
    {
      free (map->partition_to_compact);
      map->partition_to_compact = NULL;
    }

  map->num_partitions = count;

  if (rv)
    root_var_delete (rv);
  sbitmap_free (used);
}


/* This function is used to change the representative variable in MAP for VAR's 
   partition from an SSA_NAME variable to a regular variable.  This allows 
   partitions to be mapped back to real variables.  */
  
void 
change_partition_var (var_map map, tree var, int part)
{
  var_ann_t ann;

  gcc_assert (TREE_CODE (var) != SSA_NAME);

  ann = var_ann (var);
  ann->out_of_ssa_tag = 1;
  VAR_ANN_PARTITION (ann) = part;
  if (map->compact_to_partition)
    map->partition_to_var[map->compact_to_partition[part]] = var;
}

static inline void mark_all_vars_used (tree *);

/* Helper function for mark_all_vars_used, called via walk_tree.  */

static tree
mark_all_vars_used_1 (tree *tp, int *walk_subtrees,
		      void *data ATTRIBUTE_UNUSED)
{
  tree t = *tp;

  if (TREE_CODE (t) == SSA_NAME)
    t = SSA_NAME_VAR (t);

  /* Ignore TREE_ORIGINAL for TARGET_MEM_REFS, as well as other
     fields that do not contain vars.  */
  if (TREE_CODE (t) == TARGET_MEM_REF)
    {
      mark_all_vars_used (&TMR_SYMBOL (t));
      mark_all_vars_used (&TMR_BASE (t));
      mark_all_vars_used (&TMR_INDEX (t));
      *walk_subtrees = 0;
      return NULL;
    }

  /* Only need to mark VAR_DECLS; parameters and return results are not
     eliminated as unused.  */
  if (TREE_CODE (t) == VAR_DECL)
    set_is_used (t);

  if (IS_TYPE_OR_DECL_P (t))
    *walk_subtrees = 0;

  return NULL;
}

/* Mark all VAR_DECLS under *EXPR_P as used, so that they won't be 
   eliminated during the tree->rtl conversion process.  */

static inline void
mark_all_vars_used (tree *expr_p)
{
  walk_tree (expr_p, mark_all_vars_used_1, NULL, NULL);
}


/* Remove local variables that are not referenced in the IL.  */

void
remove_unused_locals (void)
{
  basic_block bb;
  tree t, *cell;

  /* Assume all locals are unused.  */
  for (t = cfun->unexpanded_var_list; t; t = TREE_CHAIN (t))
    {
      tree var = TREE_VALUE (t);
      if (TREE_CODE (var) != FUNCTION_DECL
	  && var_ann (var))
	var_ann (var)->used = false;
    }

  /* Walk the CFG marking all referenced symbols.  */
  FOR_EACH_BB (bb)
    {
      block_stmt_iterator bsi;
      tree phi, def;

      /* Walk the statements.  */
      for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
	mark_all_vars_used (bsi_stmt_ptr (bsi));

      for (phi = phi_nodes (bb); phi; phi = PHI_CHAIN (phi))
        {
          use_operand_p arg_p;
          ssa_op_iter i;

	  /* No point processing globals.  */
	  if (is_global_var (SSA_NAME_VAR (PHI_RESULT (phi))))
	    continue;

          def = PHI_RESULT (phi);
          mark_all_vars_used (&def);

          FOR_EACH_PHI_ARG (arg_p, phi, i, SSA_OP_ALL_USES)
            {
	      tree arg = USE_FROM_PTR (arg_p);
	      mark_all_vars_used (&arg);
            }
        }
    }

  /* Remove unmarked vars and clear used flag.  */
  for (cell = &cfun->unexpanded_var_list; *cell; )
    {
      tree var = TREE_VALUE (*cell);
      var_ann_t ann;

      if (TREE_CODE (var) != FUNCTION_DECL
	  && (!(ann = var_ann (var))
	      || !ann->used))
	{
	  *cell = TREE_CHAIN (*cell);
	  continue;
	}

      cell = &TREE_CHAIN (*cell);
    }
}

/* This function looks through the program and uses FLAGS to determine what 
   SSA versioned variables are given entries in a new partition table.  This
   new partition map is returned.  */

var_map
create_ssa_var_map (int flags)
{
  block_stmt_iterator bsi;
  basic_block bb;
  tree dest, use;
  tree stmt;
  var_map map;
  ssa_op_iter iter;
#ifdef ENABLE_CHECKING
  bitmap used_in_real_ops;
  bitmap used_in_virtual_ops;
#endif

  map = init_var_map (num_ssa_names + 1);

#ifdef ENABLE_CHECKING
  used_in_real_ops = BITMAP_ALLOC (NULL);
  used_in_virtual_ops = BITMAP_ALLOC (NULL);
#endif

  if (flags & SSA_VAR_MAP_REF_COUNT)
    {
      map->ref_count
	= (int *)xmalloc (((num_ssa_names + 1) * sizeof (int)));
      memset (map->ref_count, 0, (num_ssa_names + 1) * sizeof (int));
    }

  FOR_EACH_BB (bb)
    {
      tree phi, arg;

      for (phi = phi_nodes (bb); phi; phi = PHI_CHAIN (phi))
	{
	  int i;
	  register_ssa_partition (map, PHI_RESULT (phi), false);
	  for (i = 0; i < PHI_NUM_ARGS (phi); i++)
	    {
	      arg = PHI_ARG_DEF (phi, i);
	      if (TREE_CODE (arg) == SSA_NAME)
		register_ssa_partition (map, arg, true);

	      mark_all_vars_used (&PHI_ARG_DEF_TREE (phi, i));
	    }
	}

      for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
        {
	  stmt = bsi_stmt (bsi);

	  /* Register USE and DEF operands in each statement.  */
	  FOR_EACH_SSA_TREE_OPERAND (use , stmt, iter, SSA_OP_USE)
	    {
	      register_ssa_partition (map, use, true);

#ifdef ENABLE_CHECKING
	      bitmap_set_bit (used_in_real_ops, DECL_UID (SSA_NAME_VAR (use)));
#endif
	    }

	  FOR_EACH_SSA_TREE_OPERAND (dest, stmt, iter, SSA_OP_DEF)
	    {
	      register_ssa_partition (map, dest, false);

#ifdef ENABLE_CHECKING
	      bitmap_set_bit (used_in_real_ops, DECL_UID (SSA_NAME_VAR (dest)));
#endif
	    }

#ifdef ENABLE_CHECKING
	  /* Validate that virtual ops don't get used in funny ways.  */
	  FOR_EACH_SSA_TREE_OPERAND (use, stmt, iter, 
				     SSA_OP_VIRTUAL_USES | SSA_OP_VMUSTDEF)
	    {
	      bitmap_set_bit (used_in_virtual_ops, 
			      DECL_UID (SSA_NAME_VAR (use)));
	    }

#endif /* ENABLE_CHECKING */

	  mark_all_vars_used (bsi_stmt_ptr (bsi));
	}
    }

#if defined ENABLE_CHECKING
  {
    unsigned i;
    bitmap both = BITMAP_ALLOC (NULL);
    bitmap_and (both, used_in_real_ops, used_in_virtual_ops);
    if (!bitmap_empty_p (both))
      {
	bitmap_iterator bi;

	EXECUTE_IF_SET_IN_BITMAP (both, 0, i, bi)
	  fprintf (stderr, "Variable %s used in real and virtual operands\n",
		   get_name (referenced_var (i)));
	internal_error ("SSA corruption");
      }

    BITMAP_FREE (used_in_real_ops);
    BITMAP_FREE (used_in_virtual_ops);
    BITMAP_FREE (both);
  }
#endif

  return map;
}


/* Allocate and return a new live range information object base on MAP.  */

static tree_live_info_p
new_tree_live_info (var_map map)
{
  tree_live_info_p live;
  unsigned x;

  live = (tree_live_info_p) xmalloc (sizeof (struct tree_live_info_d));
  live->map = map;
  live->num_blocks = last_basic_block;

  live->global = BITMAP_ALLOC (NULL);

  live->livein = (bitmap *)xmalloc (num_var_partitions (map) * sizeof (bitmap));
  for (x = 0; x < num_var_partitions (map); x++)
    live->livein[x] = BITMAP_ALLOC (NULL);

  /* liveout is deferred until it is actually requested.  */
  live->liveout = NULL;
  return live;
}


/* Free storage for live range info object LIVE.  */

void 
delete_tree_live_info (tree_live_info_p live)
{
  int x;
  if (live->liveout)
    {
      for (x = live->num_blocks - 1; x >= 0; x--)
        BITMAP_FREE (live->liveout[x]);
      free (live->liveout);
    }
  if (live->livein)
    {
      for (x = num_var_partitions (live->map) - 1; x >= 0; x--)
        BITMAP_FREE (live->livein[x]);
      free (live->livein);
    }
  if (live->global)
    BITMAP_FREE (live->global);
  
  free (live);
}


/* Using LIVE, fill in all the live-on-entry blocks between the defs and uses 
   for partition I.  STACK is a varray used for temporary memory which is 
   passed in rather than being allocated on every call.  */

static void
live_worklist (tree_live_info_p live, int *stack, int i)
{
  unsigned b;
  tree var;
  basic_block def_bb = NULL;
  edge e;
  var_map map = live->map;
  edge_iterator ei;
  bitmap_iterator bi;
  int *tos = stack;

  var = partition_to_var (map, i);
  if (SSA_NAME_DEF_STMT (var))
    def_bb = bb_for_stmt (SSA_NAME_DEF_STMT (var));

  EXECUTE_IF_SET_IN_BITMAP (live->livein[i], 0, b, bi)
    {
      *tos++ = b;
    }

  while (tos != stack)
    {
      b = *--tos;

      FOR_EACH_EDGE (e, ei, BASIC_BLOCK (b)->preds)
	if (e->src != ENTRY_BLOCK_PTR)
	  {
	    /* Its not live on entry to the block its defined in.  */
	    if (e->src == def_bb)
	      continue;
	    if (!bitmap_bit_p (live->livein[i], e->src->index))
	      {
		bitmap_set_bit (live->livein[i], e->src->index);
		*tos++ = e->src->index;
	      }
	  }
    }
}


/* If VAR is in a partition of MAP, set the bit for that partition in VEC.  */

static inline void
set_if_valid (var_map map, bitmap vec, tree var)
{
  int p = var_to_partition (map, var);
  if (p != NO_PARTITION)
    bitmap_set_bit (vec, p);
}


/* If VAR is in a partition and it isn't defined in DEF_VEC, set the livein and 
   global bit for it in the LIVE object.  BB is the block being processed.  */

static inline void
add_livein_if_notdef (tree_live_info_p live, bitmap def_vec,
		      tree var, basic_block bb)
{
  int p = var_to_partition (live->map, var);
  if (p == NO_PARTITION || bb == ENTRY_BLOCK_PTR)
    return;
  if (!bitmap_bit_p (def_vec, p))
    {
      bitmap_set_bit (live->livein[p], bb->index);
      bitmap_set_bit (live->global, p);
    }
}


/* Given partition map MAP, calculate all the live on entry bitmaps for 
   each basic block.  Return a live info object.  */

tree_live_info_p 
calculate_live_on_entry (var_map map)
{
  tree_live_info_p live;
  unsigned i;
  basic_block bb;
  bitmap saw_def;
  tree phi, var, stmt;
  tree op;
  edge e;
  int *stack;
  block_stmt_iterator bsi;
  ssa_op_iter iter;
  bitmap_iterator bi;
#ifdef ENABLE_CHECKING
  int num;
  edge_iterator ei;
#endif

  saw_def = BITMAP_ALLOC (NULL);

  live = new_tree_live_info (map);

  FOR_EACH_BB (bb)
    {
      bitmap_clear (saw_def);

      for (phi = phi_nodes (bb); phi; phi = PHI_CHAIN (phi))
	{
	  for (i = 0; i < (unsigned)PHI_NUM_ARGS (phi); i++)
	    {
	      var = PHI_ARG_DEF (phi, i);
	      if (!phi_ssa_name_p (var))
	        continue;
	      stmt = SSA_NAME_DEF_STMT (var);
	      e = EDGE_PRED (bb, i);

	      /* Any uses in PHIs which either don't have def's or are not
	         defined in the block from which the def comes, will be live
		 on entry to that block.  */
	      if (!stmt || e->src != bb_for_stmt (stmt))
		add_livein_if_notdef (live, saw_def, var, e->src);
	    }
        }

      /* Don't mark PHI results as defined until all the PHI nodes have
	 been processed. If the PHI sequence is:
	    a_3 = PHI <a_1, a_2>
	    b_3 = PHI <b_1, a_3>
	 The a_3 referred to in b_3's PHI node is the one incoming on the
	 edge, *not* the PHI node just seen.  */

      for (phi = phi_nodes (bb); phi; phi = PHI_CHAIN (phi))
        {
	  var = PHI_RESULT (phi);
	  set_if_valid (map, saw_def, var);
	}

      for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
        {
	  stmt = bsi_stmt (bsi);

	  FOR_EACH_SSA_TREE_OPERAND (op, stmt, iter, SSA_OP_USE)
	    {
	      add_livein_if_notdef (live, saw_def, op, bb);
	    }

	  FOR_EACH_SSA_TREE_OPERAND (op, stmt, iter, SSA_OP_DEF)
	    {
	      set_if_valid (map, saw_def, op);
	    }
	}
    }

  stack = XNEWVEC (int, last_basic_block);
  EXECUTE_IF_SET_IN_BITMAP (live->global, 0, i, bi)
    {
      live_worklist (live, stack, i);
    }
  free (stack);

#ifdef ENABLE_CHECKING
   /* Check for live on entry partitions and report those with a DEF in
      the program. This will typically mean an optimization has done
      something wrong.  */

  bb = ENTRY_BLOCK_PTR;
  num = 0;
  FOR_EACH_EDGE (e, ei, bb->succs)
    {
      int entry_block = e->dest->index;
      if (e->dest == EXIT_BLOCK_PTR)
        continue;
      for (i = 0; i < (unsigned)num_var_partitions (map); i++)
	{
	  basic_block tmp;
	  tree d;
	  var = partition_to_var (map, i);
	  stmt = SSA_NAME_DEF_STMT (var);
	  tmp = bb_for_stmt (stmt);
	  d = default_def (SSA_NAME_VAR (var));

	  if (bitmap_bit_p (live_entry_blocks (live, i), entry_block))
	    {
	      if (!IS_EMPTY_STMT (stmt))
		{
		  num++;
		  print_generic_expr (stderr, var, TDF_SLIM);
		  fprintf (stderr, " is defined ");
		  if (tmp)
		    fprintf (stderr, " in BB%d, ", tmp->index);
		  fprintf (stderr, "by:\n");
		  print_generic_expr (stderr, stmt, TDF_SLIM);
		  fprintf (stderr, "\nIt is also live-on-entry to entry BB %d", 
			   entry_block);
		  fprintf (stderr, " So it appears to have multiple defs.\n");
		}
	      else
	        {
		  if (d != var)
		    {
		      num++;
		      print_generic_expr (stderr, var, TDF_SLIM);
		      fprintf (stderr, " is live-on-entry to BB%d ",entry_block);
		      if (d)
		        {
			  fprintf (stderr, " but is not the default def of ");
			  print_generic_expr (stderr, d, TDF_SLIM);
			  fprintf (stderr, "\n");
			}
		      else
			fprintf (stderr, " and there is no default def.\n");
		    }
		}
	    }
	  else
	    if (d == var)
	      {
		/* The only way this var shouldn't be marked live on entry is 
		   if it occurs in a PHI argument of the block.  */
		int z, ok = 0;
		for (phi = phi_nodes (e->dest); 
		     phi && !ok; 
		     phi = PHI_CHAIN (phi))
		  {
		    for (z = 0; z < PHI_NUM_ARGS (phi); z++)
		      if (var == PHI_ARG_DEF (phi, z))
			{
			  ok = 1;
			  break;
			}
		  }
		if (ok)
		  continue;
	        num++;
		print_generic_expr (stderr, var, TDF_SLIM);
		fprintf (stderr, " is not marked live-on-entry to entry BB%d ", 
			 entry_block);
		fprintf (stderr, "but it is a default def so it should be.\n");
	      }
	}
    }
  gcc_assert (num <= 0);
#endif

  BITMAP_FREE (saw_def);

  return live;
}


/* Calculate the live on exit vectors based on the entry info in LIVEINFO.  */

void
calculate_live_on_exit (tree_live_info_p liveinfo)
{
  unsigned b;
  unsigned i, x;
  bitmap *on_exit;
  basic_block bb;
  edge e;
  tree t, phi;
  bitmap on_entry;
  var_map map = liveinfo->map;

  on_exit = (bitmap *)xmalloc (last_basic_block * sizeof (bitmap));
  for (x = 0; x < (unsigned)last_basic_block; x++)
    on_exit[x] = BITMAP_ALLOC (NULL);

  /* Set all the live-on-exit bits for uses in PHIs.  */
  FOR_EACH_BB (bb)
    {
      for (phi = phi_nodes (bb); phi; phi = PHI_CHAIN (phi))
	for (i = 0; i < (unsigned)PHI_NUM_ARGS (phi); i++)
	  { 
	    t = PHI_ARG_DEF (phi, i);
	    e = PHI_ARG_EDGE (phi, i);
	    if (!phi_ssa_name_p (t) || e->src == ENTRY_BLOCK_PTR)
	      continue;
	    set_if_valid (map, on_exit[e->src->index], t);
	  }
    }

  /* Set live on exit for all predecessors of live on entry's.  */
  for (i = 0; i < num_var_partitions (map); i++)
    {
      bitmap_iterator bi;

      on_entry = live_entry_blocks (liveinfo, i);
      EXECUTE_IF_SET_IN_BITMAP (on_entry, 0, b, bi)
        {
	  edge_iterator ei;
	  FOR_EACH_EDGE (e, ei, BASIC_BLOCK (b)->preds)
	    if (e->src != ENTRY_BLOCK_PTR)
	      bitmap_set_bit (on_exit[e->src->index], i);
	}
    }

  liveinfo->liveout = on_exit;
}


/* Initialize a tree_partition_associator object using MAP.  */

static tpa_p
tpa_init (var_map map)
{
  tpa_p tpa;
  int num_partitions = num_var_partitions (map);
  int x;

  if (num_partitions == 0)
    return NULL;

  tpa = (tpa_p) xmalloc (sizeof (struct tree_partition_associator_d));
  tpa->num_trees = 0;
  tpa->uncompressed_num = -1;
  tpa->map = map;
  tpa->next_partition = (int *)xmalloc (num_partitions * sizeof (int));
  memset (tpa->next_partition, TPA_NONE, num_partitions * sizeof (int));

  tpa->partition_to_tree_map = (int *)xmalloc (num_partitions * sizeof (int));
  memset (tpa->partition_to_tree_map, TPA_NONE, num_partitions * sizeof (int));

  x = MAX (40, (num_partitions / 20));
  tpa->trees = VEC_alloc (tree, heap, x);
  tpa->first_partition = VEC_alloc (int, heap, x);

  return tpa;

}


/* Remove PARTITION_INDEX from TREE_INDEX's list in the tpa structure TPA.  */

void
tpa_remove_partition (tpa_p tpa, int tree_index, int partition_index)
{
  int i;

  i = tpa_first_partition (tpa, tree_index);
  if (i == partition_index)
    {
      VEC_replace (int, tpa->first_partition, tree_index,
		   tpa->next_partition[i]);
    }
  else
    {
      for ( ; i != TPA_NONE; i = tpa_next_partition (tpa, i))
        {
	  if (tpa->next_partition[i] == partition_index)
	    {
	      tpa->next_partition[i] = tpa->next_partition[partition_index];
	      break;
	    }
	}
    }
}


/* Free the memory used by tree_partition_associator object TPA.  */

void
tpa_delete (tpa_p tpa)
{
  if (!tpa)
    return;

  VEC_free (tree, heap, tpa->trees);
  VEC_free (int, heap, tpa->first_partition);
  free (tpa->partition_to_tree_map);
  free (tpa->next_partition);
  free (tpa);
}


/* This function will remove any tree entries from TPA which have only a single
   element.  This will help keep the size of the conflict graph down.  The 
   function returns the number of remaining tree lists.  */

int 
tpa_compact (tpa_p tpa)
{
  int last, x, y, first, swap_i;
  tree swap_t;

  /* Find the last list which has more than 1 partition.  */
  for (last = tpa->num_trees - 1; last > 0; last--)
    {
      first = tpa_first_partition (tpa, last);
      if (tpa_next_partition (tpa, first) != NO_PARTITION)
        break;
    }

  x = 0;
  while (x < last)
    {
      first = tpa_first_partition (tpa, x);

      /* If there is not more than one partition, swap with the current end
	 of the tree list.  */
      if (tpa_next_partition (tpa, first) == NO_PARTITION)
        {
	  swap_t = VEC_index (tree, tpa->trees, last);
	  swap_i = VEC_index (int, tpa->first_partition, last);

	  /* Update the last entry. Since it is known to only have one
	     partition, there is nothing else to update.  */
	  VEC_replace (tree, tpa->trees, last,
		       VEC_index (tree, tpa->trees, x));
	  VEC_replace (int, tpa->first_partition, last,
		       VEC_index (int, tpa->first_partition, x));
	  tpa->partition_to_tree_map[tpa_first_partition (tpa, last)] = last;

	  /* Since this list is known to have more than one partition, update
	     the list owner entries.  */
	  VEC_replace (tree, tpa->trees, x, swap_t);
	  VEC_replace (int, tpa->first_partition, x, swap_i);
	  for (y = tpa_first_partition (tpa, x); 
	       y != NO_PARTITION; 
	       y = tpa_next_partition (tpa, y))
	    tpa->partition_to_tree_map[y] = x;

	  /* Ensure last is a list with more than one partition.  */
	  last--;
	  for (; last > x; last--)
	    {
	      first = tpa_first_partition (tpa, last);
	      if (tpa_next_partition (tpa, first) != NO_PARTITION)
		break;
	    }
	}
      x++;
    }

  first = tpa_first_partition (tpa, x);
  if (tpa_next_partition (tpa, first) != NO_PARTITION)
    x++;
  tpa->uncompressed_num = tpa->num_trees;
  tpa->num_trees = x;
  return last;
}


/* Initialize a root_var object with SSA partitions from MAP which are based 
   on each root variable.  */

root_var_p
root_var_init (var_map map)
{
  root_var_p rv;
  int num_partitions = num_var_partitions (map);
  int x, p;
  tree t;
  var_ann_t ann;
  sbitmap seen;

  rv = tpa_init (map);
  if (!rv)
    return NULL;

  seen = sbitmap_alloc (num_partitions);
  sbitmap_zero (seen);

  /* Start at the end and work towards the front. This will provide a list
     that is ordered from smallest to largest.  */
  for (x = num_partitions - 1; x >= 0; x--)
    {
      t = partition_to_var (map, x);

      /* The var map may not be compacted yet, so check for NULL.  */
      if (!t) 
        continue;

      p = var_to_partition (map, t);

      gcc_assert (p != NO_PARTITION);

      /* Make sure we only put coalesced partitions into the list once.  */
      if (TEST_BIT (seen, p))
        continue;
      SET_BIT (seen, p);
      if (TREE_CODE (t) == SSA_NAME)
	t = SSA_NAME_VAR (t);
      ann = var_ann (t);
      if (ann->root_var_processed)
        {
	  rv->next_partition[p] = VEC_index (int, rv->first_partition, 
					     VAR_ANN_ROOT_INDEX (ann));
	  VEC_replace (int, rv->first_partition, VAR_ANN_ROOT_INDEX (ann), p);
	}
      else
        {
	  ann->root_var_processed = 1;
	  VAR_ANN_ROOT_INDEX (ann) = rv->num_trees++;
	  VEC_safe_push (tree, heap, rv->trees, t);
	  VEC_safe_push (int, heap, rv->first_partition, p);
	}
      rv->partition_to_tree_map[p] = VAR_ANN_ROOT_INDEX (ann);
    }

  /* Reset the out_of_ssa_tag flag on each variable for later use.  */
  for (x = 0; x < rv->num_trees; x++)
    {
      t = VEC_index (tree, rv->trees, x);
      var_ann (t)->root_var_processed = 0;
    }

  sbitmap_free (seen);
  return rv;
}


/* Initialize a type_var structure which associates all the partitions in MAP 
   of the same type to the type node's index.  Volatiles are ignored.  */

type_var_p
type_var_init (var_map map)
{
  type_var_p tv;
  int x, y, p;
  int num_partitions = num_var_partitions (map);
  tree t;
  sbitmap seen;

  tv = tpa_init (map);
  if (!tv)
    return NULL;

  seen = sbitmap_alloc (num_partitions);
  sbitmap_zero (seen);

  for (x = num_partitions - 1; x >= 0; x--)
    {
      t = partition_to_var (map, x);

      /* Disallow coalescing of these types of variables.  */
      if (!t
	  || TREE_THIS_VOLATILE (t)
	  || TREE_CODE (t) == RESULT_DECL
      	  || TREE_CODE (t) == PARM_DECL 
	  || (DECL_P (t)
	      && (DECL_REGISTER (t)
		  || !DECL_IGNORED_P (t)
		  || DECL_RTL_SET_P (t))))
        continue;

      p = var_to_partition (map, t);

      gcc_assert (p != NO_PARTITION);

      /* If partitions have been coalesced, only add the representative 
	 for the partition to the list once.  */
      if (TEST_BIT (seen, p))
        continue;
      SET_BIT (seen, p);
      t = TREE_TYPE (t);

      /* Find the list for this type.  */
      for (y = 0; y < tv->num_trees; y++)
        if (t == VEC_index (tree, tv->trees, y))
	  break;
      if (y == tv->num_trees)
        {
	  tv->num_trees++;
	  VEC_safe_push (tree, heap, tv->trees, t);
	  VEC_safe_push (int, heap, tv->first_partition, p);
	}
      else
        {
	  tv->next_partition[p] = VEC_index (int, tv->first_partition, y);
	  VEC_replace (int, tv->first_partition, y, p);
	}
      tv->partition_to_tree_map[p] = y;
    }
  sbitmap_free (seen);
  return tv;
}


/* Create a new coalesce list object from MAP and return it.  */

coalesce_list_p 
create_coalesce_list (var_map map)
{
  coalesce_list_p list;

  list = (coalesce_list_p) xmalloc (sizeof (struct coalesce_list_d));

  list->map = map;
  list->add_mode = true;
  list->list = (partition_pair_p *) xcalloc (num_var_partitions (map),
					     sizeof (struct partition_pair_d));
  return list;
}


/* Delete coalesce list CL.  */

void 
delete_coalesce_list (coalesce_list_p cl)
{
  free (cl->list);
  free (cl);
}


/* Find a matching coalesce pair object in CL for partitions P1 and P2.  If 
   one isn't found, return NULL if CREATE is false, otherwise create a new 
   coalesce pair object and return it.  */

static partition_pair_p
find_partition_pair (coalesce_list_p cl, int p1, int p2, bool create)
{
  partition_pair_p node, tmp;
  int s;
    
  /* Normalize so that p1 is the smaller value.  */
  if (p2 < p1)
    {
      s = p1;
      p1 = p2;
      p2 = s;
    }
  
  tmp = NULL;

  /* The list is sorted such that if we find a value greater than p2,
     p2 is not in the list.  */
  for (node = cl->list[p1]; node; node = node->next)
    {
      if (node->second_partition == p2)
        return node;
      else
        if (node->second_partition > p2)
	  break;
     tmp = node;
    }

  if (!create)
    return NULL;

  node = (partition_pair_p) xmalloc (sizeof (struct partition_pair_d));
  node->first_partition = p1;
  node->second_partition = p2;
  node->cost = 0;
    
  if (tmp != NULL)
    {
      node->next = tmp->next;
      tmp->next = node;
    }
  else
    {
      /* This is now the first node in the list.  */
      node->next = cl->list[p1];
      cl->list[p1] = node;
    }

  return node;
}

/* Return cost of execution of copy instruction with FREQUENCY
   possibly on CRITICAL edge and in HOT basic block.  */
int
coalesce_cost (int frequency, bool hot, bool critical)
{
  /* Base costs on BB frequencies bounded by 1.  */
  int cost = frequency;

  if (!cost)
    cost = 1;
  if (optimize_size || hot)
    cost = 1;
  /* Inserting copy on critical edge costs more
     than inserting it elsewhere.  */
  if (critical)
    cost *= 2;
  return cost;
}

/* Add a potential coalesce between P1 and P2 in CL with a cost of VALUE.  */

void 
add_coalesce (coalesce_list_p cl, int p1, int p2,
	      int value)
{
  partition_pair_p node;

  gcc_assert (cl->add_mode);

  if (p1 == p2)
    return;

  node = find_partition_pair (cl, p1, p2, true);

  node->cost += value;
}


/* Comparison function to allow qsort to sort P1 and P2 in descending order.  */

static
int compare_pairs (const void *p1, const void *p2)
{
  return (*(partition_pair_p *)p2)->cost - (*(partition_pair_p *)p1)->cost;
}


/* Prepare CL for removal of preferred pairs.  When finished, list element 
   0 has all the coalesce pairs, sorted in order from most important coalesce 
   to least important.  */

void
sort_coalesce_list (coalesce_list_p cl)
{
  unsigned x, num, count;
  partition_pair_p chain, p;
  partition_pair_p  *list;

  gcc_assert (cl->add_mode);

  cl->add_mode = false;

  /* Compact the array of lists to a single list, and count the elements.  */
  num = 0;
  chain = NULL;
  for (x = 0; x < num_var_partitions (cl->map); x++)
    if (cl->list[x] != NULL)
      {
        for (p = cl->list[x]; p->next != NULL; p = p->next)
	  num++;
	num++;
	p->next = chain;
	chain = cl->list[x];
	cl->list[x] = NULL;
      }

  /* Only call qsort if there are more than 2 items.  */
  if (num > 2)
    {
      list = XNEWVEC (partition_pair_p, num);
      count = 0;
      for (p = chain; p != NULL; p = p->next)
	list[count++] = p;

      gcc_assert (count == num);
	
      qsort (list, count, sizeof (partition_pair_p), compare_pairs);

      p = list[0];
      for (x = 1; x < num; x++)
	{
	  p->next = list[x];
	  p = list[x];
	}
      p->next = NULL;
      cl->list[0] = list[0];
      free (list);
    }
  else
    {
      cl->list[0] = chain;
      if (num == 2)
	{
	  /* Simply swap the two elements if they are in the wrong order.  */
	  if (chain->cost < chain->next->cost)
	    {
	      cl->list[0] = chain->next;
	      cl->list[0]->next = chain;
	      chain->next = NULL;
	    }
	}
    }
}


/* Retrieve the best remaining pair to coalesce from CL.  Returns the 2 
   partitions via P1 and P2.  Their calculated cost is returned by the function.
   NO_BEST_COALESCE is returned if the coalesce list is empty.  */

static int
pop_best_coalesce (coalesce_list_p cl, int *p1, int *p2)
{
  partition_pair_p node;
  int ret;

  gcc_assert (!cl->add_mode);

  node = cl->list[0];
  if (!node)
    return NO_BEST_COALESCE;

  cl->list[0] = node->next;

  *p1 = node->first_partition;
  *p2 = node->second_partition;
  ret = node->cost;
  free (node);

  return ret;
}


/* If variable VAR is in a partition in MAP, add a conflict in GRAPH between 
   VAR and any other live partitions in VEC which are associated via TPA.  
   Reset the live bit in VEC.  */

static inline void 
add_conflicts_if_valid (tpa_p tpa, conflict_graph graph,
			var_map map, bitmap vec, tree var)
{ 
  int p, y, first;
  p = var_to_partition (map, var);
  if (p != NO_PARTITION)
    { 
      bitmap_clear_bit (vec, p);
      first = tpa_find_tree (tpa, p);
      /* If find returns nothing, this object isn't interesting.  */
      if (first == TPA_NONE)
        return;
      /* Only add interferences between objects in the same list.  */
      for (y = tpa_first_partition (tpa, first);
	   y != TPA_NONE;
	   y = tpa_next_partition (tpa, y))
	{
	  if (bitmap_bit_p (vec, y))
	    conflict_graph_add (graph, p, y);
	}
    }
}

/* Return a conflict graph for the information contained in LIVE_INFO.  Only
   conflicts between items in the same TPA list are added.  If optional 
   coalesce list CL is passed in, any copies encountered are added.  */

conflict_graph
build_tree_conflict_graph (tree_live_info_p liveinfo, tpa_p tpa, 
			   coalesce_list_p cl)
{
  conflict_graph graph;
  var_map map;
  bitmap live;
  unsigned x, y, i;
  basic_block bb;
  int *partition_link, *tpa_nodes;
  VEC(int,heap) *tpa_to_clear;
  unsigned l;
  ssa_op_iter iter;
  bitmap_iterator bi;

  map = live_var_map (liveinfo);
  graph = conflict_graph_new (num_var_partitions (map));

  if (tpa_num_trees (tpa) == 0)
    return graph;

  live = BITMAP_ALLOC (NULL);

  partition_link = XCNEWVEC (int, num_var_partitions (map) + 1);
  tpa_nodes = XCNEWVEC (int, tpa_num_trees (tpa));
  tpa_to_clear = VEC_alloc (int, heap, 50);

  FOR_EACH_BB (bb)
    {
      block_stmt_iterator bsi;
      tree phi;
      int idx;

      /* Start with live on exit temporaries.  */
      bitmap_copy (live, live_on_exit (liveinfo, bb));

      for (bsi = bsi_last (bb); !bsi_end_p (bsi); bsi_prev (&bsi))
        {
	  bool is_a_copy = false;
	  tree stmt = bsi_stmt (bsi);

	  /* A copy between 2 partitions does not introduce an interference 
	     by itself.  If they did, you would never be able to coalesce 
	     two things which are copied.  If the two variables really do 
	     conflict, they will conflict elsewhere in the program.  
	     
	     This is handled specially here since we may also be interested 
	     in copies between real variables and SSA_NAME variables.  We may
	     be interested in trying to coalesce SSA_NAME variables with
	     root variables in some cases.  */

	  if (TREE_CODE (stmt) == MODIFY_EXPR)
	    {
	      tree lhs = TREE_OPERAND (stmt, 0);
	      tree rhs = TREE_OPERAND (stmt, 1);
	      int p1, p2;
	      int bit;

	      if (DECL_P (lhs) || TREE_CODE (lhs) == SSA_NAME)
		p1 = var_to_partition (map, lhs);
	      else 
		p1 = NO_PARTITION;

	      if (DECL_P (rhs) || TREE_CODE (rhs) == SSA_NAME)
		p2 = var_to_partition (map, rhs);
	      else 
		p2 = NO_PARTITION;

	      if (p1 != NO_PARTITION && p2 != NO_PARTITION)
		{
		  is_a_copy = true;
		  bit = bitmap_bit_p (live, p2);
		  /* If the RHS is live, make it not live while we add
		     the conflicts, then make it live again.  */
		  if (bit)
		    bitmap_clear_bit (live, p2);
		  add_conflicts_if_valid (tpa, graph, map, live, lhs);
		  if (bit)
		    bitmap_set_bit (live, p2);
		  if (cl)
		    add_coalesce (cl, p1, p2,
				  coalesce_cost (bb->frequency,
				                 maybe_hot_bb_p (bb), false));
		  set_if_valid (map, live, rhs);
		}
	    }

	  if (!is_a_copy)
	    {
	      tree var;
	      FOR_EACH_SSA_TREE_OPERAND (var, stmt, iter, SSA_OP_DEF)
		{
		  add_conflicts_if_valid (tpa, graph, map, live, var);
		}

	      FOR_EACH_SSA_TREE_OPERAND (var, stmt, iter, SSA_OP_USE)
		{
		  set_if_valid (map, live, var);
		}
	    }
	}

      /* If result of a PHI is unused, then the loops over the statements
	 will not record any conflicts.  However, since the PHI node is 
	 going to be translated out of SSA form we must record a conflict
	 between the result of the PHI and any variables with are live. 
	 Otherwise the out-of-ssa translation may create incorrect code.  */
      for (phi = phi_nodes (bb); phi; phi = PHI_CHAIN (phi))
	{
	  tree result = PHI_RESULT (phi);
	  int p = var_to_partition (map, result);

	  if (p != NO_PARTITION && ! bitmap_bit_p (live, p))
	    add_conflicts_if_valid (tpa, graph, map, live, result);
	}

      /* Anything which is still live at this point interferes.  
	 In order to implement this efficiently, only conflicts between
	 partitions which have the same TPA root need be added.
	 TPA roots which have been seen are tracked in 'tpa_nodes'.  A nonzero
	 entry points to an index into 'partition_link', which then indexes 
	 into itself forming a linked list of partitions sharing a tpa root 
	 which have been seen as live up to this point.  Since partitions start
	 at index zero, all entries in partition_link are (partition + 1).

	 Conflicts are added between the current partition and any already seen.
	 tpa_clear contains all the tpa_roots processed, and these are the only
	 entries which need to be zero'd out for a clean restart.  */

      EXECUTE_IF_SET_IN_BITMAP (live, 0, x, bi)
        {
	  i = tpa_find_tree (tpa, x);
	  if (i != (unsigned)TPA_NONE)
	    {
	      int start = tpa_nodes[i];
	      /* If start is 0, a new root reference list is being started.
		 Register it to be cleared.  */
	      if (!start)
		VEC_safe_push (int, heap, tpa_to_clear, i);

	      /* Add interferences to other tpa members seen.  */
	      for (y = start; y != 0; y = partition_link[y])
		conflict_graph_add (graph, x, y - 1);
	      tpa_nodes[i] = x + 1;
	      partition_link[x + 1] = start;
	    }
	}

	/* Now clear the used tpa root references.  */
	for (l = 0; VEC_iterate (int, tpa_to_clear, l, idx); l++)
	  tpa_nodes[idx] = 0;
	VEC_truncate (int, tpa_to_clear, 0);
    }

  free (tpa_nodes);
  free (partition_link);
  VEC_free (int, heap, tpa_to_clear);
  BITMAP_FREE (live);
  return graph;
}


/* This routine will attempt to coalesce the elements in TPA subject to the
   conflicts found in GRAPH.  If optional coalesce_list CL is provided, 
   only coalesces specified within the coalesce list are attempted.  Otherwise 
   an attempt is made to coalesce as many partitions within each TPA grouping 
   as possible.  If DEBUG is provided, debug output will be sent there.  */

void
coalesce_tpa_members (tpa_p tpa, conflict_graph graph, var_map map, 
		      coalesce_list_p cl, FILE *debug)
{
  int x, y, z, w;
  tree var, tmp;

  /* Attempt to coalesce any items in a coalesce list.  */
  if (cl)
    {
      while (pop_best_coalesce (cl, &x, &y) != NO_BEST_COALESCE)
        {
	  if (debug)
	    {
	      fprintf (debug, "Coalesce list: (%d)", x);
	      print_generic_expr (debug, partition_to_var (map, x), TDF_SLIM);
	      fprintf (debug, " & (%d)", y);
	      print_generic_expr (debug, partition_to_var (map, y), TDF_SLIM);
	    }

	  w = tpa_find_tree (tpa, x);
	  z = tpa_find_tree (tpa, y);
	  if (w != z || w == TPA_NONE || z == TPA_NONE)
	    {
	      if (debug)
		{
		  if (w != z)
		    fprintf (debug, ": Fail, Non-matching TPA's\n");
		  if (w == TPA_NONE)
		    fprintf (debug, ": Fail %d non TPA.\n", x);
		  else
		    fprintf (debug, ": Fail %d non TPA.\n", y);
		}
	      continue;
	    }
	  var = partition_to_var (map, x);
	  tmp = partition_to_var (map, y);
	  x = var_to_partition (map, var);
	  y = var_to_partition (map, tmp);
	  if (debug)
	    fprintf (debug, " [map: %d, %d] ", x, y);
	  if (x == y)
	    {
	      if (debug)
		fprintf (debug, ": Already Coalesced.\n");
	      continue;
	    }
	  if (!conflict_graph_conflict_p (graph, x, y))
	    {
	      z = var_union (map, var, tmp);
	      if (z == NO_PARTITION)
	        {
		  if (debug)
		    fprintf (debug, ": Unable to perform partition union.\n");
		  continue;
		}

	      /* z is the new combined partition. We need to remove the other
	         partition from the list. Set x to be that other partition.  */
	      if (z == x)
	        {
		  conflict_graph_merge_regs (graph, x, y);
		  w = tpa_find_tree (tpa, y);
		  tpa_remove_partition (tpa, w, y);
		}
	      else
	        {
		  conflict_graph_merge_regs (graph, y, x);
		  w = tpa_find_tree (tpa, x);
		  tpa_remove_partition (tpa, w, x);
		}

	      if (debug)
		fprintf (debug, ": Success -> %d\n", z);
	    }
	  else
	    if (debug)
	      fprintf (debug, ": Fail due to conflict\n");
	}
      /* If using a coalesce list, don't try to coalesce anything else.  */
      return;
    }

  for (x = 0; x < tpa_num_trees (tpa); x++)
    {
      while (tpa_first_partition (tpa, x) != TPA_NONE)
        {
	  int p1, p2;
	  /* Coalesce first partition with anything that doesn't conflict.  */
	  y = tpa_first_partition (tpa, x);
	  tpa_remove_partition (tpa, x, y);

	  var = partition_to_var (map, y);
	  /* p1 is the partition representative to which y belongs.  */
	  p1 = var_to_partition (map, var);
	  
	  for (z = tpa_next_partition (tpa, y); 
	       z != TPA_NONE; 
	       z = tpa_next_partition (tpa, z))
	    {
	      tmp = partition_to_var (map, z);
	      /* p2 is the partition representative to which z belongs.  */
	      p2 = var_to_partition (map, tmp);
	      if (debug)
		{
		  fprintf (debug, "Coalesce : ");
		  print_generic_expr (debug, var, TDF_SLIM);
		  fprintf (debug, " &");
		  print_generic_expr (debug, tmp, TDF_SLIM);
		  fprintf (debug, "  (%d ,%d)", p1, p2);
		}

	      /* If partitions are already merged, don't check for conflict.  */
	      if (tmp == var)
	        {
		  tpa_remove_partition (tpa, x, z);
		  if (debug)
		    fprintf (debug, ": Already coalesced\n");
		}
	      else
		if (!conflict_graph_conflict_p (graph, p1, p2))
		  {
		    int v;
		    if (tpa_find_tree (tpa, y) == TPA_NONE 
			|| tpa_find_tree (tpa, z) == TPA_NONE)
		      {
			if (debug)
			  fprintf (debug, ": Fail non-TPA member\n");
			continue;
		      }
		    if ((v = var_union (map, var, tmp)) == NO_PARTITION)
		      {
			if (debug)
			  fprintf (debug, ": Fail cannot combine partitions\n");
			continue;
		      }

		    tpa_remove_partition (tpa, x, z);
		    if (v == p1)
		      conflict_graph_merge_regs (graph, v, z);
		    else
		      {
			/* Update the first partition's representative.  */
			conflict_graph_merge_regs (graph, v, y);
			p1 = v;
		      }

		    /* The root variable of the partition may be changed
		       now.  */
		    var = partition_to_var (map, p1);

		    if (debug)
		      fprintf (debug, ": Success -> %d\n", v);
		  }
		else
		  if (debug)
		    fprintf (debug, ": Fail, Conflict\n");
	    }
	}
    }
}


/* Send debug info for coalesce list CL to file F.  */

void 
dump_coalesce_list (FILE *f, coalesce_list_p cl)
{
  partition_pair_p node;
  int x, num;
  tree var;

  if (cl->add_mode)
    {
      fprintf (f, "Coalesce List:\n");
      num = num_var_partitions (cl->map);
      for (x = 0; x < num; x++)
        {
	  node = cl->list[x];
	  if (node)
	    {
	      fprintf (f, "[");
	      print_generic_expr (f, partition_to_var (cl->map, x), TDF_SLIM);
	      fprintf (f, "] - ");
	      for ( ; node; node = node->next)
	        {
		  var = partition_to_var (cl->map, node->second_partition);
		  print_generic_expr (f, var, TDF_SLIM);
		  fprintf (f, "(%1d), ", node->cost);
		}
	      fprintf (f, "\n");
	    }
	}
    }
  else
    {
      fprintf (f, "Sorted Coalesce list:\n");
      for (node = cl->list[0]; node; node = node->next)
        {
	  fprintf (f, "(%d) ", node->cost);
	  var = partition_to_var (cl->map, node->first_partition);
	  print_generic_expr (f, var, TDF_SLIM);
	  fprintf (f, " : ");
	  var = partition_to_var (cl->map, node->second_partition);
	  print_generic_expr (f, var, TDF_SLIM);
	  fprintf (f, "\n");
	}
    }
}


/* Output tree_partition_associator object TPA to file F..  */

void
tpa_dump (FILE *f, tpa_p tpa)
{
  int x, i;

  if (!tpa)
    return;

  for (x = 0; x < tpa_num_trees (tpa); x++)
    {
      print_generic_expr (f, tpa_tree (tpa, x), TDF_SLIM);
      fprintf (f, " : (");
      for (i = tpa_first_partition (tpa, x); 
	   i != TPA_NONE;
	   i = tpa_next_partition (tpa, i))
	{
	  fprintf (f, "(%d)",i);
	  print_generic_expr (f, partition_to_var (tpa->map, i), TDF_SLIM);
	  fprintf (f, " ");

#ifdef ENABLE_CHECKING
	  if (tpa_find_tree (tpa, i) != x)
	    fprintf (f, "**find tree incorrectly set** ");
#endif

	}
      fprintf (f, ")\n");
    }
  fflush (f);
}


/* Output partition map MAP to file F.  */

void
dump_var_map (FILE *f, var_map map)
{
  int t;
  unsigned x, y;
  int p;

  fprintf (f, "\nPartition map \n\n");

  for (x = 0; x < map->num_partitions; x++)
    {
      if (map->compact_to_partition != NULL)
	p = map->compact_to_partition[x];
      else
	p = x;

      if (map->partition_to_var[p] == NULL_TREE)
        continue;

      t = 0;
      for (y = 1; y < num_ssa_names; y++)
        {
	  p = partition_find (map->var_partition, y);
	  if (map->partition_to_compact)
	    p = map->partition_to_compact[p];
	  if (p == (int)x)
	    {
	      if (t++ == 0)
	        {
		  fprintf(f, "Partition %d (", x);
		  print_generic_expr (f, partition_to_var (map, p), TDF_SLIM);
		  fprintf (f, " - ");
		}
	      fprintf (f, "%d ", y);
	    }
	}
      if (t != 0)
	fprintf (f, ")\n");
    }
  fprintf (f, "\n");
}


/* Output live range info LIVE to file F, controlled by FLAG.  */

void
dump_live_info (FILE *f, tree_live_info_p live, int flag)
{
  basic_block bb;
  unsigned i;
  var_map map = live->map;
  bitmap_iterator bi;

  if ((flag & LIVEDUMP_ENTRY) && live->livein)
    {
      FOR_EACH_BB (bb)
	{
	  fprintf (f, "\nLive on entry to BB%d : ", bb->index);
	  for (i = 0; i < num_var_partitions (map); i++)
	    {
	      if (bitmap_bit_p (live_entry_blocks (live, i), bb->index))
	        {
		  print_generic_expr (f, partition_to_var (map, i), TDF_SLIM);
		  fprintf (f, "  ");
		}
	    }
	  fprintf (f, "\n");
	}
    }

  if ((flag & LIVEDUMP_EXIT) && live->liveout)
    {
      FOR_EACH_BB (bb)
	{
	  fprintf (f, "\nLive on exit from BB%d : ", bb->index);
	  EXECUTE_IF_SET_IN_BITMAP (live->liveout[bb->index], 0, i, bi)
	    {
	      print_generic_expr (f, partition_to_var (map, i), TDF_SLIM);
	      fprintf (f, "  ");
	    }
	  fprintf (f, "\n");
	}
    }
}

#ifdef ENABLE_CHECKING
void
register_ssa_partition_check (tree ssa_var)
{
  gcc_assert (TREE_CODE (ssa_var) == SSA_NAME);
  if (!is_gimple_reg (SSA_NAME_VAR (ssa_var)))
    {
      fprintf (stderr, "Illegally registering a virtual SSA name :");
      print_generic_expr (stderr, ssa_var, TDF_SLIM);
      fprintf (stderr, " in the SSA->Normal phase.\n");
      internal_error ("SSA corruption");
    }
}
#endif
