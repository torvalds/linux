/* Control flow functions for trees.
   Copyright (C) 2001, 2002, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.
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
#include "rtl.h"
#include "tm_p.h"
#include "hard-reg-set.h"
#include "basic-block.h"
#include "output.h"
#include "flags.h"
#include "function.h"
#include "expr.h"
#include "ggc.h"
#include "langhooks.h"
#include "diagnostic.h"
#include "tree-flow.h"
#include "timevar.h"
#include "tree-dump.h"
#include "tree-pass.h"
#include "toplev.h"
#include "except.h"
#include "cfgloop.h"
#include "cfglayout.h"
#include "hashtab.h"
#include "tree-ssa-propagate.h"

/* This file contains functions for building the Control Flow Graph (CFG)
   for a function tree.  */

/* Local declarations.  */

/* Initial capacity for the basic block array.  */
static const int initial_cfg_capacity = 20;

/* This hash table allows us to efficiently lookup all CASE_LABEL_EXPRs
   which use a particular edge.  The CASE_LABEL_EXPRs are chained together
   via their TREE_CHAIN field, which we clear after we're done with the
   hash table to prevent problems with duplication of SWITCH_EXPRs.

   Access to this list of CASE_LABEL_EXPRs allows us to efficiently
   update the case vector in response to edge redirections.

   Right now this table is set up and torn down at key points in the
   compilation process.  It would be nice if we could make the table
   more persistent.  The key is getting notification of changes to
   the CFG (particularly edge removal, creation and redirection).  */

struct edge_to_cases_elt
{
  /* The edge itself.  Necessary for hashing and equality tests.  */
  edge e;

  /* The case labels associated with this edge.  We link these up via
     their TREE_CHAIN field, then we wipe out the TREE_CHAIN fields
     when we destroy the hash table.  This prevents problems when copying
     SWITCH_EXPRs.  */
  tree case_labels;
};

static htab_t edge_to_cases;

/* CFG statistics.  */
struct cfg_stats_d
{
  long num_merged_labels;
};

static struct cfg_stats_d cfg_stats;

/* Nonzero if we found a computed goto while building basic blocks.  */
static bool found_computed_goto;

/* Basic blocks and flowgraphs.  */
static basic_block create_bb (void *, void *, basic_block);
static void make_blocks (tree);
static void factor_computed_gotos (void);

/* Edges.  */
static void make_edges (void);
static void make_cond_expr_edges (basic_block);
static void make_switch_expr_edges (basic_block);
static void make_goto_expr_edges (basic_block);
static edge tree_redirect_edge_and_branch (edge, basic_block);
static edge tree_try_redirect_by_replacing_jump (edge, basic_block);
static unsigned int split_critical_edges (void);

/* Various helpers.  */
static inline bool stmt_starts_bb_p (tree, tree);
static int tree_verify_flow_info (void);
static void tree_make_forwarder_block (edge);
static void tree_cfg2vcg (FILE *);
static inline void change_bb_for_stmt (tree t, basic_block bb);

/* Flowgraph optimization and cleanup.  */
static void tree_merge_blocks (basic_block, basic_block);
static bool tree_can_merge_blocks_p (basic_block, basic_block);
static void remove_bb (basic_block);
static edge find_taken_edge_computed_goto (basic_block, tree);
static edge find_taken_edge_cond_expr (basic_block, tree);
static edge find_taken_edge_switch_expr (basic_block, tree);
static tree find_case_label_for_value (tree, tree);

void
init_empty_tree_cfg (void)
{
  /* Initialize the basic block array.  */
  init_flow ();
  profile_status = PROFILE_ABSENT;
  n_basic_blocks = NUM_FIXED_BLOCKS;
  last_basic_block = NUM_FIXED_BLOCKS;
  basic_block_info = VEC_alloc (basic_block, gc, initial_cfg_capacity);
  VEC_safe_grow (basic_block, gc, basic_block_info, initial_cfg_capacity);
  memset (VEC_address (basic_block, basic_block_info), 0,
	  sizeof (basic_block) * initial_cfg_capacity);

  /* Build a mapping of labels to their associated blocks.  */
  label_to_block_map = VEC_alloc (basic_block, gc, initial_cfg_capacity);
  VEC_safe_grow (basic_block, gc, label_to_block_map, initial_cfg_capacity);
  memset (VEC_address (basic_block, label_to_block_map),
	  0, sizeof (basic_block) * initial_cfg_capacity);

  SET_BASIC_BLOCK (ENTRY_BLOCK, ENTRY_BLOCK_PTR);
  SET_BASIC_BLOCK (EXIT_BLOCK, EXIT_BLOCK_PTR);
  ENTRY_BLOCK_PTR->next_bb = EXIT_BLOCK_PTR;
  EXIT_BLOCK_PTR->prev_bb = ENTRY_BLOCK_PTR;
}

/*---------------------------------------------------------------------------
			      Create basic blocks
---------------------------------------------------------------------------*/

/* Entry point to the CFG builder for trees.  TP points to the list of
   statements to be added to the flowgraph.  */

static void
build_tree_cfg (tree *tp)
{
  /* Register specific tree functions.  */
  tree_register_cfg_hooks ();

  memset ((void *) &cfg_stats, 0, sizeof (cfg_stats));

  init_empty_tree_cfg ();

  found_computed_goto = 0;
  make_blocks (*tp);

  /* Computed gotos are hell to deal with, especially if there are
     lots of them with a large number of destinations.  So we factor
     them to a common computed goto location before we build the
     edge list.  After we convert back to normal form, we will un-factor
     the computed gotos since factoring introduces an unwanted jump.  */
  if (found_computed_goto)
    factor_computed_gotos ();

  /* Make sure there is always at least one block, even if it's empty.  */
  if (n_basic_blocks == NUM_FIXED_BLOCKS)
    create_empty_bb (ENTRY_BLOCK_PTR);

  /* Adjust the size of the array.  */
  if (VEC_length (basic_block, basic_block_info) < (size_t) n_basic_blocks)
    {
      size_t old_size = VEC_length (basic_block, basic_block_info);
      basic_block *p;
      VEC_safe_grow (basic_block, gc, basic_block_info, n_basic_blocks);
      p = VEC_address (basic_block, basic_block_info);
      memset (&p[old_size], 0,
	      sizeof (basic_block) * (n_basic_blocks - old_size));
    }

  /* To speed up statement iterator walks, we first purge dead labels.  */
  cleanup_dead_labels ();

  /* Group case nodes to reduce the number of edges.
     We do this after cleaning up dead labels because otherwise we miss
     a lot of obvious case merging opportunities.  */
  group_case_labels ();

  /* Create the edges of the flowgraph.  */
  make_edges ();

  /* Debugging dumps.  */

  /* Write the flowgraph to a VCG file.  */
  {
    int local_dump_flags;
    FILE *vcg_file = dump_begin (TDI_vcg, &local_dump_flags);
    if (vcg_file)
      {
	tree_cfg2vcg (vcg_file);
	dump_end (TDI_vcg, vcg_file);
      }
  }

#ifdef ENABLE_CHECKING
  verify_stmts ();
#endif

  /* Dump a textual representation of the flowgraph.  */
  if (dump_file)
    dump_tree_cfg (dump_file, dump_flags);
}

static unsigned int
execute_build_cfg (void)
{
  build_tree_cfg (&DECL_SAVED_TREE (current_function_decl));
  return 0;
}

struct tree_opt_pass pass_build_cfg =
{
  "cfg",				/* name */
  NULL,					/* gate */
  execute_build_cfg,			/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  TV_TREE_CFG,				/* tv_id */
  PROP_gimple_leh,			/* properties_required */
  PROP_cfg,				/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  TODO_verify_stmts,			/* todo_flags_finish */
  0					/* letter */
};

/* Search the CFG for any computed gotos.  If found, factor them to a
   common computed goto site.  Also record the location of that site so
   that we can un-factor the gotos after we have converted back to
   normal form.  */

static void
factor_computed_gotos (void)
{
  basic_block bb;
  tree factored_label_decl = NULL;
  tree var = NULL;
  tree factored_computed_goto_label = NULL;
  tree factored_computed_goto = NULL;

  /* We know there are one or more computed gotos in this function.
     Examine the last statement in each basic block to see if the block
     ends with a computed goto.  */

  FOR_EACH_BB (bb)
    {
      block_stmt_iterator bsi = bsi_last (bb);
      tree last;

      if (bsi_end_p (bsi))
	continue;
      last = bsi_stmt (bsi);

      /* Ignore the computed goto we create when we factor the original
	 computed gotos.  */
      if (last == factored_computed_goto)
	continue;

      /* If the last statement is a computed goto, factor it.  */
      if (computed_goto_p (last))
	{
	  tree assignment;

	  /* The first time we find a computed goto we need to create
	     the factored goto block and the variable each original
	     computed goto will use for their goto destination.  */
	  if (! factored_computed_goto)
	    {
	      basic_block new_bb = create_empty_bb (bb);
	      block_stmt_iterator new_bsi = bsi_start (new_bb);

	      /* Create the destination of the factored goto.  Each original
		 computed goto will put its desired destination into this
		 variable and jump to the label we create immediately
		 below.  */
	      var = create_tmp_var (ptr_type_node, "gotovar");

	      /* Build a label for the new block which will contain the
		 factored computed goto.  */
	      factored_label_decl = create_artificial_label ();
	      factored_computed_goto_label
		= build1 (LABEL_EXPR, void_type_node, factored_label_decl);
	      bsi_insert_after (&new_bsi, factored_computed_goto_label,
				BSI_NEW_STMT);

	      /* Build our new computed goto.  */
	      factored_computed_goto = build1 (GOTO_EXPR, void_type_node, var);
	      bsi_insert_after (&new_bsi, factored_computed_goto,
				BSI_NEW_STMT);
	    }

	  /* Copy the original computed goto's destination into VAR.  */
	  assignment = build2 (MODIFY_EXPR, ptr_type_node,
			       var, GOTO_DESTINATION (last));
	  bsi_insert_before (&bsi, assignment, BSI_SAME_STMT);

	  /* And re-vector the computed goto to the new destination.  */
	  GOTO_DESTINATION (last) = factored_label_decl;
	}
    }
}


/* Build a flowgraph for the statement_list STMT_LIST.  */

static void
make_blocks (tree stmt_list)
{
  tree_stmt_iterator i = tsi_start (stmt_list);
  tree stmt = NULL;
  bool start_new_block = true;
  bool first_stmt_of_list = true;
  basic_block bb = ENTRY_BLOCK_PTR;

  while (!tsi_end_p (i))
    {
      tree prev_stmt;

      prev_stmt = stmt;
      stmt = tsi_stmt (i);

      /* If the statement starts a new basic block or if we have determined
	 in a previous pass that we need to create a new block for STMT, do
	 so now.  */
      if (start_new_block || stmt_starts_bb_p (stmt, prev_stmt))
	{
	  if (!first_stmt_of_list)
	    stmt_list = tsi_split_statement_list_before (&i);
	  bb = create_basic_block (stmt_list, NULL, bb);
	  start_new_block = false;
	}

      /* Now add STMT to BB and create the subgraphs for special statement
	 codes.  */
      set_bb_for_stmt (stmt, bb);

      if (computed_goto_p (stmt))
	found_computed_goto = true;

      /* If STMT is a basic block terminator, set START_NEW_BLOCK for the
	 next iteration.  */
      if (stmt_ends_bb_p (stmt))
	start_new_block = true;

      tsi_next (&i);
      first_stmt_of_list = false;
    }
}


/* Create and return a new empty basic block after bb AFTER.  */

static basic_block
create_bb (void *h, void *e, basic_block after)
{
  basic_block bb;

  gcc_assert (!e);

  /* Create and initialize a new basic block.  Since alloc_block uses
     ggc_alloc_cleared to allocate a basic block, we do not have to
     clear the newly allocated basic block here.  */
  bb = alloc_block ();

  bb->index = last_basic_block;
  bb->flags = BB_NEW;
  bb->stmt_list = h ? (tree) h : alloc_stmt_list ();

  /* Add the new block to the linked list of blocks.  */
  link_block (bb, after);

  /* Grow the basic block array if needed.  */
  if ((size_t) last_basic_block == VEC_length (basic_block, basic_block_info))
    {
      size_t old_size = VEC_length (basic_block, basic_block_info);
      size_t new_size = last_basic_block + (last_basic_block + 3) / 4;
      basic_block *p;
      VEC_safe_grow (basic_block, gc, basic_block_info, new_size);
      p = VEC_address (basic_block, basic_block_info);
      memset (&p[old_size], 0, sizeof (basic_block) * (new_size - old_size));
    }

  /* Add the newly created block to the array.  */
  SET_BASIC_BLOCK (last_basic_block, bb);

  n_basic_blocks++;
  last_basic_block++;

  return bb;
}


/*---------------------------------------------------------------------------
				 Edge creation
---------------------------------------------------------------------------*/

/* Fold COND_EXPR_COND of each COND_EXPR.  */

void
fold_cond_expr_cond (void)
{
  basic_block bb;

  FOR_EACH_BB (bb)
    {
      tree stmt = last_stmt (bb);

      if (stmt
	  && TREE_CODE (stmt) == COND_EXPR)
	{
	  tree cond;
	  bool zerop, onep;

	  fold_defer_overflow_warnings ();
	  cond = fold (COND_EXPR_COND (stmt));
	  zerop = integer_zerop (cond);
	  onep = integer_onep (cond);
	  fold_undefer_overflow_warnings (((zerop || onep)
					   && !TREE_NO_WARNING (stmt)),
					  stmt,
					  WARN_STRICT_OVERFLOW_CONDITIONAL);
	  if (zerop)
	    COND_EXPR_COND (stmt) = boolean_false_node;
	  else if (onep)
	    COND_EXPR_COND (stmt) = boolean_true_node;
	}
    }
}

/* Join all the blocks in the flowgraph.  */

static void
make_edges (void)
{
  basic_block bb;
  struct omp_region *cur_region = NULL;

  /* Create an edge from entry to the first block with executable
     statements in it.  */
  make_edge (ENTRY_BLOCK_PTR, BASIC_BLOCK (NUM_FIXED_BLOCKS), EDGE_FALLTHRU);

  /* Traverse the basic block array placing edges.  */
  FOR_EACH_BB (bb)
    {
      tree last = last_stmt (bb);
      bool fallthru;

      if (last)
	{
	  enum tree_code code = TREE_CODE (last);
	  switch (code)
	    {
	    case GOTO_EXPR:
	      make_goto_expr_edges (bb);
	      fallthru = false;
	      break;
	    case RETURN_EXPR:
	      make_edge (bb, EXIT_BLOCK_PTR, 0);
	      fallthru = false;
	      break;
	    case COND_EXPR:
	      make_cond_expr_edges (bb);
	      fallthru = false;
	      break;
	    case SWITCH_EXPR:
	      make_switch_expr_edges (bb);
	      fallthru = false;
	      break;
	    case RESX_EXPR:
	      make_eh_edges (last);
	      fallthru = false;
	      break;

	    case CALL_EXPR:
	      /* If this function receives a nonlocal goto, then we need to
		 make edges from this call site to all the nonlocal goto
		 handlers.  */
	      if (tree_can_make_abnormal_goto (last))
		make_abnormal_goto_edges (bb, true);

	      /* If this statement has reachable exception handlers, then
		 create abnormal edges to them.  */
	      make_eh_edges (last);

	      /* Some calls are known not to return.  */
	      fallthru = !(call_expr_flags (last) & ECF_NORETURN);
	      break;

	    case MODIFY_EXPR:
	      if (is_ctrl_altering_stmt (last))
		{
		  /* A MODIFY_EXPR may have a CALL_EXPR on its RHS and the
		     CALL_EXPR may have an abnormal edge.  Search the RHS for
		     this case and create any required edges.  */
		  if (tree_can_make_abnormal_goto (last))
		    make_abnormal_goto_edges (bb, true);  

		  make_eh_edges (last);
		}
	      fallthru = true;
	      break;

	    case OMP_PARALLEL:
	    case OMP_FOR:
	    case OMP_SINGLE:
	    case OMP_MASTER:
	    case OMP_ORDERED:
	    case OMP_CRITICAL:
	    case OMP_SECTION:
	      cur_region = new_omp_region (bb, code, cur_region);
	      fallthru = true;
	      break;

	    case OMP_SECTIONS:
	      cur_region = new_omp_region (bb, code, cur_region);
	      fallthru = false;
	      break;

	    case OMP_RETURN:
	      /* In the case of an OMP_SECTION, the edge will go somewhere
		 other than the next block.  This will be created later.  */
	      cur_region->exit = bb;
	      fallthru = cur_region->type != OMP_SECTION;
	      cur_region = cur_region->outer;
	      break;

	    case OMP_CONTINUE:
	      cur_region->cont = bb;
	      switch (cur_region->type)
		{
		case OMP_FOR:
		  /* ??? Technically there should be a some sort of loopback
		     edge here, but it goes to a block that doesn't exist yet,
		     and without it, updating the ssa form would be a real
		     bear.  Fortunately, we don't yet do ssa before expanding
		     these nodes.  */
		  break;

		case OMP_SECTIONS:
		  /* Wire up the edges into and out of the nested sections.  */
		  /* ??? Similarly wrt loopback.  */
		  {
		    struct omp_region *i;
		    for (i = cur_region->inner; i ; i = i->next)
		      {
			gcc_assert (i->type == OMP_SECTION);
			make_edge (cur_region->entry, i->entry, 0);
			make_edge (i->exit, bb, EDGE_FALLTHRU);
		      }
		  }
		  break;

		default:
		  gcc_unreachable ();
		}
	      fallthru = true;
	      break;

	    default:
	      gcc_assert (!stmt_ends_bb_p (last));
	      fallthru = true;
	    }
	}
      else
	fallthru = true;

      if (fallthru)
	make_edge (bb, bb->next_bb, EDGE_FALLTHRU);
    }

  if (root_omp_region)
    free_omp_regions ();

  /* Fold COND_EXPR_COND of each COND_EXPR.  */
  fold_cond_expr_cond ();

  /* Clean up the graph and warn for unreachable code.  */
  cleanup_tree_cfg ();
}


/* Create the edges for a COND_EXPR starting at block BB.
   At this point, both clauses must contain only simple gotos.  */

static void
make_cond_expr_edges (basic_block bb)
{
  tree entry = last_stmt (bb);
  basic_block then_bb, else_bb;
  tree then_label, else_label;
  edge e;

  gcc_assert (entry);
  gcc_assert (TREE_CODE (entry) == COND_EXPR);

  /* Entry basic blocks for each component.  */
  then_label = GOTO_DESTINATION (COND_EXPR_THEN (entry));
  else_label = GOTO_DESTINATION (COND_EXPR_ELSE (entry));
  then_bb = label_to_block (then_label);
  else_bb = label_to_block (else_label);

  e = make_edge (bb, then_bb, EDGE_TRUE_VALUE);
#ifdef USE_MAPPED_LOCATION
  e->goto_locus = EXPR_LOCATION (COND_EXPR_THEN (entry));
#else
  e->goto_locus = EXPR_LOCUS (COND_EXPR_THEN (entry));
#endif
  e = make_edge (bb, else_bb, EDGE_FALSE_VALUE);
  if (e)
    {
#ifdef USE_MAPPED_LOCATION
      e->goto_locus = EXPR_LOCATION (COND_EXPR_ELSE (entry));
#else
      e->goto_locus = EXPR_LOCUS (COND_EXPR_ELSE (entry));
#endif
    }
}

/* Hashing routine for EDGE_TO_CASES.  */

static hashval_t
edge_to_cases_hash (const void *p)
{
  edge e = ((struct edge_to_cases_elt *)p)->e;

  /* Hash on the edge itself (which is a pointer).  */
  return htab_hash_pointer (e);
}

/* Equality routine for EDGE_TO_CASES, edges are unique, so testing
   for equality is just a pointer comparison.  */

static int
edge_to_cases_eq (const void *p1, const void *p2)
{
  edge e1 = ((struct edge_to_cases_elt *)p1)->e;
  edge e2 = ((struct edge_to_cases_elt *)p2)->e;

  return e1 == e2;
}

/* Called for each element in the hash table (P) as we delete the
   edge to cases hash table.

   Clear all the TREE_CHAINs to prevent problems with copying of
   SWITCH_EXPRs and structure sharing rules, then free the hash table
   element.  */

static void
edge_to_cases_cleanup (void *p)
{
  struct edge_to_cases_elt *elt = (struct edge_to_cases_elt *) p;
  tree t, next;

  for (t = elt->case_labels; t; t = next)
    {
      next = TREE_CHAIN (t);
      TREE_CHAIN (t) = NULL;
    }
  free (p);
}

/* Start recording information mapping edges to case labels.  */

void
start_recording_case_labels (void)
{
  gcc_assert (edge_to_cases == NULL);

  edge_to_cases = htab_create (37,
			       edge_to_cases_hash,
			       edge_to_cases_eq,
			       edge_to_cases_cleanup);
}

/* Return nonzero if we are recording information for case labels.  */

static bool
recording_case_labels_p (void)
{
  return (edge_to_cases != NULL);
}

/* Stop recording information mapping edges to case labels and
   remove any information we have recorded.  */
void
end_recording_case_labels (void)
{
  htab_delete (edge_to_cases);
  edge_to_cases = NULL;
}

/* Record that CASE_LABEL (a CASE_LABEL_EXPR) references edge E.  */

static void
record_switch_edge (edge e, tree case_label)
{
  struct edge_to_cases_elt *elt;
  void **slot;

  /* Build a hash table element so we can see if E is already
     in the table.  */
  elt = XNEW (struct edge_to_cases_elt);
  elt->e = e;
  elt->case_labels = case_label;

  slot = htab_find_slot (edge_to_cases, elt, INSERT);

  if (*slot == NULL)
    {
      /* E was not in the hash table.  Install E into the hash table.  */
      *slot = (void *)elt;
    }
  else
    {
      /* E was already in the hash table.  Free ELT as we do not need it
	 anymore.  */
      free (elt);

      /* Get the entry stored in the hash table.  */
      elt = (struct edge_to_cases_elt *) *slot;

      /* Add it to the chain of CASE_LABEL_EXPRs referencing E.  */
      TREE_CHAIN (case_label) = elt->case_labels;
      elt->case_labels = case_label;
    }
}

/* If we are inside a {start,end}_recording_cases block, then return
   a chain of CASE_LABEL_EXPRs from T which reference E.

   Otherwise return NULL.  */

static tree
get_cases_for_edge (edge e, tree t)
{
  struct edge_to_cases_elt elt, *elt_p;
  void **slot;
  size_t i, n;
  tree vec;

  /* If we are not recording cases, then we do not have CASE_LABEL_EXPR
     chains available.  Return NULL so the caller can detect this case.  */
  if (!recording_case_labels_p ())
    return NULL;

restart:
  elt.e = e;
  elt.case_labels = NULL;
  slot = htab_find_slot (edge_to_cases, &elt, NO_INSERT);

  if (slot)
    {
      elt_p = (struct edge_to_cases_elt *)*slot;
      return elt_p->case_labels;
    }

  /* If we did not find E in the hash table, then this must be the first
     time we have been queried for information about E & T.  Add all the
     elements from T to the hash table then perform the query again.  */

  vec = SWITCH_LABELS (t);
  n = TREE_VEC_LENGTH (vec);
  for (i = 0; i < n; i++)
    {
      tree lab = CASE_LABEL (TREE_VEC_ELT (vec, i));
      basic_block label_bb = label_to_block (lab);
      record_switch_edge (find_edge (e->src, label_bb), TREE_VEC_ELT (vec, i));
    }
  goto restart;
}

/* Create the edges for a SWITCH_EXPR starting at block BB.
   At this point, the switch body has been lowered and the
   SWITCH_LABELS filled in, so this is in effect a multi-way branch.  */

static void
make_switch_expr_edges (basic_block bb)
{
  tree entry = last_stmt (bb);
  size_t i, n;
  tree vec;

  vec = SWITCH_LABELS (entry);
  n = TREE_VEC_LENGTH (vec);

  for (i = 0; i < n; ++i)
    {
      tree lab = CASE_LABEL (TREE_VEC_ELT (vec, i));
      basic_block label_bb = label_to_block (lab);
      make_edge (bb, label_bb, 0);
    }
}


/* Return the basic block holding label DEST.  */

basic_block
label_to_block_fn (struct function *ifun, tree dest)
{
  int uid = LABEL_DECL_UID (dest);

  /* We would die hard when faced by an undefined label.  Emit a label to
     the very first basic block.  This will hopefully make even the dataflow
     and undefined variable warnings quite right.  */
  if ((errorcount || sorrycount) && uid < 0)
    {
      block_stmt_iterator bsi =
	bsi_start (BASIC_BLOCK (NUM_FIXED_BLOCKS));
      tree stmt;

      stmt = build1 (LABEL_EXPR, void_type_node, dest);
      bsi_insert_before (&bsi, stmt, BSI_NEW_STMT);
      uid = LABEL_DECL_UID (dest);
    }
  if (VEC_length (basic_block, ifun->cfg->x_label_to_block_map)
      <= (unsigned int) uid)
    return NULL;
  return VEC_index (basic_block, ifun->cfg->x_label_to_block_map, uid);
}

/* Create edges for an abnormal goto statement at block BB.  If FOR_CALL
   is true, the source statement is a CALL_EXPR instead of a GOTO_EXPR.  */

void
make_abnormal_goto_edges (basic_block bb, bool for_call)
{
  basic_block target_bb;
  block_stmt_iterator bsi;

  FOR_EACH_BB (target_bb)
    for (bsi = bsi_start (target_bb); !bsi_end_p (bsi); bsi_next (&bsi))
      {
	tree target = bsi_stmt (bsi);

	if (TREE_CODE (target) != LABEL_EXPR)
	  break;

	target = LABEL_EXPR_LABEL (target);

	/* Make an edge to every label block that has been marked as a
	   potential target for a computed goto or a non-local goto.  */
	if ((FORCED_LABEL (target) && !for_call)
	    || (DECL_NONLOCAL (target) && for_call))
	  {
	    make_edge (bb, target_bb, EDGE_ABNORMAL);
	    break;
	  }
      }
}

/* Create edges for a goto statement at block BB.  */

static void
make_goto_expr_edges (basic_block bb)
{
  block_stmt_iterator last = bsi_last (bb);
  tree goto_t = bsi_stmt (last);

  /* A simple GOTO creates normal edges.  */
  if (simple_goto_p (goto_t))
    {
      tree dest = GOTO_DESTINATION (goto_t);
      edge e = make_edge (bb, label_to_block (dest), EDGE_FALLTHRU);
#ifdef USE_MAPPED_LOCATION
      e->goto_locus = EXPR_LOCATION (goto_t);
#else
      e->goto_locus = EXPR_LOCUS (goto_t);
#endif
      bsi_remove (&last, true);
      return;
    }

  /* A computed GOTO creates abnormal edges.  */
  make_abnormal_goto_edges (bb, false);
}


/*---------------------------------------------------------------------------
			       Flowgraph analysis
---------------------------------------------------------------------------*/

/* Cleanup useless labels in basic blocks.  This is something we wish
   to do early because it allows us to group case labels before creating
   the edges for the CFG, and it speeds up block statement iterators in
   all passes later on.
   We only run this pass once, running it more than once is probably not
   profitable.  */

/* A map from basic block index to the leading label of that block.  */
static tree *label_for_bb;

/* Callback for for_each_eh_region.  Helper for cleanup_dead_labels.  */
static void
update_eh_label (struct eh_region *region)
{
  tree old_label = get_eh_region_tree_label (region);
  if (old_label)
    {
      tree new_label;
      basic_block bb = label_to_block (old_label);

      /* ??? After optimizing, there may be EH regions with labels
	 that have already been removed from the function body, so
	 there is no basic block for them.  */
      if (! bb)
	return;

      new_label = label_for_bb[bb->index];
      set_eh_region_tree_label (region, new_label);
    }
}

/* Given LABEL return the first label in the same basic block.  */
static tree
main_block_label (tree label)
{
  basic_block bb = label_to_block (label);

  /* label_to_block possibly inserted undefined label into the chain.  */
  if (!label_for_bb[bb->index])
    label_for_bb[bb->index] = label;
  return label_for_bb[bb->index];
}

/* Cleanup redundant labels.  This is a three-step process:
     1) Find the leading label for each block.
     2) Redirect all references to labels to the leading labels.
     3) Cleanup all useless labels.  */

void
cleanup_dead_labels (void)
{
  basic_block bb;
  label_for_bb = XCNEWVEC (tree, last_basic_block);

  /* Find a suitable label for each block.  We use the first user-defined
     label if there is one, or otherwise just the first label we see.  */
  FOR_EACH_BB (bb)
    {
      block_stmt_iterator i;

      for (i = bsi_start (bb); !bsi_end_p (i); bsi_next (&i))
	{
	  tree label, stmt = bsi_stmt (i);

	  if (TREE_CODE (stmt) != LABEL_EXPR)
	    break;

	  label = LABEL_EXPR_LABEL (stmt);

	  /* If we have not yet seen a label for the current block,
	     remember this one and see if there are more labels.  */
	  if (! label_for_bb[bb->index])
	    {
	      label_for_bb[bb->index] = label;
	      continue;
	    }

	  /* If we did see a label for the current block already, but it
	     is an artificially created label, replace it if the current
	     label is a user defined label.  */
	  if (! DECL_ARTIFICIAL (label)
	      && DECL_ARTIFICIAL (label_for_bb[bb->index]))
	    {
	      label_for_bb[bb->index] = label;
	      break;
	    }
	}
    }

  /* Now redirect all jumps/branches to the selected label.
     First do so for each block ending in a control statement.  */
  FOR_EACH_BB (bb)
    {
      tree stmt = last_stmt (bb);
      if (!stmt)
	continue;

      switch (TREE_CODE (stmt))
	{
	case COND_EXPR:
	  {
	    tree true_branch, false_branch;

	    true_branch = COND_EXPR_THEN (stmt);
	    false_branch = COND_EXPR_ELSE (stmt);

	    GOTO_DESTINATION (true_branch)
	      = main_block_label (GOTO_DESTINATION (true_branch));
	    GOTO_DESTINATION (false_branch)
	      = main_block_label (GOTO_DESTINATION (false_branch));

	    break;
	  }

	case SWITCH_EXPR:
	  {
	    size_t i;
	    tree vec = SWITCH_LABELS (stmt);
	    size_t n = TREE_VEC_LENGTH (vec);

	    /* Replace all destination labels.  */
	    for (i = 0; i < n; ++i)
	      {
		tree elt = TREE_VEC_ELT (vec, i);
		tree label = main_block_label (CASE_LABEL (elt));
		CASE_LABEL (elt) = label;
	      }
	    break;
	  }

	/* We have to handle GOTO_EXPRs until they're removed, and we don't
	   remove them until after we've created the CFG edges.  */
	case GOTO_EXPR:
          if (! computed_goto_p (stmt))
	    {
	      GOTO_DESTINATION (stmt)
		= main_block_label (GOTO_DESTINATION (stmt));
	      break;
	    }

	default:
	  break;
      }
    }

  for_each_eh_region (update_eh_label);

  /* Finally, purge dead labels.  All user-defined labels and labels that
     can be the target of non-local gotos and labels which have their
     address taken are preserved.  */
  FOR_EACH_BB (bb)
    {
      block_stmt_iterator i;
      tree label_for_this_bb = label_for_bb[bb->index];

      if (! label_for_this_bb)
	continue;

      for (i = bsi_start (bb); !bsi_end_p (i); )
	{
	  tree label, stmt = bsi_stmt (i);

	  if (TREE_CODE (stmt) != LABEL_EXPR)
	    break;

	  label = LABEL_EXPR_LABEL (stmt);

	  if (label == label_for_this_bb
	      || ! DECL_ARTIFICIAL (label)
	      || DECL_NONLOCAL (label)
	      || FORCED_LABEL (label))
	    bsi_next (&i);
	  else
	    bsi_remove (&i, true);
	}
    }

  free (label_for_bb);
}

/* Look for blocks ending in a multiway branch (a SWITCH_EXPR in GIMPLE),
   and scan the sorted vector of cases.  Combine the ones jumping to the
   same label.
   Eg. three separate entries 1: 2: 3: become one entry 1..3:  */

void
group_case_labels (void)
{
  basic_block bb;

  FOR_EACH_BB (bb)
    {
      tree stmt = last_stmt (bb);
      if (stmt && TREE_CODE (stmt) == SWITCH_EXPR)
	{
	  tree labels = SWITCH_LABELS (stmt);
	  int old_size = TREE_VEC_LENGTH (labels);
	  int i, j, new_size = old_size;
	  tree default_case = TREE_VEC_ELT (labels, old_size - 1);
	  tree default_label;

	  /* The default label is always the last case in a switch
	     statement after gimplification.  */
	  default_label = CASE_LABEL (default_case);

	  /* Look for possible opportunities to merge cases.
	     Ignore the last element of the label vector because it
	     must be the default case.  */
          i = 0;
	  while (i < old_size - 1)
	    {
	      tree base_case, base_label, base_high;
	      base_case = TREE_VEC_ELT (labels, i);

	      gcc_assert (base_case);
	      base_label = CASE_LABEL (base_case);

	      /* Discard cases that have the same destination as the
		 default case.  */
	      if (base_label == default_label)
		{
		  TREE_VEC_ELT (labels, i) = NULL_TREE;
		  i++;
		  new_size--;
		  continue;
		}

	      base_high = CASE_HIGH (base_case) ?
		CASE_HIGH (base_case) : CASE_LOW (base_case);
	      i++;
	      /* Try to merge case labels.  Break out when we reach the end
		 of the label vector or when we cannot merge the next case
		 label with the current one.  */
	      while (i < old_size - 1)
		{
		  tree merge_case = TREE_VEC_ELT (labels, i);
	          tree merge_label = CASE_LABEL (merge_case);
		  tree t = int_const_binop (PLUS_EXPR, base_high,
					    integer_one_node, 1);

		  /* Merge the cases if they jump to the same place,
		     and their ranges are consecutive.  */
		  if (merge_label == base_label
		      && tree_int_cst_equal (CASE_LOW (merge_case), t))
		    {
		      base_high = CASE_HIGH (merge_case) ?
			CASE_HIGH (merge_case) : CASE_LOW (merge_case);
		      CASE_HIGH (base_case) = base_high;
		      TREE_VEC_ELT (labels, i) = NULL_TREE;
		      new_size--;
		      i++;
		    }
		  else
		    break;
		}
	    }

	  /* Compress the case labels in the label vector, and adjust the
	     length of the vector.  */
	  for (i = 0, j = 0; i < new_size; i++)
	    {
	      while (! TREE_VEC_ELT (labels, j))
		j++;
	      TREE_VEC_ELT (labels, i) = TREE_VEC_ELT (labels, j++);
	    }
	  TREE_VEC_LENGTH (labels) = new_size;
	}
    }
}

/* Checks whether we can merge block B into block A.  */

static bool
tree_can_merge_blocks_p (basic_block a, basic_block b)
{
  tree stmt;
  block_stmt_iterator bsi;
  tree phi;

  if (!single_succ_p (a))
    return false;

  if (single_succ_edge (a)->flags & EDGE_ABNORMAL)
    return false;

  if (single_succ (a) != b)
    return false;

  if (!single_pred_p (b))
    return false;

  if (b == EXIT_BLOCK_PTR)
    return false;

  /* If A ends by a statement causing exceptions or something similar, we
     cannot merge the blocks.  */
  stmt = last_stmt (a);
  if (stmt && stmt_ends_bb_p (stmt))
    return false;

  /* Do not allow a block with only a non-local label to be merged.  */
  if (stmt && TREE_CODE (stmt) == LABEL_EXPR
      && DECL_NONLOCAL (LABEL_EXPR_LABEL (stmt)))
    return false;

  /* It must be possible to eliminate all phi nodes in B.  If ssa form
     is not up-to-date, we cannot eliminate any phis.  */
  phi = phi_nodes (b);
  if (phi)
    {
      if (need_ssa_update_p ())
	return false;

      for (; phi; phi = PHI_CHAIN (phi))
	if (!is_gimple_reg (PHI_RESULT (phi))
	    && !may_propagate_copy (PHI_RESULT (phi), PHI_ARG_DEF (phi, 0)))
	  return false;
    }

  /* Do not remove user labels.  */
  for (bsi = bsi_start (b); !bsi_end_p (bsi); bsi_next (&bsi))
    {
      stmt = bsi_stmt (bsi);
      if (TREE_CODE (stmt) != LABEL_EXPR)
	break;
      if (!DECL_ARTIFICIAL (LABEL_EXPR_LABEL (stmt)))
	return false;
    }

  /* Protect the loop latches.  */
  if (current_loops
      && b->loop_father->latch == b)
    return false;

  return true;
}

/* Replaces all uses of NAME by VAL.  */

void
replace_uses_by (tree name, tree val)
{
  imm_use_iterator imm_iter;
  use_operand_p use;
  tree stmt;
  edge e;
  unsigned i;


  FOR_EACH_IMM_USE_STMT (stmt, imm_iter, name)
    {
      FOR_EACH_IMM_USE_ON_STMT (use, imm_iter)
        {
	  replace_exp (use, val);

	  if (TREE_CODE (stmt) == PHI_NODE)
	    {
	      e = PHI_ARG_EDGE (stmt, PHI_ARG_INDEX_FROM_USE (use));
	      if (e->flags & EDGE_ABNORMAL)
		{
		  /* This can only occur for virtual operands, since
		     for the real ones SSA_NAME_OCCURS_IN_ABNORMAL_PHI (name))
		     would prevent replacement.  */
		  gcc_assert (!is_gimple_reg (name));
		  SSA_NAME_OCCURS_IN_ABNORMAL_PHI (val) = 1;
		}
	    }
	}
      if (TREE_CODE (stmt) != PHI_NODE)
	{
	  tree rhs;

	  fold_stmt_inplace (stmt);
	  rhs = get_rhs (stmt);
	  if (TREE_CODE (rhs) == ADDR_EXPR)
	    recompute_tree_invariant_for_addr_expr (rhs);

	  maybe_clean_or_replace_eh_stmt (stmt, stmt);
	  mark_new_vars_to_rename (stmt);
	}
    }

  gcc_assert (num_imm_uses (name) == 0);

  /* Also update the trees stored in loop structures.  */
  if (current_loops)
    {
      struct loop *loop;

      for (i = 0; i < current_loops->num; i++)
	{
	  loop = current_loops->parray[i];
	  if (loop)
	    substitute_in_loop_info (loop, name, val);
	}
    }
}

/* Merge block B into block A.  */

static void
tree_merge_blocks (basic_block a, basic_block b)
{
  block_stmt_iterator bsi;
  tree_stmt_iterator last;
  tree phi;

  if (dump_file)
    fprintf (dump_file, "Merging blocks %d and %d\n", a->index, b->index);

  /* Remove all single-valued PHI nodes from block B of the form
     V_i = PHI <V_j> by propagating V_j to all the uses of V_i.  */
  bsi = bsi_last (a);
  for (phi = phi_nodes (b); phi; phi = phi_nodes (b))
    {
      tree def = PHI_RESULT (phi), use = PHI_ARG_DEF (phi, 0);
      tree copy;
      bool may_replace_uses = may_propagate_copy (def, use);

      /* In case we have loops to care about, do not propagate arguments of
	 loop closed ssa phi nodes.  */
      if (current_loops
	  && is_gimple_reg (def)
	  && TREE_CODE (use) == SSA_NAME
	  && a->loop_father != b->loop_father)
	may_replace_uses = false;

      if (!may_replace_uses)
	{
	  gcc_assert (is_gimple_reg (def));

	  /* Note that just emitting the copies is fine -- there is no problem
	     with ordering of phi nodes.  This is because A is the single
	     predecessor of B, therefore results of the phi nodes cannot
	     appear as arguments of the phi nodes.  */
	  copy = build2 (MODIFY_EXPR, void_type_node, def, use);
	  bsi_insert_after (&bsi, copy, BSI_NEW_STMT);
	  SET_PHI_RESULT (phi, NULL_TREE);
	  SSA_NAME_DEF_STMT (def) = copy;
	}
      else
	replace_uses_by (def, use);

      remove_phi_node (phi, NULL);
    }

  /* Ensure that B follows A.  */
  move_block_after (b, a);

  gcc_assert (single_succ_edge (a)->flags & EDGE_FALLTHRU);
  gcc_assert (!last_stmt (a) || !stmt_ends_bb_p (last_stmt (a)));

  /* Remove labels from B and set bb_for_stmt to A for other statements.  */
  for (bsi = bsi_start (b); !bsi_end_p (bsi);)
    {
      if (TREE_CODE (bsi_stmt (bsi)) == LABEL_EXPR)
	{
	  tree label = bsi_stmt (bsi);

	  bsi_remove (&bsi, false);
	  /* Now that we can thread computed gotos, we might have
	     a situation where we have a forced label in block B
	     However, the label at the start of block B might still be
	     used in other ways (think about the runtime checking for
	     Fortran assigned gotos).  So we can not just delete the
	     label.  Instead we move the label to the start of block A.  */
	  if (FORCED_LABEL (LABEL_EXPR_LABEL (label)))
	    {
	      block_stmt_iterator dest_bsi = bsi_start (a);
	      bsi_insert_before (&dest_bsi, label, BSI_NEW_STMT);
	    }
	}
      else
	{
	  change_bb_for_stmt (bsi_stmt (bsi), a);
	  bsi_next (&bsi);
	}
    }

  /* Merge the chains.  */
  last = tsi_last (a->stmt_list);
  tsi_link_after (&last, b->stmt_list, TSI_NEW_STMT);
  b->stmt_list = NULL;
}


/* Return the one of two successors of BB that is not reachable by a
   reached by a complex edge, if there is one.  Else, return BB.  We use
   this in optimizations that use post-dominators for their heuristics,
   to catch the cases in C++ where function calls are involved.  */

basic_block
single_noncomplex_succ (basic_block bb)
{
  edge e0, e1;
  if (EDGE_COUNT (bb->succs) != 2)
    return bb;

  e0 = EDGE_SUCC (bb, 0);
  e1 = EDGE_SUCC (bb, 1);
  if (e0->flags & EDGE_COMPLEX)
    return e1->dest;
  if (e1->flags & EDGE_COMPLEX)
    return e0->dest;

  return bb;
}


/* Walk the function tree removing unnecessary statements.

     * Empty statement nodes are removed

     * Unnecessary TRY_FINALLY and TRY_CATCH blocks are removed

     * Unnecessary COND_EXPRs are removed

     * Some unnecessary BIND_EXPRs are removed

   Clearly more work could be done.  The trick is doing the analysis
   and removal fast enough to be a net improvement in compile times.

   Note that when we remove a control structure such as a COND_EXPR
   BIND_EXPR, or TRY block, we will need to repeat this optimization pass
   to ensure we eliminate all the useless code.  */

struct rus_data
{
  tree *last_goto;
  bool repeat;
  bool may_throw;
  bool may_branch;
  bool has_label;
};

static void remove_useless_stmts_1 (tree *, struct rus_data *);

static bool
remove_useless_stmts_warn_notreached (tree stmt)
{
  if (EXPR_HAS_LOCATION (stmt))
    {
      location_t loc = EXPR_LOCATION (stmt);
      if (LOCATION_LINE (loc) > 0)
	{
	  warning (0, "%Hwill never be executed", &loc);
	  return true;
	}
    }

  switch (TREE_CODE (stmt))
    {
    case STATEMENT_LIST:
      {
	tree_stmt_iterator i;
	for (i = tsi_start (stmt); !tsi_end_p (i); tsi_next (&i))
	  if (remove_useless_stmts_warn_notreached (tsi_stmt (i)))
	    return true;
      }
      break;

    case COND_EXPR:
      if (remove_useless_stmts_warn_notreached (COND_EXPR_COND (stmt)))
	return true;
      if (remove_useless_stmts_warn_notreached (COND_EXPR_THEN (stmt)))
	return true;
      if (remove_useless_stmts_warn_notreached (COND_EXPR_ELSE (stmt)))
	return true;
      break;

    case TRY_FINALLY_EXPR:
    case TRY_CATCH_EXPR:
      if (remove_useless_stmts_warn_notreached (TREE_OPERAND (stmt, 0)))
	return true;
      if (remove_useless_stmts_warn_notreached (TREE_OPERAND (stmt, 1)))
	return true;
      break;

    case CATCH_EXPR:
      return remove_useless_stmts_warn_notreached (CATCH_BODY (stmt));
    case EH_FILTER_EXPR:
      return remove_useless_stmts_warn_notreached (EH_FILTER_FAILURE (stmt));
    case BIND_EXPR:
      return remove_useless_stmts_warn_notreached (BIND_EXPR_BLOCK (stmt));

    default:
      /* Not a live container.  */
      break;
    }

  return false;
}

static void
remove_useless_stmts_cond (tree *stmt_p, struct rus_data *data)
{
  tree then_clause, else_clause, cond;
  bool save_has_label, then_has_label, else_has_label;

  save_has_label = data->has_label;
  data->has_label = false;
  data->last_goto = NULL;

  remove_useless_stmts_1 (&COND_EXPR_THEN (*stmt_p), data);

  then_has_label = data->has_label;
  data->has_label = false;
  data->last_goto = NULL;

  remove_useless_stmts_1 (&COND_EXPR_ELSE (*stmt_p), data);

  else_has_label = data->has_label;
  data->has_label = save_has_label | then_has_label | else_has_label;

  then_clause = COND_EXPR_THEN (*stmt_p);
  else_clause = COND_EXPR_ELSE (*stmt_p);
  cond = fold (COND_EXPR_COND (*stmt_p));

  /* If neither arm does anything at all, we can remove the whole IF.  */
  if (!TREE_SIDE_EFFECTS (then_clause) && !TREE_SIDE_EFFECTS (else_clause))
    {
      *stmt_p = build_empty_stmt ();
      data->repeat = true;
    }

  /* If there are no reachable statements in an arm, then we can
     zap the entire conditional.  */
  else if (integer_nonzerop (cond) && !else_has_label)
    {
      if (warn_notreached)
	remove_useless_stmts_warn_notreached (else_clause);
      *stmt_p = then_clause;
      data->repeat = true;
    }
  else if (integer_zerop (cond) && !then_has_label)
    {
      if (warn_notreached)
	remove_useless_stmts_warn_notreached (then_clause);
      *stmt_p = else_clause;
      data->repeat = true;
    }

  /* Check a couple of simple things on then/else with single stmts.  */
  else
    {
      tree then_stmt = expr_only (then_clause);
      tree else_stmt = expr_only (else_clause);

      /* Notice branches to a common destination.  */
      if (then_stmt && else_stmt
	  && TREE_CODE (then_stmt) == GOTO_EXPR
	  && TREE_CODE (else_stmt) == GOTO_EXPR
	  && (GOTO_DESTINATION (then_stmt) == GOTO_DESTINATION (else_stmt)))
	{
	  *stmt_p = then_stmt;
	  data->repeat = true;
	}

      /* If the THEN/ELSE clause merely assigns a value to a variable or
	 parameter which is already known to contain that value, then
	 remove the useless THEN/ELSE clause.  */
      else if (TREE_CODE (cond) == VAR_DECL || TREE_CODE (cond) == PARM_DECL)
	{
	  if (else_stmt
	      && TREE_CODE (else_stmt) == MODIFY_EXPR
	      && TREE_OPERAND (else_stmt, 0) == cond
	      && integer_zerop (TREE_OPERAND (else_stmt, 1)))
	    COND_EXPR_ELSE (*stmt_p) = alloc_stmt_list ();
	}
      else if ((TREE_CODE (cond) == EQ_EXPR || TREE_CODE (cond) == NE_EXPR)
	       && (TREE_CODE (TREE_OPERAND (cond, 0)) == VAR_DECL
		   || TREE_CODE (TREE_OPERAND (cond, 0)) == PARM_DECL)
	       && TREE_CONSTANT (TREE_OPERAND (cond, 1)))
	{
	  tree stmt = (TREE_CODE (cond) == EQ_EXPR
		       ? then_stmt : else_stmt);
	  tree *location = (TREE_CODE (cond) == EQ_EXPR
			    ? &COND_EXPR_THEN (*stmt_p)
			    : &COND_EXPR_ELSE (*stmt_p));

	  if (stmt
	      && TREE_CODE (stmt) == MODIFY_EXPR
	      && TREE_OPERAND (stmt, 0) == TREE_OPERAND (cond, 0)
	      && TREE_OPERAND (stmt, 1) == TREE_OPERAND (cond, 1))
	    *location = alloc_stmt_list ();
	}
    }

  /* Protect GOTOs in the arm of COND_EXPRs from being removed.  They
     would be re-introduced during lowering.  */
  data->last_goto = NULL;
}


static void
remove_useless_stmts_tf (tree *stmt_p, struct rus_data *data)
{
  bool save_may_branch, save_may_throw;
  bool this_may_branch, this_may_throw;

  /* Collect may_branch and may_throw information for the body only.  */
  save_may_branch = data->may_branch;
  save_may_throw = data->may_throw;
  data->may_branch = false;
  data->may_throw = false;
  data->last_goto = NULL;

  remove_useless_stmts_1 (&TREE_OPERAND (*stmt_p, 0), data);

  this_may_branch = data->may_branch;
  this_may_throw = data->may_throw;
  data->may_branch |= save_may_branch;
  data->may_throw |= save_may_throw;
  data->last_goto = NULL;

  remove_useless_stmts_1 (&TREE_OPERAND (*stmt_p, 1), data);

  /* If the body is empty, then we can emit the FINALLY block without
     the enclosing TRY_FINALLY_EXPR.  */
  if (!TREE_SIDE_EFFECTS (TREE_OPERAND (*stmt_p, 0)))
    {
      *stmt_p = TREE_OPERAND (*stmt_p, 1);
      data->repeat = true;
    }

  /* If the handler is empty, then we can emit the TRY block without
     the enclosing TRY_FINALLY_EXPR.  */
  else if (!TREE_SIDE_EFFECTS (TREE_OPERAND (*stmt_p, 1)))
    {
      *stmt_p = TREE_OPERAND (*stmt_p, 0);
      data->repeat = true;
    }

  /* If the body neither throws, nor branches, then we can safely
     string the TRY and FINALLY blocks together.  */
  else if (!this_may_branch && !this_may_throw)
    {
      tree stmt = *stmt_p;
      *stmt_p = TREE_OPERAND (stmt, 0);
      append_to_statement_list (TREE_OPERAND (stmt, 1), stmt_p);
      data->repeat = true;
    }
}


static void
remove_useless_stmts_tc (tree *stmt_p, struct rus_data *data)
{
  bool save_may_throw, this_may_throw;
  tree_stmt_iterator i;
  tree stmt;

  /* Collect may_throw information for the body only.  */
  save_may_throw = data->may_throw;
  data->may_throw = false;
  data->last_goto = NULL;

  remove_useless_stmts_1 (&TREE_OPERAND (*stmt_p, 0), data);

  this_may_throw = data->may_throw;
  data->may_throw = save_may_throw;

  /* If the body cannot throw, then we can drop the entire TRY_CATCH_EXPR.  */
  if (!this_may_throw)
    {
      if (warn_notreached)
	remove_useless_stmts_warn_notreached (TREE_OPERAND (*stmt_p, 1));
      *stmt_p = TREE_OPERAND (*stmt_p, 0);
      data->repeat = true;
      return;
    }

  /* Process the catch clause specially.  We may be able to tell that
     no exceptions propagate past this point.  */

  this_may_throw = true;
  i = tsi_start (TREE_OPERAND (*stmt_p, 1));
  stmt = tsi_stmt (i);
  data->last_goto = NULL;

  switch (TREE_CODE (stmt))
    {
    case CATCH_EXPR:
      for (; !tsi_end_p (i); tsi_next (&i))
	{
	  stmt = tsi_stmt (i);
	  /* If we catch all exceptions, then the body does not
	     propagate exceptions past this point.  */
	  if (CATCH_TYPES (stmt) == NULL)
	    this_may_throw = false;
	  data->last_goto = NULL;
	  remove_useless_stmts_1 (&CATCH_BODY (stmt), data);
	}
      break;

    case EH_FILTER_EXPR:
      if (EH_FILTER_MUST_NOT_THROW (stmt))
	this_may_throw = false;
      else if (EH_FILTER_TYPES (stmt) == NULL)
	this_may_throw = false;
      remove_useless_stmts_1 (&EH_FILTER_FAILURE (stmt), data);
      break;

    default:
      /* Otherwise this is a cleanup.  */
      remove_useless_stmts_1 (&TREE_OPERAND (*stmt_p, 1), data);

      /* If the cleanup is empty, then we can emit the TRY block without
	 the enclosing TRY_CATCH_EXPR.  */
      if (!TREE_SIDE_EFFECTS (TREE_OPERAND (*stmt_p, 1)))
	{
	  *stmt_p = TREE_OPERAND (*stmt_p, 0);
	  data->repeat = true;
	}
      break;
    }
  data->may_throw |= this_may_throw;
}


static void
remove_useless_stmts_bind (tree *stmt_p, struct rus_data *data)
{
  tree block;

  /* First remove anything underneath the BIND_EXPR.  */
  remove_useless_stmts_1 (&BIND_EXPR_BODY (*stmt_p), data);

  /* If the BIND_EXPR has no variables, then we can pull everything
     up one level and remove the BIND_EXPR, unless this is the toplevel
     BIND_EXPR for the current function or an inlined function.

     When this situation occurs we will want to apply this
     optimization again.  */
  block = BIND_EXPR_BLOCK (*stmt_p);
  if (BIND_EXPR_VARS (*stmt_p) == NULL_TREE
      && *stmt_p != DECL_SAVED_TREE (current_function_decl)
      && (! block
	  || ! BLOCK_ABSTRACT_ORIGIN (block)
	  || (TREE_CODE (BLOCK_ABSTRACT_ORIGIN (block))
	      != FUNCTION_DECL)))
    {
      *stmt_p = BIND_EXPR_BODY (*stmt_p);
      data->repeat = true;
    }
}


static void
remove_useless_stmts_goto (tree *stmt_p, struct rus_data *data)
{
  tree dest = GOTO_DESTINATION (*stmt_p);

  data->may_branch = true;
  data->last_goto = NULL;

  /* Record the last goto expr, so that we can delete it if unnecessary.  */
  if (TREE_CODE (dest) == LABEL_DECL)
    data->last_goto = stmt_p;
}


static void
remove_useless_stmts_label (tree *stmt_p, struct rus_data *data)
{
  tree label = LABEL_EXPR_LABEL (*stmt_p);

  data->has_label = true;

  /* We do want to jump across non-local label receiver code.  */
  if (DECL_NONLOCAL (label))
    data->last_goto = NULL;

  else if (data->last_goto && GOTO_DESTINATION (*data->last_goto) == label)
    {
      *data->last_goto = build_empty_stmt ();
      data->repeat = true;
    }

  /* ??? Add something here to delete unused labels.  */
}


/* If the function is "const" or "pure", then clear TREE_SIDE_EFFECTS on its
   decl.  This allows us to eliminate redundant or useless
   calls to "const" functions.

   Gimplifier already does the same operation, but we may notice functions
   being const and pure once their calls has been gimplified, so we need
   to update the flag.  */

static void
update_call_expr_flags (tree call)
{
  tree decl = get_callee_fndecl (call);
  if (!decl)
    return;
  if (call_expr_flags (call) & (ECF_CONST | ECF_PURE))
    TREE_SIDE_EFFECTS (call) = 0;
  if (TREE_NOTHROW (decl))
    TREE_NOTHROW (call) = 1;
}


/* T is CALL_EXPR.  Set current_function_calls_* flags.  */

void
notice_special_calls (tree t)
{
  int flags = call_expr_flags (t);

  if (flags & ECF_MAY_BE_ALLOCA)
    current_function_calls_alloca = true;
  if (flags & ECF_RETURNS_TWICE)
    current_function_calls_setjmp = true;
}


/* Clear flags set by notice_special_calls.  Used by dead code removal
   to update the flags.  */

void
clear_special_calls (void)
{
  current_function_calls_alloca = false;
  current_function_calls_setjmp = false;
}


static void
remove_useless_stmts_1 (tree *tp, struct rus_data *data)
{
  tree t = *tp, op;

  switch (TREE_CODE (t))
    {
    case COND_EXPR:
      remove_useless_stmts_cond (tp, data);
      break;

    case TRY_FINALLY_EXPR:
      remove_useless_stmts_tf (tp, data);
      break;

    case TRY_CATCH_EXPR:
      remove_useless_stmts_tc (tp, data);
      break;

    case BIND_EXPR:
      remove_useless_stmts_bind (tp, data);
      break;

    case GOTO_EXPR:
      remove_useless_stmts_goto (tp, data);
      break;

    case LABEL_EXPR:
      remove_useless_stmts_label (tp, data);
      break;

    case RETURN_EXPR:
      fold_stmt (tp);
      data->last_goto = NULL;
      data->may_branch = true;
      break;

    case CALL_EXPR:
      fold_stmt (tp);
      data->last_goto = NULL;
      notice_special_calls (t);
      update_call_expr_flags (t);
      if (tree_could_throw_p (t))
	data->may_throw = true;
      break;

    case MODIFY_EXPR:
      data->last_goto = NULL;
      fold_stmt (tp);
      op = get_call_expr_in (t);
      if (op)
	{
	  update_call_expr_flags (op);
	  notice_special_calls (op);
	}
      if (tree_could_throw_p (t))
	data->may_throw = true;
      break;

    case STATEMENT_LIST:
      {
	tree_stmt_iterator i = tsi_start (t);
	while (!tsi_end_p (i))
	  {
	    t = tsi_stmt (i);
	    if (IS_EMPTY_STMT (t))
	      {
		tsi_delink (&i);
		continue;
	      }

	    remove_useless_stmts_1 (tsi_stmt_ptr (i), data);

	    t = tsi_stmt (i);
	    if (TREE_CODE (t) == STATEMENT_LIST)
	      {
		tsi_link_before (&i, t, TSI_SAME_STMT);
		tsi_delink (&i);
	      }
	    else
	      tsi_next (&i);
	  }
      }
      break;
    case ASM_EXPR:
      fold_stmt (tp);
      data->last_goto = NULL;
      break;

    default:
      data->last_goto = NULL;
      break;
    }
}

static unsigned int
remove_useless_stmts (void)
{
  struct rus_data data;

  clear_special_calls ();

  do
    {
      memset (&data, 0, sizeof (data));
      remove_useless_stmts_1 (&DECL_SAVED_TREE (current_function_decl), &data);
    }
  while (data.repeat);
  return 0;
}


struct tree_opt_pass pass_remove_useless_stmts =
{
  "useless",				/* name */
  NULL,					/* gate */
  remove_useless_stmts,			/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  0,					/* tv_id */
  PROP_gimple_any,			/* properties_required */
  0,					/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  TODO_dump_func,			/* todo_flags_finish */
  0					/* letter */
};

/* Remove PHI nodes associated with basic block BB and all edges out of BB.  */

static void
remove_phi_nodes_and_edges_for_unreachable_block (basic_block bb)
{
  tree phi;

  /* Since this block is no longer reachable, we can just delete all
     of its PHI nodes.  */
  phi = phi_nodes (bb);
  while (phi)
    {
      tree next = PHI_CHAIN (phi);
      remove_phi_node (phi, NULL_TREE);
      phi = next;
    }

  /* Remove edges to BB's successors.  */
  while (EDGE_COUNT (bb->succs) > 0)
    remove_edge (EDGE_SUCC (bb, 0));
}


/* Remove statements of basic block BB.  */

static void
remove_bb (basic_block bb)
{
  block_stmt_iterator i;
#ifdef USE_MAPPED_LOCATION
  source_location loc = UNKNOWN_LOCATION;
#else
  source_locus loc = 0;
#endif

  if (dump_file)
    {
      fprintf (dump_file, "Removing basic block %d\n", bb->index);
      if (dump_flags & TDF_DETAILS)
	{
	  dump_bb (bb, dump_file, 0);
	  fprintf (dump_file, "\n");
	}
    }

  /* If we remove the header or the latch of a loop, mark the loop for
     removal by setting its header and latch to NULL.  */
  if (current_loops)
    {
      struct loop *loop = bb->loop_father;

      if (loop->latch == bb
	  || loop->header == bb)
	{
	  loop->latch = NULL;
	  loop->header = NULL;

	  /* Also clean up the information associated with the loop.  Updating
	     it would waste time. More importantly, it may refer to ssa
	     names that were defined in other removed basic block -- these
	     ssa names are now removed and invalid.  */
	  free_numbers_of_iterations_estimates_loop (loop);
	}
    }

  /* Remove all the instructions in the block.  */
  for (i = bsi_start (bb); !bsi_end_p (i);)
    {
      tree stmt = bsi_stmt (i);
      if (TREE_CODE (stmt) == LABEL_EXPR
          && (FORCED_LABEL (LABEL_EXPR_LABEL (stmt))
	      || DECL_NONLOCAL (LABEL_EXPR_LABEL (stmt))))
	{
	  basic_block new_bb;
	  block_stmt_iterator new_bsi;

	  /* A non-reachable non-local label may still be referenced.
	     But it no longer needs to carry the extra semantics of
	     non-locality.  */
	  if (DECL_NONLOCAL (LABEL_EXPR_LABEL (stmt)))
	    {
	      DECL_NONLOCAL (LABEL_EXPR_LABEL (stmt)) = 0;
	      FORCED_LABEL (LABEL_EXPR_LABEL (stmt)) = 1;
	    }

	  new_bb = bb->prev_bb;
	  new_bsi = bsi_start (new_bb);
	  bsi_remove (&i, false);
	  bsi_insert_before (&new_bsi, stmt, BSI_NEW_STMT);
	}
      else
        {
	  /* Release SSA definitions if we are in SSA.  Note that we
	     may be called when not in SSA.  For example,
	     final_cleanup calls this function via
	     cleanup_tree_cfg.  */
	  if (in_ssa_p)
	    release_defs (stmt);

	  bsi_remove (&i, true);
	}

      /* Don't warn for removed gotos.  Gotos are often removed due to
	 jump threading, thus resulting in bogus warnings.  Not great,
	 since this way we lose warnings for gotos in the original
	 program that are indeed unreachable.  */
      if (TREE_CODE (stmt) != GOTO_EXPR && EXPR_HAS_LOCATION (stmt) && !loc)
	{
#ifdef USE_MAPPED_LOCATION
	  if (EXPR_HAS_LOCATION (stmt))
	    loc = EXPR_LOCATION (stmt);
#else
	  source_locus t;
	  t = EXPR_LOCUS (stmt);
	  if (t && LOCATION_LINE (*t) > 0)
	    loc = t;
#endif
	}
    }

  /* If requested, give a warning that the first statement in the
     block is unreachable.  We walk statements backwards in the
     loop above, so the last statement we process is the first statement
     in the block.  */
#ifdef USE_MAPPED_LOCATION
  if (loc > BUILTINS_LOCATION)
    warning (OPT_Wunreachable_code, "%Hwill never be executed", &loc);
#else
  if (loc)
    warning (OPT_Wunreachable_code, "%Hwill never be executed", loc);
#endif

  remove_phi_nodes_and_edges_for_unreachable_block (bb);
}


/* Given a basic block BB ending with COND_EXPR or SWITCH_EXPR, and a
   predicate VAL, return the edge that will be taken out of the block.
   If VAL does not match a unique edge, NULL is returned.  */

edge
find_taken_edge (basic_block bb, tree val)
{
  tree stmt;

  stmt = last_stmt (bb);

  gcc_assert (stmt);
  gcc_assert (is_ctrl_stmt (stmt));
  gcc_assert (val);

  if (! is_gimple_min_invariant (val))
    return NULL;

  if (TREE_CODE (stmt) == COND_EXPR)
    return find_taken_edge_cond_expr (bb, val);

  if (TREE_CODE (stmt) == SWITCH_EXPR)
    return find_taken_edge_switch_expr (bb, val);

  if (computed_goto_p (stmt))
    {
      /* Only optimize if the argument is a label, if the argument is
	 not a label then we can not construct a proper CFG.

         It may be the case that we only need to allow the LABEL_REF to
         appear inside an ADDR_EXPR, but we also allow the LABEL_REF to
         appear inside a LABEL_EXPR just to be safe.  */
      if ((TREE_CODE (val) == ADDR_EXPR || TREE_CODE (val) == LABEL_EXPR)
	  && TREE_CODE (TREE_OPERAND (val, 0)) == LABEL_DECL)
	return find_taken_edge_computed_goto (bb, TREE_OPERAND (val, 0));
      return NULL;
    }

  gcc_unreachable ();
}

/* Given a constant value VAL and the entry block BB to a GOTO_EXPR
   statement, determine which of the outgoing edges will be taken out of the
   block.  Return NULL if either edge may be taken.  */

static edge
find_taken_edge_computed_goto (basic_block bb, tree val)
{
  basic_block dest;
  edge e = NULL;

  dest = label_to_block (val);
  if (dest)
    {
      e = find_edge (bb, dest);
      gcc_assert (e != NULL);
    }

  return e;
}

/* Given a constant value VAL and the entry block BB to a COND_EXPR
   statement, determine which of the two edges will be taken out of the
   block.  Return NULL if either edge may be taken.  */

static edge
find_taken_edge_cond_expr (basic_block bb, tree val)
{
  edge true_edge, false_edge;

  extract_true_false_edges_from_block (bb, &true_edge, &false_edge);

  gcc_assert (TREE_CODE (val) == INTEGER_CST);
  return (zero_p (val) ? false_edge : true_edge);
}

/* Given an INTEGER_CST VAL and the entry block BB to a SWITCH_EXPR
   statement, determine which edge will be taken out of the block.  Return
   NULL if any edge may be taken.  */

static edge
find_taken_edge_switch_expr (basic_block bb, tree val)
{
  tree switch_expr, taken_case;
  basic_block dest_bb;
  edge e;

  switch_expr = last_stmt (bb);
  taken_case = find_case_label_for_value (switch_expr, val);
  dest_bb = label_to_block (CASE_LABEL (taken_case));

  e = find_edge (bb, dest_bb);
  gcc_assert (e);
  return e;
}


/* Return the CASE_LABEL_EXPR that SWITCH_EXPR will take for VAL.
   We can make optimal use here of the fact that the case labels are
   sorted: We can do a binary search for a case matching VAL.  */

static tree
find_case_label_for_value (tree switch_expr, tree val)
{
  tree vec = SWITCH_LABELS (switch_expr);
  size_t low, high, n = TREE_VEC_LENGTH (vec);
  tree default_case = TREE_VEC_ELT (vec, n - 1);

  for (low = -1, high = n - 1; high - low > 1; )
    {
      size_t i = (high + low) / 2;
      tree t = TREE_VEC_ELT (vec, i);
      int cmp;

      /* Cache the result of comparing CASE_LOW and val.  */
      cmp = tree_int_cst_compare (CASE_LOW (t), val);

      if (cmp > 0)
	high = i;
      else
	low = i;

      if (CASE_HIGH (t) == NULL)
	{
	  /* A singe-valued case label.  */
	  if (cmp == 0)
	    return t;
	}
      else
	{
	  /* A case range.  We can only handle integer ranges.  */
	  if (cmp <= 0 && tree_int_cst_compare (CASE_HIGH (t), val) >= 0)
	    return t;
	}
    }

  return default_case;
}




/*---------------------------------------------------------------------------
			      Debugging functions
---------------------------------------------------------------------------*/

/* Dump tree-specific information of block BB to file OUTF.  */

void
tree_dump_bb (basic_block bb, FILE *outf, int indent)
{
  dump_generic_bb (outf, bb, indent, TDF_VOPS);
}


/* Dump a basic block on stderr.  */

void
debug_tree_bb (basic_block bb)
{
  dump_bb (bb, stderr, 0);
}


/* Dump basic block with index N on stderr.  */

basic_block
debug_tree_bb_n (int n)
{
  debug_tree_bb (BASIC_BLOCK (n));
  return BASIC_BLOCK (n);
}


/* Dump the CFG on stderr.

   FLAGS are the same used by the tree dumping functions
   (see TDF_* in tree-pass.h).  */

void
debug_tree_cfg (int flags)
{
  dump_tree_cfg (stderr, flags);
}


/* Dump the program showing basic block boundaries on the given FILE.

   FLAGS are the same used by the tree dumping functions (see TDF_* in
   tree.h).  */

void
dump_tree_cfg (FILE *file, int flags)
{
  if (flags & TDF_DETAILS)
    {
      const char *funcname
	= lang_hooks.decl_printable_name (current_function_decl, 2);

      fputc ('\n', file);
      fprintf (file, ";; Function %s\n\n", funcname);
      fprintf (file, ";; \n%d basic blocks, %d edges, last basic block %d.\n\n",
	       n_basic_blocks, n_edges, last_basic_block);

      brief_dump_cfg (file);
      fprintf (file, "\n");
    }

  if (flags & TDF_STATS)
    dump_cfg_stats (file);

  dump_function_to_file (current_function_decl, file, flags | TDF_BLOCKS);
}


/* Dump CFG statistics on FILE.  */

void
dump_cfg_stats (FILE *file)
{
  static long max_num_merged_labels = 0;
  unsigned long size, total = 0;
  long num_edges;
  basic_block bb;
  const char * const fmt_str   = "%-30s%-13s%12s\n";
  const char * const fmt_str_1 = "%-30s%13d%11lu%c\n";
  const char * const fmt_str_2 = "%-30s%13ld%11lu%c\n";
  const char * const fmt_str_3 = "%-43s%11lu%c\n";
  const char *funcname
    = lang_hooks.decl_printable_name (current_function_decl, 2);


  fprintf (file, "\nCFG Statistics for %s\n\n", funcname);

  fprintf (file, "---------------------------------------------------------\n");
  fprintf (file, fmt_str, "", "  Number of  ", "Memory");
  fprintf (file, fmt_str, "", "  instances  ", "used ");
  fprintf (file, "---------------------------------------------------------\n");

  size = n_basic_blocks * sizeof (struct basic_block_def);
  total += size;
  fprintf (file, fmt_str_1, "Basic blocks", n_basic_blocks,
	   SCALE (size), LABEL (size));

  num_edges = 0;
  FOR_EACH_BB (bb)
    num_edges += EDGE_COUNT (bb->succs);
  size = num_edges * sizeof (struct edge_def);
  total += size;
  fprintf (file, fmt_str_2, "Edges", num_edges, SCALE (size), LABEL (size));

  fprintf (file, "---------------------------------------------------------\n");
  fprintf (file, fmt_str_3, "Total memory used by CFG data", SCALE (total),
	   LABEL (total));
  fprintf (file, "---------------------------------------------------------\n");
  fprintf (file, "\n");

  if (cfg_stats.num_merged_labels > max_num_merged_labels)
    max_num_merged_labels = cfg_stats.num_merged_labels;

  fprintf (file, "Coalesced label blocks: %ld (Max so far: %ld)\n",
	   cfg_stats.num_merged_labels, max_num_merged_labels);

  fprintf (file, "\n");
}


/* Dump CFG statistics on stderr.  Keep extern so that it's always
   linked in the final executable.  */

void
debug_cfg_stats (void)
{
  dump_cfg_stats (stderr);
}


/* Dump the flowgraph to a .vcg FILE.  */

static void
tree_cfg2vcg (FILE *file)
{
  edge e;
  edge_iterator ei;
  basic_block bb;
  const char *funcname
    = lang_hooks.decl_printable_name (current_function_decl, 2);

  /* Write the file header.  */
  fprintf (file, "graph: { title: \"%s\"\n", funcname);
  fprintf (file, "node: { title: \"ENTRY\" label: \"ENTRY\" }\n");
  fprintf (file, "node: { title: \"EXIT\" label: \"EXIT\" }\n");

  /* Write blocks and edges.  */
  FOR_EACH_EDGE (e, ei, ENTRY_BLOCK_PTR->succs)
    {
      fprintf (file, "edge: { sourcename: \"ENTRY\" targetname: \"%d\"",
	       e->dest->index);

      if (e->flags & EDGE_FAKE)
	fprintf (file, " linestyle: dotted priority: 10");
      else
	fprintf (file, " linestyle: solid priority: 100");

      fprintf (file, " }\n");
    }
  fputc ('\n', file);

  FOR_EACH_BB (bb)
    {
      enum tree_code head_code, end_code;
      const char *head_name, *end_name;
      int head_line = 0;
      int end_line = 0;
      tree first = first_stmt (bb);
      tree last = last_stmt (bb);

      if (first)
	{
	  head_code = TREE_CODE (first);
	  head_name = tree_code_name[head_code];
	  head_line = get_lineno (first);
	}
      else
	head_name = "no-statement";

      if (last)
	{
	  end_code = TREE_CODE (last);
	  end_name = tree_code_name[end_code];
	  end_line = get_lineno (last);
	}
      else
	end_name = "no-statement";

      fprintf (file, "node: { title: \"%d\" label: \"#%d\\n%s (%d)\\n%s (%d)\"}\n",
	       bb->index, bb->index, head_name, head_line, end_name,
	       end_line);

      FOR_EACH_EDGE (e, ei, bb->succs)
	{
	  if (e->dest == EXIT_BLOCK_PTR)
	    fprintf (file, "edge: { sourcename: \"%d\" targetname: \"EXIT\"", bb->index);
	  else
	    fprintf (file, "edge: { sourcename: \"%d\" targetname: \"%d\"", bb->index, e->dest->index);

	  if (e->flags & EDGE_FAKE)
	    fprintf (file, " priority: 10 linestyle: dotted");
	  else
	    fprintf (file, " priority: 100 linestyle: solid");

	  fprintf (file, " }\n");
	}

      if (bb->next_bb != EXIT_BLOCK_PTR)
	fputc ('\n', file);
    }

  fputs ("}\n\n", file);
}



/*---------------------------------------------------------------------------
			     Miscellaneous helpers
---------------------------------------------------------------------------*/

/* Return true if T represents a stmt that always transfers control.  */

bool
is_ctrl_stmt (tree t)
{
  return (TREE_CODE (t) == COND_EXPR
	  || TREE_CODE (t) == SWITCH_EXPR
	  || TREE_CODE (t) == GOTO_EXPR
	  || TREE_CODE (t) == RETURN_EXPR
	  || TREE_CODE (t) == RESX_EXPR);
}


/* Return true if T is a statement that may alter the flow of control
   (e.g., a call to a non-returning function).  */

bool
is_ctrl_altering_stmt (tree t)
{
  tree call;

  gcc_assert (t);
  call = get_call_expr_in (t);
  if (call)
    {
      /* A non-pure/const CALL_EXPR alters flow control if the current
	 function has nonlocal labels.  */
      if (TREE_SIDE_EFFECTS (call) && current_function_has_nonlocal_label)
	return true;

      /* A CALL_EXPR also alters control flow if it does not return.  */
      if (call_expr_flags (call) & ECF_NORETURN)
	return true;
    }

  /* OpenMP directives alter control flow.  */
  if (OMP_DIRECTIVE_P (t))
    return true;

  /* If a statement can throw, it alters control flow.  */
  return tree_can_throw_internal (t);
}


/* Return true if T is a computed goto.  */

bool
computed_goto_p (tree t)
{
  return (TREE_CODE (t) == GOTO_EXPR
	  && TREE_CODE (GOTO_DESTINATION (t)) != LABEL_DECL);
}


/* Return true if T is a simple local goto.  */

bool
simple_goto_p (tree t)
{
  return (TREE_CODE (t) == GOTO_EXPR
	  && TREE_CODE (GOTO_DESTINATION (t)) == LABEL_DECL);
}


/* Return true if T can make an abnormal transfer of control flow.
   Transfers of control flow associated with EH are excluded.  */

bool
tree_can_make_abnormal_goto (tree t)
{
  if (computed_goto_p (t))
    return true;
  if (TREE_CODE (t) == MODIFY_EXPR)
    t = TREE_OPERAND (t, 1);
  if (TREE_CODE (t) == WITH_SIZE_EXPR)
    t = TREE_OPERAND (t, 0);
  if (TREE_CODE (t) == CALL_EXPR)
    return TREE_SIDE_EFFECTS (t) && current_function_has_nonlocal_label;
  return false;
}


/* Return true if T should start a new basic block.  PREV_T is the
   statement preceding T.  It is used when T is a label or a case label.
   Labels should only start a new basic block if their previous statement
   wasn't a label.  Otherwise, sequence of labels would generate
   unnecessary basic blocks that only contain a single label.  */

static inline bool
stmt_starts_bb_p (tree t, tree prev_t)
{
  if (t == NULL_TREE)
    return false;

  /* LABEL_EXPRs start a new basic block only if the preceding
     statement wasn't a label of the same type.  This prevents the
     creation of consecutive blocks that have nothing but a single
     label.  */
  if (TREE_CODE (t) == LABEL_EXPR)
    {
      /* Nonlocal and computed GOTO targets always start a new block.  */
      if (DECL_NONLOCAL (LABEL_EXPR_LABEL (t))
	  || FORCED_LABEL (LABEL_EXPR_LABEL (t)))
	return true;

      if (prev_t && TREE_CODE (prev_t) == LABEL_EXPR)
	{
	  if (DECL_NONLOCAL (LABEL_EXPR_LABEL (prev_t)))
	    return true;

	  cfg_stats.num_merged_labels++;
	  return false;
	}
      else
	return true;
    }

  return false;
}


/* Return true if T should end a basic block.  */

bool
stmt_ends_bb_p (tree t)
{
  return is_ctrl_stmt (t) || is_ctrl_altering_stmt (t);
}


/* Add gotos that used to be represented implicitly in the CFG.  */

void
disband_implicit_edges (void)
{
  basic_block bb;
  block_stmt_iterator last;
  edge e;
  edge_iterator ei;
  tree stmt, label;

  FOR_EACH_BB (bb)
    {
      last = bsi_last (bb);
      stmt = last_stmt (bb);

      if (stmt && TREE_CODE (stmt) == COND_EXPR)
	{
	  /* Remove superfluous gotos from COND_EXPR branches.  Moved
	     from cfg_remove_useless_stmts here since it violates the
	     invariants for tree--cfg correspondence and thus fits better
	     here where we do it anyway.  */
	  e = find_edge (bb, bb->next_bb);
	  if (e)
	    {
	      if (e->flags & EDGE_TRUE_VALUE)
		COND_EXPR_THEN (stmt) = build_empty_stmt ();
	      else if (e->flags & EDGE_FALSE_VALUE)
		COND_EXPR_ELSE (stmt) = build_empty_stmt ();
	      else
		gcc_unreachable ();
	      e->flags |= EDGE_FALLTHRU;
	    }

	  continue;
	}

      if (stmt && TREE_CODE (stmt) == RETURN_EXPR)
	{
	  /* Remove the RETURN_EXPR if we may fall though to the exit
	     instead.  */
	  gcc_assert (single_succ_p (bb));
	  gcc_assert (single_succ (bb) == EXIT_BLOCK_PTR);

	  if (bb->next_bb == EXIT_BLOCK_PTR
	      && !TREE_OPERAND (stmt, 0))
	    {
	      bsi_remove (&last, true);
	      single_succ_edge (bb)->flags |= EDGE_FALLTHRU;
	    }
	  continue;
	}

      /* There can be no fallthru edge if the last statement is a control
	 one.  */
      if (stmt && is_ctrl_stmt (stmt))
	continue;

      /* Find a fallthru edge and emit the goto if necessary.  */
      FOR_EACH_EDGE (e, ei, bb->succs)
	if (e->flags & EDGE_FALLTHRU)
	  break;

      if (!e || e->dest == bb->next_bb)
	continue;

      gcc_assert (e->dest != EXIT_BLOCK_PTR);
      label = tree_block_label (e->dest);

      stmt = build1 (GOTO_EXPR, void_type_node, label);
#ifdef USE_MAPPED_LOCATION
      SET_EXPR_LOCATION (stmt, e->goto_locus);
#else
      SET_EXPR_LOCUS (stmt, e->goto_locus);
#endif
      bsi_insert_after (&last, stmt, BSI_NEW_STMT);
      e->flags &= ~EDGE_FALLTHRU;
    }
}

/* Remove block annotations and other datastructures.  */

void
delete_tree_cfg_annotations (void)
{
  label_to_block_map = NULL;
}


/* Return the first statement in basic block BB.  */

tree
first_stmt (basic_block bb)
{
  block_stmt_iterator i = bsi_start (bb);
  return !bsi_end_p (i) ? bsi_stmt (i) : NULL_TREE;
}


/* Return the last statement in basic block BB.  */

tree
last_stmt (basic_block bb)
{
  block_stmt_iterator b = bsi_last (bb);
  return !bsi_end_p (b) ? bsi_stmt (b) : NULL_TREE;
}


/* Return a pointer to the last statement in block BB.  */

tree *
last_stmt_ptr (basic_block bb)
{
  block_stmt_iterator last = bsi_last (bb);
  return !bsi_end_p (last) ? bsi_stmt_ptr (last) : NULL;
}


/* Return the last statement of an otherwise empty block.  Return NULL
   if the block is totally empty, or if it contains more than one
   statement.  */

tree
last_and_only_stmt (basic_block bb)
{
  block_stmt_iterator i = bsi_last (bb);
  tree last, prev;

  if (bsi_end_p (i))
    return NULL_TREE;

  last = bsi_stmt (i);
  bsi_prev (&i);
  if (bsi_end_p (i))
    return last;

  /* Empty statements should no longer appear in the instruction stream.
     Everything that might have appeared before should be deleted by
     remove_useless_stmts, and the optimizers should just bsi_remove
     instead of smashing with build_empty_stmt.

     Thus the only thing that should appear here in a block containing
     one executable statement is a label.  */
  prev = bsi_stmt (i);
  if (TREE_CODE (prev) == LABEL_EXPR)
    return last;
  else
    return NULL_TREE;
}


/* Mark BB as the basic block holding statement T.  */

void
set_bb_for_stmt (tree t, basic_block bb)
{
  if (TREE_CODE (t) == PHI_NODE)
    PHI_BB (t) = bb;
  else if (TREE_CODE (t) == STATEMENT_LIST)
    {
      tree_stmt_iterator i;
      for (i = tsi_start (t); !tsi_end_p (i); tsi_next (&i))
	set_bb_for_stmt (tsi_stmt (i), bb);
    }
  else
    {
      stmt_ann_t ann = get_stmt_ann (t);
      ann->bb = bb;

      /* If the statement is a label, add the label to block-to-labels map
        so that we can speed up edge creation for GOTO_EXPRs.  */
      if (TREE_CODE (t) == LABEL_EXPR)
	{
	  int uid;

	  t = LABEL_EXPR_LABEL (t);
	  uid = LABEL_DECL_UID (t);
	  if (uid == -1)
	    {
	      unsigned old_len = VEC_length (basic_block, label_to_block_map);
	      LABEL_DECL_UID (t) = uid = cfun->last_label_uid++;
	      if (old_len <= (unsigned) uid)
		{
		  basic_block *addr;
		  unsigned new_len = 3 * uid / 2;

		  VEC_safe_grow (basic_block, gc, label_to_block_map,
				 new_len);
		  addr = VEC_address (basic_block, label_to_block_map);
		  memset (&addr[old_len],
			  0, sizeof (basic_block) * (new_len - old_len));
		}
	    }
	  else
	    /* We're moving an existing label.  Make sure that we've
		removed it from the old block.  */
	    gcc_assert (!bb
			|| !VEC_index (basic_block, label_to_block_map, uid));
	  VEC_replace (basic_block, label_to_block_map, uid, bb);
	}
    }
}

/* Faster version of set_bb_for_stmt that assume that statement is being moved
   from one basic block to another.  
   For BB splitting we can run into quadratic case, so performance is quite
   important and knowing that the tables are big enough, change_bb_for_stmt
   can inline as leaf function.  */
static inline void
change_bb_for_stmt (tree t, basic_block bb)
{
  get_stmt_ann (t)->bb = bb;
  if (TREE_CODE (t) == LABEL_EXPR)
    VEC_replace (basic_block, label_to_block_map,
		 LABEL_DECL_UID (LABEL_EXPR_LABEL (t)), bb);
}

/* Finds iterator for STMT.  */

extern block_stmt_iterator
bsi_for_stmt (tree stmt)
{
  block_stmt_iterator bsi;

  for (bsi = bsi_start (bb_for_stmt (stmt)); !bsi_end_p (bsi); bsi_next (&bsi))
    if (bsi_stmt (bsi) == stmt)
      return bsi;

  gcc_unreachable ();
}

/* Mark statement T as modified, and update it.  */
static inline void
update_modified_stmts (tree t)
{
  if (TREE_CODE (t) == STATEMENT_LIST)
    {
      tree_stmt_iterator i;
      tree stmt;
      for (i = tsi_start (t); !tsi_end_p (i); tsi_next (&i))
        {
	  stmt = tsi_stmt (i);
	  update_stmt_if_modified (stmt);
	}
    }
  else
    update_stmt_if_modified (t);
}

/* Insert statement (or statement list) T before the statement
   pointed-to by iterator I.  M specifies how to update iterator I
   after insertion (see enum bsi_iterator_update).  */

void
bsi_insert_before (block_stmt_iterator *i, tree t, enum bsi_iterator_update m)
{
  set_bb_for_stmt (t, i->bb);
  update_modified_stmts (t);
  tsi_link_before (&i->tsi, t, m);
}


/* Insert statement (or statement list) T after the statement
   pointed-to by iterator I.  M specifies how to update iterator I
   after insertion (see enum bsi_iterator_update).  */

void
bsi_insert_after (block_stmt_iterator *i, tree t, enum bsi_iterator_update m)
{
  set_bb_for_stmt (t, i->bb);
  update_modified_stmts (t);
  tsi_link_after (&i->tsi, t, m);
}


/* Remove the statement pointed to by iterator I.  The iterator is updated
   to the next statement.

   When REMOVE_EH_INFO is true we remove the statement pointed to by
   iterator I from the EH tables.  Otherwise we do not modify the EH
   tables.

   Generally, REMOVE_EH_INFO should be true when the statement is going to
   be removed from the IL and not reinserted elsewhere.  */

void
bsi_remove (block_stmt_iterator *i, bool remove_eh_info)
{
  tree t = bsi_stmt (*i);
  set_bb_for_stmt (t, NULL);
  delink_stmt_imm_use (t);
  tsi_delink (&i->tsi);
  mark_stmt_modified (t);
  if (remove_eh_info)
    remove_stmt_from_eh_region (t);
}


/* Move the statement at FROM so it comes right after the statement at TO.  */

void
bsi_move_after (block_stmt_iterator *from, block_stmt_iterator *to)
{
  tree stmt = bsi_stmt (*from);
  bsi_remove (from, false);
  bsi_insert_after (to, stmt, BSI_SAME_STMT);
}


/* Move the statement at FROM so it comes right before the statement at TO.  */

void
bsi_move_before (block_stmt_iterator *from, block_stmt_iterator *to)
{
  tree stmt = bsi_stmt (*from);
  bsi_remove (from, false);
  bsi_insert_before (to, stmt, BSI_SAME_STMT);
}


/* Move the statement at FROM to the end of basic block BB.  */

void
bsi_move_to_bb_end (block_stmt_iterator *from, basic_block bb)
{
  block_stmt_iterator last = bsi_last (bb);

  /* Have to check bsi_end_p because it could be an empty block.  */
  if (!bsi_end_p (last) && is_ctrl_stmt (bsi_stmt (last)))
    bsi_move_before (from, &last);
  else
    bsi_move_after (from, &last);
}


/* Replace the contents of the statement pointed to by iterator BSI
   with STMT.  If UPDATE_EH_INFO is true, the exception handling
   information of the original statement is moved to the new statement.  */

void
bsi_replace (const block_stmt_iterator *bsi, tree stmt, bool update_eh_info)
{
  int eh_region;
  tree orig_stmt = bsi_stmt (*bsi);

  SET_EXPR_LOCUS (stmt, EXPR_LOCUS (orig_stmt));
  set_bb_for_stmt (stmt, bsi->bb);

  /* Preserve EH region information from the original statement, if
     requested by the caller.  */
  if (update_eh_info)
    {
      eh_region = lookup_stmt_eh_region (orig_stmt);
      if (eh_region >= 0)
	{
	  remove_stmt_from_eh_region (orig_stmt);
	  add_stmt_to_eh_region (stmt, eh_region);
	}
    }

  delink_stmt_imm_use (orig_stmt);
  *bsi_stmt_ptr (*bsi) = stmt;
  mark_stmt_modified (stmt);
  update_modified_stmts (stmt);
}


/* Insert the statement pointed-to by BSI into edge E.  Every attempt
   is made to place the statement in an existing basic block, but
   sometimes that isn't possible.  When it isn't possible, the edge is
   split and the statement is added to the new block.

   In all cases, the returned *BSI points to the correct location.  The
   return value is true if insertion should be done after the location,
   or false if it should be done before the location.  If new basic block
   has to be created, it is stored in *NEW_BB.  */

static bool
tree_find_edge_insert_loc (edge e, block_stmt_iterator *bsi,
			   basic_block *new_bb)
{
  basic_block dest, src;
  tree tmp;

  dest = e->dest;
 restart:

  /* If the destination has one predecessor which has no PHI nodes,
     insert there.  Except for the exit block.

     The requirement for no PHI nodes could be relaxed.  Basically we
     would have to examine the PHIs to prove that none of them used
     the value set by the statement we want to insert on E.  That
     hardly seems worth the effort.  */
  if (single_pred_p (dest)
      && ! phi_nodes (dest)
      && dest != EXIT_BLOCK_PTR)
    {
      *bsi = bsi_start (dest);
      if (bsi_end_p (*bsi))
	return true;

      /* Make sure we insert after any leading labels.  */
      tmp = bsi_stmt (*bsi);
      while (TREE_CODE (tmp) == LABEL_EXPR)
	{
	  bsi_next (bsi);
	  if (bsi_end_p (*bsi))
	    break;
	  tmp = bsi_stmt (*bsi);
	}

      if (bsi_end_p (*bsi))
	{
	  *bsi = bsi_last (dest);
	  return true;
	}
      else
	return false;
    }

  /* If the source has one successor, the edge is not abnormal and
     the last statement does not end a basic block, insert there.
     Except for the entry block.  */
  src = e->src;
  if ((e->flags & EDGE_ABNORMAL) == 0
      && single_succ_p (src)
      && src != ENTRY_BLOCK_PTR)
    {
      *bsi = bsi_last (src);
      if (bsi_end_p (*bsi))
	return true;

      tmp = bsi_stmt (*bsi);
      if (!stmt_ends_bb_p (tmp))
	return true;

      /* Insert code just before returning the value.  We may need to decompose
         the return in the case it contains non-trivial operand.  */
      if (TREE_CODE (tmp) == RETURN_EXPR)
        {
	  tree op = TREE_OPERAND (tmp, 0);
	  if (op && !is_gimple_val (op))
	    {
	      gcc_assert (TREE_CODE (op) == MODIFY_EXPR);
	      bsi_insert_before (bsi, op, BSI_NEW_STMT);
	      TREE_OPERAND (tmp, 0) = TREE_OPERAND (op, 0);
	    }
	  bsi_prev (bsi);
	  return true;
        }
    }

  /* Otherwise, create a new basic block, and split this edge.  */
  dest = split_edge (e);
  if (new_bb)
    *new_bb = dest;
  e = single_pred_edge (dest);
  goto restart;
}


/* This routine will commit all pending edge insertions, creating any new
   basic blocks which are necessary.  */

void
bsi_commit_edge_inserts (void)
{
  basic_block bb;
  edge e;
  edge_iterator ei;

  bsi_commit_one_edge_insert (single_succ_edge (ENTRY_BLOCK_PTR), NULL);

  FOR_EACH_BB (bb)
    FOR_EACH_EDGE (e, ei, bb->succs)
      bsi_commit_one_edge_insert (e, NULL);
}


/* Commit insertions pending at edge E. If a new block is created, set NEW_BB
   to this block, otherwise set it to NULL.  */

void
bsi_commit_one_edge_insert (edge e, basic_block *new_bb)
{
  if (new_bb)
    *new_bb = NULL;
  if (PENDING_STMT (e))
    {
      block_stmt_iterator bsi;
      tree stmt = PENDING_STMT (e);

      PENDING_STMT (e) = NULL_TREE;

      if (tree_find_edge_insert_loc (e, &bsi, new_bb))
	bsi_insert_after (&bsi, stmt, BSI_NEW_STMT);
      else
	bsi_insert_before (&bsi, stmt, BSI_NEW_STMT);
    }
}


/* Add STMT to the pending list of edge E.  No actual insertion is
   made until a call to bsi_commit_edge_inserts () is made.  */

void
bsi_insert_on_edge (edge e, tree stmt)
{
  append_to_statement_list (stmt, &PENDING_STMT (e));
}

/* Similar to bsi_insert_on_edge+bsi_commit_edge_inserts.  If a new
   block has to be created, it is returned.  */

basic_block
bsi_insert_on_edge_immediate (edge e, tree stmt)
{
  block_stmt_iterator bsi;
  basic_block new_bb = NULL;

  gcc_assert (!PENDING_STMT (e));

  if (tree_find_edge_insert_loc (e, &bsi, &new_bb))
    bsi_insert_after (&bsi, stmt, BSI_NEW_STMT);
  else
    bsi_insert_before (&bsi, stmt, BSI_NEW_STMT);

  return new_bb;
}

/*---------------------------------------------------------------------------
	     Tree specific functions for CFG manipulation
---------------------------------------------------------------------------*/

/* Reinstall those PHI arguments queued in OLD_EDGE to NEW_EDGE.  */

static void
reinstall_phi_args (edge new_edge, edge old_edge)
{
  tree var, phi;

  if (!PENDING_STMT (old_edge))
    return;

  for (var = PENDING_STMT (old_edge), phi = phi_nodes (new_edge->dest);
       var && phi;
       var = TREE_CHAIN (var), phi = PHI_CHAIN (phi))
    {
      tree result = TREE_PURPOSE (var);
      tree arg = TREE_VALUE (var);

      gcc_assert (result == PHI_RESULT (phi));

      add_phi_arg (phi, arg, new_edge);
    }

  PENDING_STMT (old_edge) = NULL;
}

/* Returns the basic block after which the new basic block created
   by splitting edge EDGE_IN should be placed.  Tries to keep the new block
   near its "logical" location.  This is of most help to humans looking
   at debugging dumps.  */

static basic_block
split_edge_bb_loc (edge edge_in)
{
  basic_block dest = edge_in->dest;

  if (dest->prev_bb && find_edge (dest->prev_bb, dest))
    return edge_in->src;
  else
    return dest->prev_bb;
}

/* Split a (typically critical) edge EDGE_IN.  Return the new block.
   Abort on abnormal edges.  */

static basic_block
tree_split_edge (edge edge_in)
{
  basic_block new_bb, after_bb, dest;
  edge new_edge, e;

  /* Abnormal edges cannot be split.  */
  gcc_assert (!(edge_in->flags & EDGE_ABNORMAL));

  dest = edge_in->dest;

  after_bb = split_edge_bb_loc (edge_in);

  new_bb = create_empty_bb (after_bb);
  new_bb->frequency = EDGE_FREQUENCY (edge_in);
  new_bb->count = edge_in->count;
  new_edge = make_edge (new_bb, dest, EDGE_FALLTHRU);
  new_edge->probability = REG_BR_PROB_BASE;
  new_edge->count = edge_in->count;

  e = redirect_edge_and_branch (edge_in, new_bb);
  gcc_assert (e);
  reinstall_phi_args (new_edge, e);

  return new_bb;
}


/* Return true when BB has label LABEL in it.  */

static bool
has_label_p (basic_block bb, tree label)
{
  block_stmt_iterator bsi;

  for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
    {
      tree stmt = bsi_stmt (bsi);

      if (TREE_CODE (stmt) != LABEL_EXPR)
	return false;
      if (LABEL_EXPR_LABEL (stmt) == label)
	return true;
    }
  return false;
}


/* Callback for walk_tree, check that all elements with address taken are
   properly noticed as such.  The DATA is an int* that is 1 if TP was seen
   inside a PHI node.  */

static tree
verify_expr (tree *tp, int *walk_subtrees, void *data ATTRIBUTE_UNUSED)
{
  tree t = *tp, x;
  bool in_phi = (data != NULL);

  if (TYPE_P (t))
    *walk_subtrees = 0;

  /* Check operand N for being valid GIMPLE and give error MSG if not.  */
#define CHECK_OP(N, MSG) \
  do { if (!is_gimple_val (TREE_OPERAND (t, N)))		\
       { error (MSG); return TREE_OPERAND (t, N); }} while (0)

  switch (TREE_CODE (t))
    {
    case SSA_NAME:
      if (SSA_NAME_IN_FREE_LIST (t))
	{
	  error ("SSA name in freelist but still referenced");
	  return *tp;
	}
      break;

    case ASSERT_EXPR:
      x = fold (ASSERT_EXPR_COND (t));
      if (x == boolean_false_node)
	{
	  error ("ASSERT_EXPR with an always-false condition");
	  return *tp;
	}
      break;

    case MODIFY_EXPR:
      x = TREE_OPERAND (t, 0);
      if (TREE_CODE (x) == BIT_FIELD_REF
	  && is_gimple_reg (TREE_OPERAND (x, 0)))
	{
	  error ("GIMPLE register modified with BIT_FIELD_REF");
	  return t;
	}
      break;

    case ADDR_EXPR:
      {
	bool old_invariant;
	bool old_constant;
	bool old_side_effects;
	bool new_invariant;
	bool new_constant;
	bool new_side_effects;

        /* ??? tree-ssa-alias.c may have overlooked dead PHI nodes, missing
	   dead PHIs that take the address of something.  But if the PHI
	   result is dead, the fact that it takes the address of anything
	   is irrelevant.  Because we can not tell from here if a PHI result
	   is dead, we just skip this check for PHIs altogether.  This means
	   we may be missing "valid" checks, but what can you do?
	   This was PR19217.  */
        if (in_phi)
	  break;

	old_invariant = TREE_INVARIANT (t);
	old_constant = TREE_CONSTANT (t);
	old_side_effects = TREE_SIDE_EFFECTS (t);

	recompute_tree_invariant_for_addr_expr (t);
	new_invariant = TREE_INVARIANT (t);
	new_side_effects = TREE_SIDE_EFFECTS (t);
	new_constant = TREE_CONSTANT (t);

	if (old_invariant != new_invariant)
	  {
	    error ("invariant not recomputed when ADDR_EXPR changed");
	    return t;
	  }

        if (old_constant != new_constant)
	  {
	    error ("constant not recomputed when ADDR_EXPR changed");
	    return t;
	  }
	if (old_side_effects != new_side_effects)
	  {
	    error ("side effects not recomputed when ADDR_EXPR changed");
	    return t;
	  }

	/* Skip any references (they will be checked when we recurse down the
	   tree) and ensure that any variable used as a prefix is marked
	   addressable.  */
	for (x = TREE_OPERAND (t, 0);
	     handled_component_p (x);
	     x = TREE_OPERAND (x, 0))
	  ;

	if (TREE_CODE (x) != VAR_DECL && TREE_CODE (x) != PARM_DECL)
	  return NULL;
	if (!TREE_ADDRESSABLE (x))
	  {
	    error ("address taken, but ADDRESSABLE bit not set");
	    return x;
	  }
	break;
      }

    case COND_EXPR:
      x = COND_EXPR_COND (t);
      if (TREE_CODE (TREE_TYPE (x)) != BOOLEAN_TYPE)
	{
	  error ("non-boolean used in condition");
	  return x;
	}
      if (!is_gimple_condexpr (x))
        {
	  error ("invalid conditional operand");
	  return x;
	}
      break;

    case NOP_EXPR:
    case CONVERT_EXPR:
    case FIX_TRUNC_EXPR:
    case FIX_CEIL_EXPR:
    case FIX_FLOOR_EXPR:
    case FIX_ROUND_EXPR:
    case FLOAT_EXPR:
    case NEGATE_EXPR:
    case ABS_EXPR:
    case BIT_NOT_EXPR:
    case NON_LVALUE_EXPR:
    case TRUTH_NOT_EXPR:
      CHECK_OP (0, "invalid operand to unary operator");
      break;

    case REALPART_EXPR:
    case IMAGPART_EXPR:
    case COMPONENT_REF:
    case ARRAY_REF:
    case ARRAY_RANGE_REF:
    case BIT_FIELD_REF:
    case VIEW_CONVERT_EXPR:
      /* We have a nest of references.  Verify that each of the operands
	 that determine where to reference is either a constant or a variable,
	 verify that the base is valid, and then show we've already checked
	 the subtrees.  */
      while (handled_component_p (t))
	{
	  if (TREE_CODE (t) == COMPONENT_REF && TREE_OPERAND (t, 2))
	    CHECK_OP (2, "invalid COMPONENT_REF offset operator");
	  else if (TREE_CODE (t) == ARRAY_REF
		   || TREE_CODE (t) == ARRAY_RANGE_REF)
	    {
	      CHECK_OP (1, "invalid array index");
	      if (TREE_OPERAND (t, 2))
		CHECK_OP (2, "invalid array lower bound");
	      if (TREE_OPERAND (t, 3))
		CHECK_OP (3, "invalid array stride");
	    }
	  else if (TREE_CODE (t) == BIT_FIELD_REF)
	    {
	      CHECK_OP (1, "invalid operand to BIT_FIELD_REF");
	      CHECK_OP (2, "invalid operand to BIT_FIELD_REF");
	    }

	  t = TREE_OPERAND (t, 0);
	}

      if (!CONSTANT_CLASS_P (t) && !is_gimple_lvalue (t))
	{
	  error ("invalid reference prefix");
	  return t;
	}
      *walk_subtrees = 0;
      break;

    case LT_EXPR:
    case LE_EXPR:
    case GT_EXPR:
    case GE_EXPR:
    case EQ_EXPR:
    case NE_EXPR:
    case UNORDERED_EXPR:
    case ORDERED_EXPR:
    case UNLT_EXPR:
    case UNLE_EXPR:
    case UNGT_EXPR:
    case UNGE_EXPR:
    case UNEQ_EXPR:
    case LTGT_EXPR:
    case PLUS_EXPR:
    case MINUS_EXPR:
    case MULT_EXPR:
    case TRUNC_DIV_EXPR:
    case CEIL_DIV_EXPR:
    case FLOOR_DIV_EXPR:
    case ROUND_DIV_EXPR:
    case TRUNC_MOD_EXPR:
    case CEIL_MOD_EXPR:
    case FLOOR_MOD_EXPR:
    case ROUND_MOD_EXPR:
    case RDIV_EXPR:
    case EXACT_DIV_EXPR:
    case MIN_EXPR:
    case MAX_EXPR:
    case LSHIFT_EXPR:
    case RSHIFT_EXPR:
    case LROTATE_EXPR:
    case RROTATE_EXPR:
    case BIT_IOR_EXPR:
    case BIT_XOR_EXPR:
    case BIT_AND_EXPR:
      CHECK_OP (0, "invalid operand to binary operator");
      CHECK_OP (1, "invalid operand to binary operator");
      break;

    case CONSTRUCTOR:
      if (TREE_CONSTANT (t) && TREE_CODE (TREE_TYPE (t)) == VECTOR_TYPE)
	*walk_subtrees = 0;
      break;

    default:
      break;
    }
  return NULL;

#undef CHECK_OP
}


/* Verify STMT, return true if STMT is not in GIMPLE form.
   TODO: Implement type checking.  */

static bool
verify_stmt (tree stmt, bool last_in_block)
{
  tree addr;

  if (OMP_DIRECTIVE_P (stmt))
    {
      /* OpenMP directives are validated by the FE and never operated
	 on by the optimizers.  Furthermore, OMP_FOR may contain
	 non-gimple expressions when the main index variable has had
	 its address taken.  This does not affect the loop itself
	 because the header of an OMP_FOR is merely used to determine
	 how to setup the parallel iteration.  */
      return false;
    }

  if (!is_gimple_stmt (stmt))
    {
      error ("is not a valid GIMPLE statement");
      goto fail;
    }

  addr = walk_tree (&stmt, verify_expr, NULL, NULL);
  if (addr)
    {
      debug_generic_stmt (addr);
      return true;
    }

  /* If the statement is marked as part of an EH region, then it is
     expected that the statement could throw.  Verify that when we
     have optimizations that simplify statements such that we prove
     that they cannot throw, that we update other data structures
     to match.  */
  if (lookup_stmt_eh_region (stmt) >= 0)
    {
      if (!tree_could_throw_p (stmt))
	{
	  error ("statement marked for throw, but doesn%'t");
	  goto fail;
	}
      if (!last_in_block && tree_can_throw_internal (stmt))
	{
	  error ("statement marked for throw in middle of block");
	  goto fail;
	}
    }

  return false;

 fail:
  debug_generic_stmt (stmt);
  return true;
}


/* Return true when the T can be shared.  */

static bool
tree_node_can_be_shared (tree t)
{
  if (IS_TYPE_OR_DECL_P (t)
      || is_gimple_min_invariant (t)
      || TREE_CODE (t) == SSA_NAME
      || t == error_mark_node
      || TREE_CODE (t) == IDENTIFIER_NODE)
    return true;

  if (TREE_CODE (t) == CASE_LABEL_EXPR)
    return true;

  while (((TREE_CODE (t) == ARRAY_REF || TREE_CODE (t) == ARRAY_RANGE_REF)
	   && is_gimple_min_invariant (TREE_OPERAND (t, 1)))
	 || TREE_CODE (t) == COMPONENT_REF
	 || TREE_CODE (t) == REALPART_EXPR
	 || TREE_CODE (t) == IMAGPART_EXPR)
    t = TREE_OPERAND (t, 0);

  if (DECL_P (t))
    return true;

  return false;
}


/* Called via walk_trees.  Verify tree sharing.  */

static tree
verify_node_sharing (tree * tp, int *walk_subtrees, void *data)
{
  htab_t htab = (htab_t) data;
  void **slot;

  if (tree_node_can_be_shared (*tp))
    {
      *walk_subtrees = false;
      return NULL;
    }

  slot = htab_find_slot (htab, *tp, INSERT);
  if (*slot)
    return (tree) *slot;
  *slot = *tp;

  return NULL;
}


/* Verify the GIMPLE statement chain.  */

void
verify_stmts (void)
{
  basic_block bb;
  block_stmt_iterator bsi;
  bool err = false;
  htab_t htab;
  tree addr;

  timevar_push (TV_TREE_STMT_VERIFY);
  htab = htab_create (37, htab_hash_pointer, htab_eq_pointer, NULL);

  FOR_EACH_BB (bb)
    {
      tree phi;
      int i;

      for (phi = phi_nodes (bb); phi; phi = PHI_CHAIN (phi))
	{
	  int phi_num_args = PHI_NUM_ARGS (phi);

	  if (bb_for_stmt (phi) != bb)
	    {
	      error ("bb_for_stmt (phi) is set to a wrong basic block");
	      err |= true;
	    }

	  for (i = 0; i < phi_num_args; i++)
	    {
	      tree t = PHI_ARG_DEF (phi, i);
	      tree addr;

	      /* Addressable variables do have SSA_NAMEs but they
		 are not considered gimple values.  */
	      if (TREE_CODE (t) != SSA_NAME
		  && TREE_CODE (t) != FUNCTION_DECL
		  && !is_gimple_val (t))
		{
		  error ("PHI def is not a GIMPLE value");
		  debug_generic_stmt (phi);
		  debug_generic_stmt (t);
		  err |= true;
		}

	      addr = walk_tree (&t, verify_expr, (void *) 1, NULL);
	      if (addr)
		{
		  debug_generic_stmt (addr);
		  err |= true;
		}

	      addr = walk_tree (&t, verify_node_sharing, htab, NULL);
	      if (addr)
		{
		  error ("incorrect sharing of tree nodes");
		  debug_generic_stmt (phi);
		  debug_generic_stmt (addr);
		  err |= true;
		}
	    }
	}

      for (bsi = bsi_start (bb); !bsi_end_p (bsi); )
	{
	  tree stmt = bsi_stmt (bsi);

	  if (bb_for_stmt (stmt) != bb)
	    {
	      error ("bb_for_stmt (stmt) is set to a wrong basic block");
	      err |= true;
	    }

	  bsi_next (&bsi);
	  err |= verify_stmt (stmt, bsi_end_p (bsi));
	  addr = walk_tree (&stmt, verify_node_sharing, htab, NULL);
	  if (addr)
	    {
	      error ("incorrect sharing of tree nodes");
	      debug_generic_stmt (stmt);
	      debug_generic_stmt (addr);
	      err |= true;
	    }
	}
    }

  if (err)
    internal_error ("verify_stmts failed");

  htab_delete (htab);
  timevar_pop (TV_TREE_STMT_VERIFY);
}


/* Verifies that the flow information is OK.  */

static int
tree_verify_flow_info (void)
{
  int err = 0;
  basic_block bb;
  block_stmt_iterator bsi;
  tree stmt;
  edge e;
  edge_iterator ei;

  if (ENTRY_BLOCK_PTR->stmt_list)
    {
      error ("ENTRY_BLOCK has a statement list associated with it");
      err = 1;
    }

  if (EXIT_BLOCK_PTR->stmt_list)
    {
      error ("EXIT_BLOCK has a statement list associated with it");
      err = 1;
    }

  FOR_EACH_EDGE (e, ei, EXIT_BLOCK_PTR->preds)
    if (e->flags & EDGE_FALLTHRU)
      {
	error ("fallthru to exit from bb %d", e->src->index);
	err = 1;
      }

  FOR_EACH_BB (bb)
    {
      bool found_ctrl_stmt = false;

      stmt = NULL_TREE;

      /* Skip labels on the start of basic block.  */
      for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
	{
	  tree prev_stmt = stmt;

	  stmt = bsi_stmt (bsi);

	  if (TREE_CODE (stmt) != LABEL_EXPR)
	    break;

	  if (prev_stmt && DECL_NONLOCAL (LABEL_EXPR_LABEL (stmt)))
	    {
	      error ("nonlocal label ");
	      print_generic_expr (stderr, LABEL_EXPR_LABEL (stmt), 0);
	      fprintf (stderr, " is not first in a sequence of labels in bb %d",
		       bb->index);
	      err = 1;
	    }

	  if (label_to_block (LABEL_EXPR_LABEL (stmt)) != bb)
	    {
	      error ("label ");
	      print_generic_expr (stderr, LABEL_EXPR_LABEL (stmt), 0);
	      fprintf (stderr, " to block does not match in bb %d",
		       bb->index);
	      err = 1;
	    }

	  if (decl_function_context (LABEL_EXPR_LABEL (stmt))
	      != current_function_decl)
	    {
	      error ("label ");
	      print_generic_expr (stderr, LABEL_EXPR_LABEL (stmt), 0);
	      fprintf (stderr, " has incorrect context in bb %d",
		       bb->index);
	      err = 1;
	    }
	}

      /* Verify that body of basic block BB is free of control flow.  */
      for (; !bsi_end_p (bsi); bsi_next (&bsi))
	{
	  tree stmt = bsi_stmt (bsi);

	  if (found_ctrl_stmt)
	    {
	      error ("control flow in the middle of basic block %d",
		     bb->index);
	      err = 1;
	    }

	  if (stmt_ends_bb_p (stmt))
	    found_ctrl_stmt = true;

	  if (TREE_CODE (stmt) == LABEL_EXPR)
	    {
	      error ("label ");
	      print_generic_expr (stderr, LABEL_EXPR_LABEL (stmt), 0);
	      fprintf (stderr, " in the middle of basic block %d", bb->index);
	      err = 1;
	    }
	}

      bsi = bsi_last (bb);
      if (bsi_end_p (bsi))
	continue;

      stmt = bsi_stmt (bsi);

      err |= verify_eh_edges (stmt);

      if (is_ctrl_stmt (stmt))
	{
	  FOR_EACH_EDGE (e, ei, bb->succs)
	    if (e->flags & EDGE_FALLTHRU)
	      {
		error ("fallthru edge after a control statement in bb %d",
		       bb->index);
		err = 1;
	      }
	}

      if (TREE_CODE (stmt) != COND_EXPR)
	{
	  /* Verify that there are no edges with EDGE_TRUE/FALSE_FLAG set
	     after anything else but if statement.  */
	  FOR_EACH_EDGE (e, ei, bb->succs)
	    if (e->flags & (EDGE_TRUE_VALUE | EDGE_FALSE_VALUE))
	      {
		error ("true/false edge after a non-COND_EXPR in bb %d",
		       bb->index);
		err = 1;
	      }
	}

      switch (TREE_CODE (stmt))
	{
	case COND_EXPR:
	  {
	    edge true_edge;
	    edge false_edge;
	    if (TREE_CODE (COND_EXPR_THEN (stmt)) != GOTO_EXPR
		|| TREE_CODE (COND_EXPR_ELSE (stmt)) != GOTO_EXPR)
	      {
		error ("structured COND_EXPR at the end of bb %d", bb->index);
		err = 1;
	      }

	    extract_true_false_edges_from_block (bb, &true_edge, &false_edge);

	    if (!true_edge || !false_edge
		|| !(true_edge->flags & EDGE_TRUE_VALUE)
		|| !(false_edge->flags & EDGE_FALSE_VALUE)
		|| (true_edge->flags & (EDGE_FALLTHRU | EDGE_ABNORMAL))
		|| (false_edge->flags & (EDGE_FALLTHRU | EDGE_ABNORMAL))
		|| EDGE_COUNT (bb->succs) >= 3)
	      {
		error ("wrong outgoing edge flags at end of bb %d",
		       bb->index);
		err = 1;
	      }

	    if (!has_label_p (true_edge->dest,
			      GOTO_DESTINATION (COND_EXPR_THEN (stmt))))
	      {
		error ("%<then%> label does not match edge at end of bb %d",
		       bb->index);
		err = 1;
	      }

	    if (!has_label_p (false_edge->dest,
			      GOTO_DESTINATION (COND_EXPR_ELSE (stmt))))
	      {
		error ("%<else%> label does not match edge at end of bb %d",
		       bb->index);
		err = 1;
	      }
	  }
	  break;

	case GOTO_EXPR:
	  if (simple_goto_p (stmt))
	    {
	      error ("explicit goto at end of bb %d", bb->index);
	      err = 1;
	    }
	  else
	    {
	      /* FIXME.  We should double check that the labels in the
		 destination blocks have their address taken.  */
	      FOR_EACH_EDGE (e, ei, bb->succs)
		if ((e->flags & (EDGE_FALLTHRU | EDGE_TRUE_VALUE
				 | EDGE_FALSE_VALUE))
		    || !(e->flags & EDGE_ABNORMAL))
		  {
		    error ("wrong outgoing edge flags at end of bb %d",
			   bb->index);
		    err = 1;
		  }
	    }
	  break;

	case RETURN_EXPR:
	  if (!single_succ_p (bb)
	      || (single_succ_edge (bb)->flags
		  & (EDGE_FALLTHRU | EDGE_ABNORMAL
		     | EDGE_TRUE_VALUE | EDGE_FALSE_VALUE)))
	    {
	      error ("wrong outgoing edge flags at end of bb %d", bb->index);
	      err = 1;
	    }
	  if (single_succ (bb) != EXIT_BLOCK_PTR)
	    {
	      error ("return edge does not point to exit in bb %d",
		     bb->index);
	      err = 1;
	    }
	  break;

	case SWITCH_EXPR:
	  {
	    tree prev;
	    edge e;
	    size_t i, n;
	    tree vec;

	    vec = SWITCH_LABELS (stmt);
	    n = TREE_VEC_LENGTH (vec);

	    /* Mark all the destination basic blocks.  */
	    for (i = 0; i < n; ++i)
	      {
		tree lab = CASE_LABEL (TREE_VEC_ELT (vec, i));
		basic_block label_bb = label_to_block (lab);

		gcc_assert (!label_bb->aux || label_bb->aux == (void *)1);
		label_bb->aux = (void *)1;
	      }

	    /* Verify that the case labels are sorted.  */
	    prev = TREE_VEC_ELT (vec, 0);
	    for (i = 1; i < n - 1; ++i)
	      {
		tree c = TREE_VEC_ELT (vec, i);
		if (! CASE_LOW (c))
		  {
		    error ("found default case not at end of case vector");
		    err = 1;
		    continue;
		  }
		if (! tree_int_cst_lt (CASE_LOW (prev), CASE_LOW (c)))
		  {
		    error ("case labels not sorted: ");
		    print_generic_expr (stderr, prev, 0);
		    fprintf (stderr," is greater than ");
		    print_generic_expr (stderr, c, 0);
		    fprintf (stderr," but comes before it.\n");
		    err = 1;
		  }
		prev = c;
	      }
	    if (CASE_LOW (TREE_VEC_ELT (vec, n - 1)))
	      {
		error ("no default case found at end of case vector");
		err = 1;
	      }

	    FOR_EACH_EDGE (e, ei, bb->succs)
	      {
		if (!e->dest->aux)
		  {
		    error ("extra outgoing edge %d->%d",
			   bb->index, e->dest->index);
		    err = 1;
		  }
		e->dest->aux = (void *)2;
		if ((e->flags & (EDGE_FALLTHRU | EDGE_ABNORMAL
				 | EDGE_TRUE_VALUE | EDGE_FALSE_VALUE)))
		  {
		    error ("wrong outgoing edge flags at end of bb %d",
			   bb->index);
		    err = 1;
		  }
	      }

	    /* Check that we have all of them.  */
	    for (i = 0; i < n; ++i)
	      {
		tree lab = CASE_LABEL (TREE_VEC_ELT (vec, i));
		basic_block label_bb = label_to_block (lab);

		if (label_bb->aux != (void *)2)
		  {
		    error ("missing edge %i->%i",
			   bb->index, label_bb->index);
		    err = 1;
		  }
	      }

	    FOR_EACH_EDGE (e, ei, bb->succs)
	      e->dest->aux = (void *)0;
	  }

	default: ;
	}
    }

  if (dom_computed[CDI_DOMINATORS] >= DOM_NO_FAST_QUERY)
    verify_dominators (CDI_DOMINATORS);

  return err;
}


/* Updates phi nodes after creating a forwarder block joined
   by edge FALLTHRU.  */

static void
tree_make_forwarder_block (edge fallthru)
{
  edge e;
  edge_iterator ei;
  basic_block dummy, bb;
  tree phi, new_phi, var;

  dummy = fallthru->src;
  bb = fallthru->dest;

  if (single_pred_p (bb))
    return;

  /* If we redirected a branch we must create new phi nodes at the
     start of BB.  */
  for (phi = phi_nodes (dummy); phi; phi = PHI_CHAIN (phi))
    {
      var = PHI_RESULT (phi);
      new_phi = create_phi_node (var, bb);
      SSA_NAME_DEF_STMT (var) = new_phi;
      SET_PHI_RESULT (phi, make_ssa_name (SSA_NAME_VAR (var), phi));
      add_phi_arg (new_phi, PHI_RESULT (phi), fallthru);
    }

  /* Ensure that the PHI node chain is in the same order.  */
  set_phi_nodes (bb, phi_reverse (phi_nodes (bb)));

  /* Add the arguments we have stored on edges.  */
  FOR_EACH_EDGE (e, ei, bb->preds)
    {
      if (e == fallthru)
	continue;

      flush_pending_stmts (e);
    }
}


/* Return a non-special label in the head of basic block BLOCK.
   Create one if it doesn't exist.  */

tree
tree_block_label (basic_block bb)
{
  block_stmt_iterator i, s = bsi_start (bb);
  bool first = true;
  tree label, stmt;

  for (i = s; !bsi_end_p (i); first = false, bsi_next (&i))
    {
      stmt = bsi_stmt (i);
      if (TREE_CODE (stmt) != LABEL_EXPR)
	break;
      label = LABEL_EXPR_LABEL (stmt);
      if (!DECL_NONLOCAL (label))
	{
	  if (!first)
	    bsi_move_before (&i, &s);
	  return label;
	}
    }

  label = create_artificial_label ();
  stmt = build1 (LABEL_EXPR, void_type_node, label);
  bsi_insert_before (&s, stmt, BSI_NEW_STMT);
  return label;
}


/* Attempt to perform edge redirection by replacing a possibly complex
   jump instruction by a goto or by removing the jump completely.
   This can apply only if all edges now point to the same block.  The
   parameters and return values are equivalent to
   redirect_edge_and_branch.  */

static edge
tree_try_redirect_by_replacing_jump (edge e, basic_block target)
{
  basic_block src = e->src;
  block_stmt_iterator b;
  tree stmt;

  /* We can replace or remove a complex jump only when we have exactly
     two edges.  */
  if (EDGE_COUNT (src->succs) != 2
      /* Verify that all targets will be TARGET.  Specifically, the
	 edge that is not E must also go to TARGET.  */
      || EDGE_SUCC (src, EDGE_SUCC (src, 0) == e)->dest != target)
    return NULL;

  b = bsi_last (src);
  if (bsi_end_p (b))
    return NULL;
  stmt = bsi_stmt (b);

  if (TREE_CODE (stmt) == COND_EXPR
      || TREE_CODE (stmt) == SWITCH_EXPR)
    {
      bsi_remove (&b, true);
      e = ssa_redirect_edge (e, target);
      e->flags = EDGE_FALLTHRU;
      return e;
    }

  return NULL;
}


/* Redirect E to DEST.  Return NULL on failure.  Otherwise, return the
   edge representing the redirected branch.  */

static edge
tree_redirect_edge_and_branch (edge e, basic_block dest)
{
  basic_block bb = e->src;
  block_stmt_iterator bsi;
  edge ret;
  tree label, stmt;

  if (e->flags & EDGE_ABNORMAL)
    return NULL;

  if (e->src != ENTRY_BLOCK_PTR
      && (ret = tree_try_redirect_by_replacing_jump (e, dest)))
    return ret;

  if (e->dest == dest)
    return NULL;

  label = tree_block_label (dest);

  bsi = bsi_last (bb);
  stmt = bsi_end_p (bsi) ? NULL : bsi_stmt (bsi);

  switch (stmt ? TREE_CODE (stmt) : ERROR_MARK)
    {
    case COND_EXPR:
      stmt = (e->flags & EDGE_TRUE_VALUE
	      ? COND_EXPR_THEN (stmt)
	      : COND_EXPR_ELSE (stmt));
      GOTO_DESTINATION (stmt) = label;
      break;

    case GOTO_EXPR:
      /* No non-abnormal edges should lead from a non-simple goto, and
	 simple ones should be represented implicitly.  */
      gcc_unreachable ();

    case SWITCH_EXPR:
      {
        tree cases = get_cases_for_edge (e, stmt);

	/* If we have a list of cases associated with E, then use it
	   as it's a lot faster than walking the entire case vector.  */
	if (cases)
	  {
	    edge e2 = find_edge (e->src, dest);
	    tree last, first;

	    first = cases;
	    while (cases)
	      {
		last = cases;
		CASE_LABEL (cases) = label;
		cases = TREE_CHAIN (cases);
	      }

	    /* If there was already an edge in the CFG, then we need
	       to move all the cases associated with E to E2.  */
	    if (e2)
	      {
		tree cases2 = get_cases_for_edge (e2, stmt);

		TREE_CHAIN (last) = TREE_CHAIN (cases2);
		TREE_CHAIN (cases2) = first;
	      }
	  }
	else
	  {
	    tree vec = SWITCH_LABELS (stmt);
	    size_t i, n = TREE_VEC_LENGTH (vec);

	    for (i = 0; i < n; i++)
	      {
		tree elt = TREE_VEC_ELT (vec, i);

		if (label_to_block (CASE_LABEL (elt)) == e->dest)
		  CASE_LABEL (elt) = label;
	      }
	  }

	break;
      }

    case RETURN_EXPR:
      bsi_remove (&bsi, true);
      e->flags |= EDGE_FALLTHRU;
      break;

    default:
      /* Otherwise it must be a fallthru edge, and we don't need to
	 do anything besides redirecting it.  */
      gcc_assert (e->flags & EDGE_FALLTHRU);
      break;
    }

  /* Update/insert PHI nodes as necessary.  */

  /* Now update the edges in the CFG.  */
  e = ssa_redirect_edge (e, dest);

  return e;
}


/* Simple wrapper, as we can always redirect fallthru edges.  */

static basic_block
tree_redirect_edge_and_branch_force (edge e, basic_block dest)
{
  e = tree_redirect_edge_and_branch (e, dest);
  gcc_assert (e);

  return NULL;
}


/* Splits basic block BB after statement STMT (but at least after the
   labels).  If STMT is NULL, BB is split just after the labels.  */

static basic_block
tree_split_block (basic_block bb, void *stmt)
{
  block_stmt_iterator bsi;
  tree_stmt_iterator tsi_tgt;
  tree act;
  basic_block new_bb;
  edge e;
  edge_iterator ei;

  new_bb = create_empty_bb (bb);

  /* Redirect the outgoing edges.  */
  new_bb->succs = bb->succs;
  bb->succs = NULL;
  FOR_EACH_EDGE (e, ei, new_bb->succs)
    e->src = new_bb;

  if (stmt && TREE_CODE ((tree) stmt) == LABEL_EXPR)
    stmt = NULL;

  /* Move everything from BSI to the new basic block.  */
  for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
    {
      act = bsi_stmt (bsi);
      if (TREE_CODE (act) == LABEL_EXPR)
	continue;

      if (!stmt)
	break;

      if (stmt == act)
	{
	  bsi_next (&bsi);
	  break;
	}
    }

  if (bsi_end_p (bsi))
    return new_bb;

  /* Split the statement list - avoid re-creating new containers as this
     brings ugly quadratic memory consumption in the inliner.  
     (We are still quadratic since we need to update stmt BB pointers,
     sadly.)  */
  new_bb->stmt_list = tsi_split_statement_list_before (&bsi.tsi);
  for (tsi_tgt = tsi_start (new_bb->stmt_list);
       !tsi_end_p (tsi_tgt); tsi_next (&tsi_tgt))
    change_bb_for_stmt (tsi_stmt (tsi_tgt), new_bb);

  return new_bb;
}


/* Moves basic block BB after block AFTER.  */

static bool
tree_move_block_after (basic_block bb, basic_block after)
{
  if (bb->prev_bb == after)
    return true;

  unlink_block (bb);
  link_block (bb, after);

  return true;
}


/* Return true if basic_block can be duplicated.  */

static bool
tree_can_duplicate_bb_p (basic_block bb ATTRIBUTE_UNUSED)
{
  return true;
}


/* Create a duplicate of the basic block BB.  NOTE: This does not
   preserve SSA form.  */

static basic_block
tree_duplicate_bb (basic_block bb)
{
  basic_block new_bb;
  block_stmt_iterator bsi, bsi_tgt;
  tree phi;

  new_bb = create_empty_bb (EXIT_BLOCK_PTR->prev_bb);

  /* Copy the PHI nodes.  We ignore PHI node arguments here because
     the incoming edges have not been setup yet.  */
  for (phi = phi_nodes (bb); phi; phi = PHI_CHAIN (phi))
    {
      tree copy = create_phi_node (PHI_RESULT (phi), new_bb);
      create_new_def_for (PHI_RESULT (copy), copy, PHI_RESULT_PTR (copy));
    }

  /* Keep the chain of PHI nodes in the same order so that they can be
     updated by ssa_redirect_edge.  */
  set_phi_nodes (new_bb, phi_reverse (phi_nodes (new_bb)));

  bsi_tgt = bsi_start (new_bb);
  for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
    {
      def_operand_p def_p;
      ssa_op_iter op_iter;
      tree stmt, copy;
      int region;

      stmt = bsi_stmt (bsi);
      if (TREE_CODE (stmt) == LABEL_EXPR)
	continue;

      /* Create a new copy of STMT and duplicate STMT's virtual
	 operands.  */
      copy = unshare_expr (stmt);
      bsi_insert_after (&bsi_tgt, copy, BSI_NEW_STMT);
      copy_virtual_operands (copy, stmt);
      region = lookup_stmt_eh_region (stmt);
      if (region >= 0)
	add_stmt_to_eh_region (copy, region);

      /* Create new names for all the definitions created by COPY and
	 add replacement mappings for each new name.  */
      FOR_EACH_SSA_DEF_OPERAND (def_p, copy, op_iter, SSA_OP_ALL_DEFS)
	create_new_def_for (DEF_FROM_PTR (def_p), copy, def_p);
    }

  return new_bb;
}


/* Basic block BB_COPY was created by code duplication.  Add phi node
   arguments for edges going out of BB_COPY.  The blocks that were
   duplicated have BB_DUPLICATED set.  */

void
add_phi_args_after_copy_bb (basic_block bb_copy)
{
  basic_block bb, dest;
  edge e, e_copy;
  edge_iterator ei;
  tree phi, phi_copy, phi_next, def;

  bb = get_bb_original (bb_copy);

  FOR_EACH_EDGE (e_copy, ei, bb_copy->succs)
    {
      if (!phi_nodes (e_copy->dest))
	continue;

      if (e_copy->dest->flags & BB_DUPLICATED)
	dest = get_bb_original (e_copy->dest);
      else
	dest = e_copy->dest;

      e = find_edge (bb, dest);
      if (!e)
	{
	  /* During loop unrolling the target of the latch edge is copied.
	     In this case we are not looking for edge to dest, but to
	     duplicated block whose original was dest.  */
	  FOR_EACH_EDGE (e, ei, bb->succs)
	    if ((e->dest->flags & BB_DUPLICATED)
		&& get_bb_original (e->dest) == dest)
	      break;

	  gcc_assert (e != NULL);
	}

      for (phi = phi_nodes (e->dest), phi_copy = phi_nodes (e_copy->dest);
	   phi;
	   phi = phi_next, phi_copy = PHI_CHAIN (phi_copy))
	{
	  phi_next = PHI_CHAIN (phi);
	  def = PHI_ARG_DEF_FROM_EDGE (phi, e);
	  add_phi_arg (phi_copy, def, e_copy);
	}
    }
}

/* Blocks in REGION_COPY array of length N_REGION were created by
   duplication of basic blocks.  Add phi node arguments for edges
   going from these blocks.  */

void
add_phi_args_after_copy (basic_block *region_copy, unsigned n_region)
{
  unsigned i;

  for (i = 0; i < n_region; i++)
    region_copy[i]->flags |= BB_DUPLICATED;

  for (i = 0; i < n_region; i++)
    add_phi_args_after_copy_bb (region_copy[i]);

  for (i = 0; i < n_region; i++)
    region_copy[i]->flags &= ~BB_DUPLICATED;
}

/* Duplicates a REGION (set of N_REGION basic blocks) with just a single
   important exit edge EXIT.  By important we mean that no SSA name defined
   inside region is live over the other exit edges of the region.  All entry
   edges to the region must go to ENTRY->dest.  The edge ENTRY is redirected
   to the duplicate of the region.  SSA form, dominance and loop information
   is updated.  The new basic blocks are stored to REGION_COPY in the same
   order as they had in REGION, provided that REGION_COPY is not NULL.
   The function returns false if it is unable to copy the region,
   true otherwise.  */

bool
tree_duplicate_sese_region (edge entry, edge exit,
			    basic_block *region, unsigned n_region,
			    basic_block *region_copy)
{
  unsigned i, n_doms;
  bool free_region_copy = false, copying_header = false;
  struct loop *loop = entry->dest->loop_father;
  edge exit_copy;
  basic_block *doms;
  edge redirected;
  int total_freq = 0, entry_freq = 0;
  gcov_type total_count = 0, entry_count = 0;

  if (!can_copy_bbs_p (region, n_region))
    return false;

  /* Some sanity checking.  Note that we do not check for all possible
     missuses of the functions.  I.e. if you ask to copy something weird,
     it will work, but the state of structures probably will not be
     correct.  */
  for (i = 0; i < n_region; i++)
    {
      /* We do not handle subloops, i.e. all the blocks must belong to the
	 same loop.  */
      if (region[i]->loop_father != loop)
	return false;

      if (region[i] != entry->dest
	  && region[i] == loop->header)
	return false;
    }

  loop->copy = loop;

  /* In case the function is used for loop header copying (which is the primary
     use), ensure that EXIT and its copy will be new latch and entry edges.  */
  if (loop->header == entry->dest)
    {
      copying_header = true;
      loop->copy = loop->outer;

      if (!dominated_by_p (CDI_DOMINATORS, loop->latch, exit->src))
	return false;

      for (i = 0; i < n_region; i++)
	if (region[i] != exit->src
	    && dominated_by_p (CDI_DOMINATORS, region[i], exit->src))
	  return false;
    }

  if (!region_copy)
    {
      region_copy = XNEWVEC (basic_block, n_region);
      free_region_copy = true;
    }

  gcc_assert (!need_ssa_update_p ());

  /* Record blocks outside the region that are dominated by something
     inside.  */
  doms = XNEWVEC (basic_block, n_basic_blocks);
  initialize_original_copy_tables ();

  n_doms = get_dominated_by_region (CDI_DOMINATORS, region, n_region, doms);

  if (entry->dest->count)
    {
      total_count = entry->dest->count;
      entry_count = entry->count;
      /* Fix up corner cases, to avoid division by zero or creation of negative
	 frequencies.  */
      if (entry_count > total_count)
	entry_count = total_count;
    }
  else
    {
      total_freq = entry->dest->frequency;
      entry_freq = EDGE_FREQUENCY (entry);
      /* Fix up corner cases, to avoid division by zero or creation of negative
	 frequencies.  */
      if (total_freq == 0)
	total_freq = 1;
      else if (entry_freq > total_freq)
	entry_freq = total_freq;
    }

  copy_bbs (region, n_region, region_copy, &exit, 1, &exit_copy, loop,
	    split_edge_bb_loc (entry));
  if (total_count)
    {
      scale_bbs_frequencies_gcov_type (region, n_region,
				       total_count - entry_count,
				       total_count);
      scale_bbs_frequencies_gcov_type (region_copy, n_region, entry_count,
				       total_count);
    }
  else
    {
      scale_bbs_frequencies_int (region, n_region, total_freq - entry_freq,
				 total_freq);
      scale_bbs_frequencies_int (region_copy, n_region, entry_freq, total_freq);
    }

  if (copying_header)
    {
      loop->header = exit->dest;
      loop->latch = exit->src;
    }

  /* Redirect the entry and add the phi node arguments.  */
  redirected = redirect_edge_and_branch (entry, get_bb_copy (entry->dest));
  gcc_assert (redirected != NULL);
  flush_pending_stmts (entry);

  /* Concerning updating of dominators:  We must recount dominators
     for entry block and its copy.  Anything that is outside of the
     region, but was dominated by something inside needs recounting as
     well.  */
  set_immediate_dominator (CDI_DOMINATORS, entry->dest, entry->src);
  doms[n_doms++] = get_bb_original (entry->dest);
  iterate_fix_dominators (CDI_DOMINATORS, doms, n_doms);
  free (doms);

  /* Add the other PHI node arguments.  */
  add_phi_args_after_copy (region_copy, n_region);

  /* Update the SSA web.  */
  update_ssa (TODO_update_ssa);

  if (free_region_copy)
    free (region_copy);

  free_original_copy_tables ();
  return true;
}

/*
DEF_VEC_P(basic_block);
DEF_VEC_ALLOC_P(basic_block,heap);
*/

/* Add all the blocks dominated by ENTRY to the array BBS_P.  Stop
   adding blocks when the dominator traversal reaches EXIT.  This
   function silently assumes that ENTRY strictly dominates EXIT.  */

static void
gather_blocks_in_sese_region (basic_block entry, basic_block exit,
			      VEC(basic_block,heap) **bbs_p)
{
  basic_block son;

  for (son = first_dom_son (CDI_DOMINATORS, entry);
       son;
       son = next_dom_son (CDI_DOMINATORS, son))
    {
      VEC_safe_push (basic_block, heap, *bbs_p, son);
      if (son != exit)
	gather_blocks_in_sese_region (son, exit, bbs_p);
    }
}


struct move_stmt_d
{
  tree block;
  tree from_context;
  tree to_context;
  bitmap vars_to_remove;
  htab_t new_label_map;
  bool remap_decls_p;
};

/* Helper for move_block_to_fn.  Set TREE_BLOCK in every expression
   contained in *TP and change the DECL_CONTEXT of every local
   variable referenced in *TP.  */

static tree
move_stmt_r (tree *tp, int *walk_subtrees, void *data)
{
  struct move_stmt_d *p = (struct move_stmt_d *) data;
  tree t = *tp;

  if (p->block && IS_EXPR_CODE_CLASS (TREE_CODE_CLASS (TREE_CODE (t))))
    TREE_BLOCK (t) = p->block;

  if (OMP_DIRECTIVE_P (t)
      && TREE_CODE (t) != OMP_RETURN
      && TREE_CODE (t) != OMP_CONTINUE)
    {
      /* Do not remap variables inside OMP directives.  Variables
	 referenced in clauses and directive header belong to the
	 parent function and should not be moved into the child
	 function.  */
      bool save_remap_decls_p = p->remap_decls_p;
      p->remap_decls_p = false;
      *walk_subtrees = 0;

      walk_tree (&OMP_BODY (t), move_stmt_r, p, NULL);

      p->remap_decls_p = save_remap_decls_p;
    }
  else if (DECL_P (t) && DECL_CONTEXT (t) == p->from_context)
    {
      if (TREE_CODE (t) == LABEL_DECL)
	{
	  if (p->new_label_map)
	    {
	      struct tree_map in, *out;
	      in.from = t;
	      out = htab_find_with_hash (p->new_label_map, &in, DECL_UID (t));
	      if (out)
		*tp = t = out->to;
	    }

	  DECL_CONTEXT (t) = p->to_context;
	}
      else if (p->remap_decls_p)
	{
	  DECL_CONTEXT (t) = p->to_context;

	  if (TREE_CODE (t) == VAR_DECL)
	    {
	      struct function *f = DECL_STRUCT_FUNCTION (p->to_context);
	      f->unexpanded_var_list
		= tree_cons (0, t, f->unexpanded_var_list);

	      /* Mark T to be removed from the original function,
	         otherwise it will be given a DECL_RTL when the
		 original function is expanded.  */
	      bitmap_set_bit (p->vars_to_remove, DECL_UID (t));
	    }
	}
    }
  else if (TYPE_P (t))
    *walk_subtrees = 0;

  return NULL_TREE;
}


/* Move basic block BB from function CFUN to function DEST_FN.  The
   block is moved out of the original linked list and placed after
   block AFTER in the new list.  Also, the block is removed from the
   original array of blocks and placed in DEST_FN's array of blocks.
   If UPDATE_EDGE_COUNT_P is true, the edge counts on both CFGs is
   updated to reflect the moved edges.

   On exit, local variables that need to be removed from
   CFUN->UNEXPANDED_VAR_LIST will have been added to VARS_TO_REMOVE.  */

static void
move_block_to_fn (struct function *dest_cfun, basic_block bb,
		  basic_block after, bool update_edge_count_p,
		  bitmap vars_to_remove, htab_t new_label_map, int eh_offset)
{
  struct control_flow_graph *cfg;
  edge_iterator ei;
  edge e;
  block_stmt_iterator si;
  struct move_stmt_d d;
  unsigned old_len, new_len;
  basic_block *addr;

  /* Link BB to the new linked list.  */
  move_block_after (bb, after);

  /* Update the edge count in the corresponding flowgraphs.  */
  if (update_edge_count_p)
    FOR_EACH_EDGE (e, ei, bb->succs)
      {
	cfun->cfg->x_n_edges--;
	dest_cfun->cfg->x_n_edges++;
      }

  /* Remove BB from the original basic block array.  */
  VEC_replace (basic_block, cfun->cfg->x_basic_block_info, bb->index, NULL);
  cfun->cfg->x_n_basic_blocks--;

  /* Grow DEST_CFUN's basic block array if needed.  */
  cfg = dest_cfun->cfg;
  cfg->x_n_basic_blocks++;
  if (bb->index > cfg->x_last_basic_block)
    cfg->x_last_basic_block = bb->index;

  old_len = VEC_length (basic_block, cfg->x_basic_block_info);
  if ((unsigned) cfg->x_last_basic_block >= old_len)
    {
      new_len = cfg->x_last_basic_block + (cfg->x_last_basic_block + 3) / 4;
      VEC_safe_grow (basic_block, gc, cfg->x_basic_block_info, new_len);
      addr = VEC_address (basic_block, cfg->x_basic_block_info);
      memset (&addr[old_len], 0, sizeof (basic_block) * (new_len - old_len));
    }

  VEC_replace (basic_block, cfg->x_basic_block_info,
               cfg->x_last_basic_block, bb);

  /* The statements in BB need to be associated with a new TREE_BLOCK.
     Labels need to be associated with a new label-to-block map.  */
  memset (&d, 0, sizeof (d));
  d.vars_to_remove = vars_to_remove;

  for (si = bsi_start (bb); !bsi_end_p (si); bsi_next (&si))
    {
      tree stmt = bsi_stmt (si);
      int region;

      d.from_context = cfun->decl;
      d.to_context = dest_cfun->decl;
      d.remap_decls_p = true;
      d.new_label_map = new_label_map;
      if (TREE_BLOCK (stmt))
	d.block = DECL_INITIAL (dest_cfun->decl);

      walk_tree (&stmt, move_stmt_r, &d, NULL);

      if (TREE_CODE (stmt) == LABEL_EXPR)
	{
	  tree label = LABEL_EXPR_LABEL (stmt);
	  int uid = LABEL_DECL_UID (label);

	  gcc_assert (uid > -1);

	  old_len = VEC_length (basic_block, cfg->x_label_to_block_map);
	  if (old_len <= (unsigned) uid)
	    {
	      new_len = 3 * uid / 2;
	      VEC_safe_grow (basic_block, gc, cfg->x_label_to_block_map,
			     new_len);
	      addr = VEC_address (basic_block, cfg->x_label_to_block_map);
	      memset (&addr[old_len], 0,
		      sizeof (basic_block) * (new_len - old_len));
	    }

	  VEC_replace (basic_block, cfg->x_label_to_block_map, uid, bb);
	  VEC_replace (basic_block, cfun->cfg->x_label_to_block_map, uid, NULL);

	  gcc_assert (DECL_CONTEXT (label) == dest_cfun->decl);

	  if (uid >= dest_cfun->last_label_uid)
	    dest_cfun->last_label_uid = uid + 1;
	}
      else if (TREE_CODE (stmt) == RESX_EXPR && eh_offset != 0)
	TREE_OPERAND (stmt, 0) =
	  build_int_cst (NULL_TREE,
			 TREE_INT_CST_LOW (TREE_OPERAND (stmt, 0))
			 + eh_offset);

      region = lookup_stmt_eh_region (stmt);
      if (region >= 0)
	{
	  add_stmt_to_eh_region_fn (dest_cfun, stmt, region + eh_offset);
	  remove_stmt_from_eh_region (stmt);
	}
    }
}

/* Examine the statements in BB (which is in SRC_CFUN); find and return
   the outermost EH region.  Use REGION as the incoming base EH region.  */

static int
find_outermost_region_in_block (struct function *src_cfun,
				basic_block bb, int region)
{
  block_stmt_iterator si;

  for (si = bsi_start (bb); !bsi_end_p (si); bsi_next (&si))
    {
      tree stmt = bsi_stmt (si);
      int stmt_region;

      if (TREE_CODE (stmt) == RESX_EXPR)
	stmt_region = TREE_INT_CST_LOW (TREE_OPERAND (stmt, 0));
      else
	stmt_region = lookup_stmt_eh_region_fn (src_cfun, stmt);
      if (stmt_region > 0)
	{
	  if (region < 0)
	    region = stmt_region;
	  else if (stmt_region != region)
	    {
	      region = eh_region_outermost (src_cfun, stmt_region, region);
	      gcc_assert (region != -1);
	    }
	}
    }

  return region;
}

static tree
new_label_mapper (tree decl, void *data)
{
  htab_t hash = (htab_t) data;
  struct tree_map *m;
  void **slot;

  gcc_assert (TREE_CODE (decl) == LABEL_DECL);

  m = xmalloc (sizeof (struct tree_map));
  m->hash = DECL_UID (decl);
  m->from = decl;
  m->to = create_artificial_label ();
  LABEL_DECL_UID (m->to) = LABEL_DECL_UID (decl);

  slot = htab_find_slot_with_hash (hash, m, m->hash, INSERT);
  gcc_assert (*slot == NULL);

  *slot = m;

  return m->to;
}

/* Move a single-entry, single-exit region delimited by ENTRY_BB and
   EXIT_BB to function DEST_CFUN.  The whole region is replaced by a
   single basic block in the original CFG and the new basic block is
   returned.  DEST_CFUN must not have a CFG yet.

   Note that the region need not be a pure SESE region.  Blocks inside
   the region may contain calls to abort/exit.  The only restriction
   is that ENTRY_BB should be the only entry point and it must
   dominate EXIT_BB.

   All local variables referenced in the region are assumed to be in
   the corresponding BLOCK_VARS and unexpanded variable lists
   associated with DEST_CFUN.  */

basic_block
move_sese_region_to_fn (struct function *dest_cfun, basic_block entry_bb,
		        basic_block exit_bb)
{
  VEC(basic_block,heap) *bbs;
  basic_block after, bb, *entry_pred, *exit_succ;
  struct function *saved_cfun;
  int *entry_flag, *exit_flag, eh_offset;
  unsigned i, num_entry_edges, num_exit_edges;
  edge e;
  edge_iterator ei;
  bitmap vars_to_remove;
  htab_t new_label_map;

  saved_cfun = cfun;

  /* Collect all the blocks in the region.  Manually add ENTRY_BB
     because it won't be added by dfs_enumerate_from.  */
  calculate_dominance_info (CDI_DOMINATORS);

  /* If ENTRY does not strictly dominate EXIT, this cannot be an SESE
     region.  */
  gcc_assert (entry_bb != exit_bb
              && (!exit_bb
		  || dominated_by_p (CDI_DOMINATORS, exit_bb, entry_bb)));

  bbs = NULL;
  VEC_safe_push (basic_block, heap, bbs, entry_bb);
  gather_blocks_in_sese_region (entry_bb, exit_bb, &bbs);

  /* Detach ENTRY_BB and EXIT_BB from CFUN->CFG.  We need to remember
     the predecessor edges to ENTRY_BB and the successor edges to
     EXIT_BB so that we can re-attach them to the new basic block that
     will replace the region.  */
  num_entry_edges = EDGE_COUNT (entry_bb->preds);
  entry_pred = (basic_block *) xcalloc (num_entry_edges, sizeof (basic_block));
  entry_flag = (int *) xcalloc (num_entry_edges, sizeof (int));
  i = 0;
  for (ei = ei_start (entry_bb->preds); (e = ei_safe_edge (ei)) != NULL;)
    {
      entry_flag[i] = e->flags;
      entry_pred[i++] = e->src;
      remove_edge (e);
    }

  if (exit_bb)
    {
      num_exit_edges = EDGE_COUNT (exit_bb->succs);
      exit_succ = (basic_block *) xcalloc (num_exit_edges,
					   sizeof (basic_block));
      exit_flag = (int *) xcalloc (num_exit_edges, sizeof (int));
      i = 0;
      for (ei = ei_start (exit_bb->succs); (e = ei_safe_edge (ei)) != NULL;)
	{
	  exit_flag[i] = e->flags;
	  exit_succ[i++] = e->dest;
	  remove_edge (e);
	}
    }
  else
    {
      num_exit_edges = 0;
      exit_succ = NULL;
      exit_flag = NULL;
    }

  /* Switch context to the child function to initialize DEST_FN's CFG.  */
  gcc_assert (dest_cfun->cfg == NULL);
  cfun = dest_cfun;

  init_empty_tree_cfg ();

  /* Initialize EH information for the new function.  */
  eh_offset = 0;
  new_label_map = NULL;
  if (saved_cfun->eh)
    {
      int region = -1;

      for (i = 0; VEC_iterate (basic_block, bbs, i, bb); i++)
	region = find_outermost_region_in_block (saved_cfun, bb, region);

      init_eh_for_function ();
      if (region != -1)
	{
	  new_label_map = htab_create (17, tree_map_hash, tree_map_eq, free);
	  eh_offset = duplicate_eh_regions (saved_cfun, new_label_mapper,
					    new_label_map, region, 0);
	}
    }

  cfun = saved_cfun;

  /* Move blocks from BBS into DEST_CFUN.  */
  gcc_assert (VEC_length (basic_block, bbs) >= 2);
  after = dest_cfun->cfg->x_entry_block_ptr;
  vars_to_remove = BITMAP_ALLOC (NULL);
  for (i = 0; VEC_iterate (basic_block, bbs, i, bb); i++)
    {
      /* No need to update edge counts on the last block.  It has
	 already been updated earlier when we detached the region from
	 the original CFG.  */
      move_block_to_fn (dest_cfun, bb, after, bb != exit_bb, vars_to_remove,
	                new_label_map, eh_offset);
      after = bb;
    }

  if (new_label_map)
    htab_delete (new_label_map);

  /* Remove the variables marked in VARS_TO_REMOVE from
     CFUN->UNEXPANDED_VAR_LIST.  Otherwise, they will be given a
     DECL_RTL in the context of CFUN.  */
  if (!bitmap_empty_p (vars_to_remove))
    {
      tree *p;

      for (p = &cfun->unexpanded_var_list; *p; )
	{
	  tree var = TREE_VALUE (*p);
	  if (bitmap_bit_p (vars_to_remove, DECL_UID (var)))
	    {
	      *p = TREE_CHAIN (*p);
	      continue;
	    }

	  p = &TREE_CHAIN (*p);
	}
    }

  BITMAP_FREE (vars_to_remove);

  /* Rewire the entry and exit blocks.  The successor to the entry
     block turns into the successor of DEST_FN's ENTRY_BLOCK_PTR in
     the child function.  Similarly, the predecessor of DEST_FN's
     EXIT_BLOCK_PTR turns into the predecessor of EXIT_BLOCK_PTR.  We
     need to switch CFUN between DEST_CFUN and SAVED_CFUN so that the
     various CFG manipulation function get to the right CFG.

     FIXME, this is silly.  The CFG ought to become a parameter to
     these helpers.  */
  cfun = dest_cfun;
  make_edge (ENTRY_BLOCK_PTR, entry_bb, EDGE_FALLTHRU);
  if (exit_bb)
    make_edge (exit_bb,  EXIT_BLOCK_PTR, 0);
  cfun = saved_cfun;

  /* Back in the original function, the SESE region has disappeared,
     create a new basic block in its place.  */
  bb = create_empty_bb (entry_pred[0]);
  for (i = 0; i < num_entry_edges; i++)
    make_edge (entry_pred[i], bb, entry_flag[i]);

  for (i = 0; i < num_exit_edges; i++)
    make_edge (bb, exit_succ[i], exit_flag[i]);

  if (exit_bb)
    {
      free (exit_flag);
      free (exit_succ);
    }
  free (entry_flag);
  free (entry_pred);
  free_dominance_info (CDI_DOMINATORS);
  free_dominance_info (CDI_POST_DOMINATORS);
  VEC_free (basic_block, heap, bbs);

  return bb;
}


/* Dump FUNCTION_DECL FN to file FILE using FLAGS (see TDF_* in tree.h)  */

void
dump_function_to_file (tree fn, FILE *file, int flags)
{
  tree arg, vars, var;
  bool ignore_topmost_bind = false, any_var = false;
  basic_block bb;
  tree chain;
  struct function *saved_cfun;

  fprintf (file, "%s (", lang_hooks.decl_printable_name (fn, 2));

  arg = DECL_ARGUMENTS (fn);
  while (arg)
    {
      print_generic_expr (file, arg, dump_flags);
      if (TREE_CHAIN (arg))
	fprintf (file, ", ");
      arg = TREE_CHAIN (arg);
    }
  fprintf (file, ")\n");

  if (flags & TDF_DETAILS)
    dump_eh_tree (file, DECL_STRUCT_FUNCTION (fn));
  if (flags & TDF_RAW)
    {
      dump_node (fn, TDF_SLIM | flags, file);
      return;
    }

  /* Switch CFUN to point to FN.  */
  saved_cfun = cfun;
  cfun = DECL_STRUCT_FUNCTION (fn);

  /* When GIMPLE is lowered, the variables are no longer available in
     BIND_EXPRs, so display them separately.  */
  if (cfun && cfun->decl == fn && cfun->unexpanded_var_list)
    {
      ignore_topmost_bind = true;

      fprintf (file, "{\n");
      for (vars = cfun->unexpanded_var_list; vars; vars = TREE_CHAIN (vars))
	{
	  var = TREE_VALUE (vars);

	  print_generic_decl (file, var, flags);
	  fprintf (file, "\n");

	  any_var = true;
	}
    }

  if (cfun && cfun->decl == fn && cfun->cfg && basic_block_info)
    {
      /* Make a CFG based dump.  */
      check_bb_profile (ENTRY_BLOCK_PTR, file);
      if (!ignore_topmost_bind)
	fprintf (file, "{\n");

      if (any_var && n_basic_blocks)
	fprintf (file, "\n");

      FOR_EACH_BB (bb)
	dump_generic_bb (file, bb, 2, flags);

      fprintf (file, "}\n");
      check_bb_profile (EXIT_BLOCK_PTR, file);
    }
  else
    {
      int indent;

      /* Make a tree based dump.  */
      chain = DECL_SAVED_TREE (fn);

      if (chain && TREE_CODE (chain) == BIND_EXPR)
	{
	  if (ignore_topmost_bind)
	    {
	      chain = BIND_EXPR_BODY (chain);
	      indent = 2;
	    }
	  else
	    indent = 0;
	}
      else
	{
	  if (!ignore_topmost_bind)
	    fprintf (file, "{\n");
	  indent = 2;
	}

      if (any_var)
	fprintf (file, "\n");

      print_generic_stmt_indented (file, chain, flags, indent);
      if (ignore_topmost_bind)
	fprintf (file, "}\n");
    }

  fprintf (file, "\n\n");

  /* Restore CFUN.  */
  cfun = saved_cfun;
}


/* Dump FUNCTION_DECL FN to stderr using FLAGS (see TDF_* in tree.h)  */

void
debug_function (tree fn, int flags)
{
  dump_function_to_file (fn, stderr, flags);
}


/* Pretty print of the loops intermediate representation.  */
static void print_loop (FILE *, struct loop *, int);
static void print_pred_bbs (FILE *, basic_block bb);
static void print_succ_bbs (FILE *, basic_block bb);


/* Print on FILE the indexes for the predecessors of basic_block BB.  */

static void
print_pred_bbs (FILE *file, basic_block bb)
{
  edge e;
  edge_iterator ei;

  FOR_EACH_EDGE (e, ei, bb->preds)
    fprintf (file, "bb_%d ", e->src->index);
}


/* Print on FILE the indexes for the successors of basic_block BB.  */

static void
print_succ_bbs (FILE *file, basic_block bb)
{
  edge e;
  edge_iterator ei;

  FOR_EACH_EDGE (e, ei, bb->succs)
    fprintf (file, "bb_%d ", e->dest->index);
}


/* Pretty print LOOP on FILE, indented INDENT spaces.  */

static void
print_loop (FILE *file, struct loop *loop, int indent)
{
  char *s_indent;
  basic_block bb;

  if (loop == NULL)
    return;

  s_indent = (char *) alloca ((size_t) indent + 1);
  memset ((void *) s_indent, ' ', (size_t) indent);
  s_indent[indent] = '\0';

  /* Print the loop's header.  */
  fprintf (file, "%sloop_%d\n", s_indent, loop->num);

  /* Print the loop's body.  */
  fprintf (file, "%s{\n", s_indent);
  FOR_EACH_BB (bb)
    if (bb->loop_father == loop)
      {
	/* Print the basic_block's header.  */
	fprintf (file, "%s  bb_%d (preds = {", s_indent, bb->index);
	print_pred_bbs (file, bb);
	fprintf (file, "}, succs = {");
	print_succ_bbs (file, bb);
	fprintf (file, "})\n");

	/* Print the basic_block's body.  */
	fprintf (file, "%s  {\n", s_indent);
	tree_dump_bb (bb, file, indent + 4);
	fprintf (file, "%s  }\n", s_indent);
      }

  print_loop (file, loop->inner, indent + 2);
  fprintf (file, "%s}\n", s_indent);
  print_loop (file, loop->next, indent);
}


/* Follow a CFG edge from the entry point of the program, and on entry
   of a loop, pretty print the loop structure on FILE.  */

void
print_loop_ir (FILE *file)
{
  basic_block bb;

  bb = BASIC_BLOCK (NUM_FIXED_BLOCKS);
  if (bb && bb->loop_father)
    print_loop (file, bb->loop_father, 0);
}


/* Debugging loops structure at tree level.  */

void
debug_loop_ir (void)
{
  print_loop_ir (stderr);
}


/* Return true if BB ends with a call, possibly followed by some
   instructions that must stay with the call.  Return false,
   otherwise.  */

static bool
tree_block_ends_with_call_p (basic_block bb)
{
  block_stmt_iterator bsi = bsi_last (bb);
  return get_call_expr_in (bsi_stmt (bsi)) != NULL;
}


/* Return true if BB ends with a conditional branch.  Return false,
   otherwise.  */

static bool
tree_block_ends_with_condjump_p (basic_block bb)
{
  tree stmt = last_stmt (bb);
  return (stmt && TREE_CODE (stmt) == COND_EXPR);
}


/* Return true if we need to add fake edge to exit at statement T.
   Helper function for tree_flow_call_edges_add.  */

static bool
need_fake_edge_p (tree t)
{
  tree call;

  /* NORETURN and LONGJMP calls already have an edge to exit.
     CONST and PURE calls do not need one.
     We don't currently check for CONST and PURE here, although
     it would be a good idea, because those attributes are
     figured out from the RTL in mark_constant_function, and
     the counter incrementation code from -fprofile-arcs
     leads to different results from -fbranch-probabilities.  */
  call = get_call_expr_in (t);
  if (call
      && !(call_expr_flags (call) & ECF_NORETURN))
    return true;

  if (TREE_CODE (t) == ASM_EXPR
       && (ASM_VOLATILE_P (t) || ASM_INPUT_P (t)))
    return true;

  return false;
}


/* Add fake edges to the function exit for any non constant and non
   noreturn calls, volatile inline assembly in the bitmap of blocks
   specified by BLOCKS or to the whole CFG if BLOCKS is zero.  Return
   the number of blocks that were split.

   The goal is to expose cases in which entering a basic block does
   not imply that all subsequent instructions must be executed.  */

static int
tree_flow_call_edges_add (sbitmap blocks)
{
  int i;
  int blocks_split = 0;
  int last_bb = last_basic_block;
  bool check_last_block = false;

  if (n_basic_blocks == NUM_FIXED_BLOCKS)
    return 0;

  if (! blocks)
    check_last_block = true;
  else
    check_last_block = TEST_BIT (blocks, EXIT_BLOCK_PTR->prev_bb->index);

  /* In the last basic block, before epilogue generation, there will be
     a fallthru edge to EXIT.  Special care is required if the last insn
     of the last basic block is a call because make_edge folds duplicate
     edges, which would result in the fallthru edge also being marked
     fake, which would result in the fallthru edge being removed by
     remove_fake_edges, which would result in an invalid CFG.

     Moreover, we can't elide the outgoing fake edge, since the block
     profiler needs to take this into account in order to solve the minimal
     spanning tree in the case that the call doesn't return.

     Handle this by adding a dummy instruction in a new last basic block.  */
  if (check_last_block)
    {
      basic_block bb = EXIT_BLOCK_PTR->prev_bb;
      block_stmt_iterator bsi = bsi_last (bb);
      tree t = NULL_TREE;
      if (!bsi_end_p (bsi))
	t = bsi_stmt (bsi);

      if (t && need_fake_edge_p (t))
	{
	  edge e;

	  e = find_edge (bb, EXIT_BLOCK_PTR);
	  if (e)
	    {
	      bsi_insert_on_edge (e, build_empty_stmt ());
	      bsi_commit_edge_inserts ();
	    }
	}
    }

  /* Now add fake edges to the function exit for any non constant
     calls since there is no way that we can determine if they will
     return or not...  */
  for (i = 0; i < last_bb; i++)
    {
      basic_block bb = BASIC_BLOCK (i);
      block_stmt_iterator bsi;
      tree stmt, last_stmt;

      if (!bb)
	continue;

      if (blocks && !TEST_BIT (blocks, i))
	continue;

      bsi = bsi_last (bb);
      if (!bsi_end_p (bsi))
	{
	  last_stmt = bsi_stmt (bsi);
	  do
	    {
	      stmt = bsi_stmt (bsi);
	      if (need_fake_edge_p (stmt))
		{
		  edge e;
		  /* The handling above of the final block before the
		     epilogue should be enough to verify that there is
		     no edge to the exit block in CFG already.
		     Calling make_edge in such case would cause us to
		     mark that edge as fake and remove it later.  */
#ifdef ENABLE_CHECKING
		  if (stmt == last_stmt)
		    {
		      e = find_edge (bb, EXIT_BLOCK_PTR);
		      gcc_assert (e == NULL);
		    }
#endif

		  /* Note that the following may create a new basic block
		     and renumber the existing basic blocks.  */
		  if (stmt != last_stmt)
		    {
		      e = split_block (bb, stmt);
		      if (e)
			blocks_split++;
		    }
		  make_edge (bb, EXIT_BLOCK_PTR, EDGE_FAKE);
		}
	      bsi_prev (&bsi);
	    }
	  while (!bsi_end_p (bsi));
	}
    }

  if (blocks_split)
    verify_flow_info ();

  return blocks_split;
}

/* Purge dead abnormal call edges from basic block BB.  */

bool
tree_purge_dead_abnormal_call_edges (basic_block bb)
{
  bool changed = tree_purge_dead_eh_edges (bb);

  if (current_function_has_nonlocal_label)
    {
      tree stmt = last_stmt (bb);
      edge_iterator ei;
      edge e;

      if (!(stmt && tree_can_make_abnormal_goto (stmt)))
	for (ei = ei_start (bb->succs); (e = ei_safe_edge (ei)); )
	  {
	    if (e->flags & EDGE_ABNORMAL)
	      {
		remove_edge (e);
		changed = true;
	      }
	    else
	      ei_next (&ei);
	  }

      /* See tree_purge_dead_eh_edges below.  */
      if (changed)
	free_dominance_info (CDI_DOMINATORS);
    }

  return changed;
}

/* Purge dead EH edges from basic block BB.  */

bool
tree_purge_dead_eh_edges (basic_block bb)
{
  bool changed = false;
  edge e;
  edge_iterator ei;
  tree stmt = last_stmt (bb);

  if (stmt && tree_can_throw_internal (stmt))
    return false;

  for (ei = ei_start (bb->succs); (e = ei_safe_edge (ei)); )
    {
      if (e->flags & EDGE_EH)
	{
	  remove_edge (e);
	  changed = true;
	}
      else
	ei_next (&ei);
    }

  /* Removal of dead EH edges might change dominators of not
     just immediate successors.  E.g. when bb1 is changed so that
     it no longer can throw and bb1->bb3 and bb1->bb4 are dead
     eh edges purged by this function in:
           0
	  / \
	 v   v
	 1-->2
        / \  |
       v   v |
       3-->4 |
        \    v
	 --->5
	     |
	     -
     idom(bb5) must be recomputed.  For now just free the dominance
     info.  */
  if (changed)
    free_dominance_info (CDI_DOMINATORS);

  return changed;
}

bool
tree_purge_all_dead_eh_edges (bitmap blocks)
{
  bool changed = false;
  unsigned i;
  bitmap_iterator bi;

  EXECUTE_IF_SET_IN_BITMAP (blocks, 0, i, bi)
    {
      changed |= tree_purge_dead_eh_edges (BASIC_BLOCK (i));
    }

  return changed;
}

/* This function is called whenever a new edge is created or
   redirected.  */

static void
tree_execute_on_growing_pred (edge e)
{
  basic_block bb = e->dest;

  if (phi_nodes (bb))
    reserve_phi_args_for_new_edge (bb);
}

/* This function is called immediately before edge E is removed from
   the edge vector E->dest->preds.  */

static void
tree_execute_on_shrinking_pred (edge e)
{
  if (phi_nodes (e->dest))
    remove_phi_args (e);
}

/*---------------------------------------------------------------------------
  Helper functions for Loop versioning
  ---------------------------------------------------------------------------*/

/* Adjust phi nodes for 'first' basic block.  'second' basic block is a copy
   of 'first'. Both of them are dominated by 'new_head' basic block. When
   'new_head' was created by 'second's incoming edge it received phi arguments
   on the edge by split_edge(). Later, additional edge 'e' was created to
   connect 'new_head' and 'first'. Now this routine adds phi args on this
   additional edge 'e' that new_head to second edge received as part of edge
   splitting.
*/

static void
tree_lv_adjust_loop_header_phi (basic_block first, basic_block second,
				basic_block new_head, edge e)
{
  tree phi1, phi2;
  edge e2 = find_edge (new_head, second);

  /* Because NEW_HEAD has been created by splitting SECOND's incoming
     edge, we should always have an edge from NEW_HEAD to SECOND.  */
  gcc_assert (e2 != NULL);

  /* Browse all 'second' basic block phi nodes and add phi args to
     edge 'e' for 'first' head. PHI args are always in correct order.  */

  for (phi2 = phi_nodes (second), phi1 = phi_nodes (first);
       phi2 && phi1;
       phi2 = PHI_CHAIN (phi2),  phi1 = PHI_CHAIN (phi1))
    {
      tree def = PHI_ARG_DEF (phi2, e2->dest_idx);
      add_phi_arg (phi1, def, e);
    }
}

/* Adds a if else statement to COND_BB with condition COND_EXPR.
   SECOND_HEAD is the destination of the THEN and FIRST_HEAD is
   the destination of the ELSE part.  */
static void
tree_lv_add_condition_to_bb (basic_block first_head, basic_block second_head,
                            basic_block cond_bb, void *cond_e)
{
  block_stmt_iterator bsi;
  tree goto1 = NULL_TREE;
  tree goto2 = NULL_TREE;
  tree new_cond_expr = NULL_TREE;
  tree cond_expr = (tree) cond_e;
  edge e0;

  /* Build new conditional expr */
  goto1 = build1 (GOTO_EXPR, void_type_node, tree_block_label (first_head));
  goto2 = build1 (GOTO_EXPR, void_type_node, tree_block_label (second_head));
  new_cond_expr = build3 (COND_EXPR, void_type_node, cond_expr, goto1, goto2);

  /* Add new cond in cond_bb.  */
  bsi = bsi_start (cond_bb);
  bsi_insert_after (&bsi, new_cond_expr, BSI_NEW_STMT);
  /* Adjust edges appropriately to connect new head with first head
     as well as second head.  */
  e0 = single_succ_edge (cond_bb);
  e0->flags &= ~EDGE_FALLTHRU;
  e0->flags |= EDGE_FALSE_VALUE;
}

struct cfg_hooks tree_cfg_hooks = {
  "tree",
  tree_verify_flow_info,
  tree_dump_bb,			/* dump_bb  */
  create_bb,			/* create_basic_block  */
  tree_redirect_edge_and_branch,/* redirect_edge_and_branch  */
  tree_redirect_edge_and_branch_force,/* redirect_edge_and_branch_force  */
  remove_bb,			/* delete_basic_block  */
  tree_split_block,		/* split_block  */
  tree_move_block_after,	/* move_block_after  */
  tree_can_merge_blocks_p,	/* can_merge_blocks_p  */
  tree_merge_blocks,		/* merge_blocks  */
  tree_predict_edge,		/* predict_edge  */
  tree_predicted_by_p,		/* predicted_by_p  */
  tree_can_duplicate_bb_p,	/* can_duplicate_block_p  */
  tree_duplicate_bb,		/* duplicate_block  */
  tree_split_edge,		/* split_edge  */
  tree_make_forwarder_block,	/* make_forward_block  */
  NULL,				/* tidy_fallthru_edge  */
  tree_block_ends_with_call_p,	/* block_ends_with_call_p */
  tree_block_ends_with_condjump_p, /* block_ends_with_condjump_p */
  tree_flow_call_edges_add,     /* flow_call_edges_add */
  tree_execute_on_growing_pred,	/* execute_on_growing_pred */
  tree_execute_on_shrinking_pred, /* execute_on_shrinking_pred */
  tree_duplicate_loop_to_header_edge, /* duplicate loop for trees */
  tree_lv_add_condition_to_bb, /* lv_add_condition_to_bb */
  tree_lv_adjust_loop_header_phi, /* lv_adjust_loop_header_phi*/
  extract_true_false_edges_from_block, /* extract_cond_bb_edges */
  flush_pending_stmts		/* flush_pending_stmts */
};


/* Split all critical edges.  */

static unsigned int
split_critical_edges (void)
{
  basic_block bb;
  edge e;
  edge_iterator ei;

  /* split_edge can redirect edges out of SWITCH_EXPRs, which can get
     expensive.  So we want to enable recording of edge to CASE_LABEL_EXPR
     mappings around the calls to split_edge.  */
  start_recording_case_labels ();
  FOR_ALL_BB (bb)
    {
      FOR_EACH_EDGE (e, ei, bb->succs)
	if (EDGE_CRITICAL_P (e) && !(e->flags & EDGE_ABNORMAL))
	  {
	    split_edge (e);
	  }
    }
  end_recording_case_labels ();
  return 0;
}

struct tree_opt_pass pass_split_crit_edges =
{
  "crited",                          /* name */
  NULL,                          /* gate */
  split_critical_edges,          /* execute */
  NULL,                          /* sub */
  NULL,                          /* next */
  0,                             /* static_pass_number */
  TV_TREE_SPLIT_EDGES,           /* tv_id */
  PROP_cfg,                      /* properties required */
  PROP_no_crit_edges,            /* properties_provided */
  0,                             /* properties_destroyed */
  0,                             /* todo_flags_start */
  TODO_dump_func,                /* todo_flags_finish */
  0                              /* letter */
};


/* Return EXP if it is a valid GIMPLE rvalue, else gimplify it into
   a temporary, make sure and register it to be renamed if necessary,
   and finally return the temporary.  Put the statements to compute
   EXP before the current statement in BSI.  */

tree
gimplify_val (block_stmt_iterator *bsi, tree type, tree exp)
{
  tree t, new_stmt, orig_stmt;

  if (is_gimple_val (exp))
    return exp;

  t = make_rename_temp (type, NULL);
  new_stmt = build2 (MODIFY_EXPR, type, t, exp);

  orig_stmt = bsi_stmt (*bsi);
  SET_EXPR_LOCUS (new_stmt, EXPR_LOCUS (orig_stmt));
  TREE_BLOCK (new_stmt) = TREE_BLOCK (orig_stmt);

  bsi_insert_before (bsi, new_stmt, BSI_SAME_STMT);
  if (in_ssa_p)
    mark_new_vars_to_rename (new_stmt);

  return t;
}

/* Build a ternary operation and gimplify it.  Emit code before BSI.
   Return the gimple_val holding the result.  */

tree
gimplify_build3 (block_stmt_iterator *bsi, enum tree_code code,
		 tree type, tree a, tree b, tree c)
{
  tree ret;

  ret = fold_build3 (code, type, a, b, c);
  STRIP_NOPS (ret);

  return gimplify_val (bsi, type, ret);
}

/* Build a binary operation and gimplify it.  Emit code before BSI.
   Return the gimple_val holding the result.  */

tree
gimplify_build2 (block_stmt_iterator *bsi, enum tree_code code,
		 tree type, tree a, tree b)
{
  tree ret;

  ret = fold_build2 (code, type, a, b);
  STRIP_NOPS (ret);

  return gimplify_val (bsi, type, ret);
}

/* Build a unary operation and gimplify it.  Emit code before BSI.
   Return the gimple_val holding the result.  */

tree
gimplify_build1 (block_stmt_iterator *bsi, enum tree_code code, tree type,
		 tree a)
{
  tree ret;

  ret = fold_build1 (code, type, a);
  STRIP_NOPS (ret);

  return gimplify_val (bsi, type, ret);
}



/* Emit return warnings.  */

static unsigned int
execute_warn_function_return (void)
{
#ifdef USE_MAPPED_LOCATION
  source_location location;
#else
  location_t *locus;
#endif
  tree last;
  edge e;
  edge_iterator ei;

  /* If we have a path to EXIT, then we do return.  */
  if (TREE_THIS_VOLATILE (cfun->decl)
      && EDGE_COUNT (EXIT_BLOCK_PTR->preds) > 0)
    {
#ifdef USE_MAPPED_LOCATION
      location = UNKNOWN_LOCATION;
#else
      locus = NULL;
#endif
      FOR_EACH_EDGE (e, ei, EXIT_BLOCK_PTR->preds)
	{
	  last = last_stmt (e->src);
	  if (TREE_CODE (last) == RETURN_EXPR
#ifdef USE_MAPPED_LOCATION
	      && (location = EXPR_LOCATION (last)) != UNKNOWN_LOCATION)
#else
	      && (locus = EXPR_LOCUS (last)) != NULL)
#endif
	    break;
	}
#ifdef USE_MAPPED_LOCATION
      if (location == UNKNOWN_LOCATION)
	location = cfun->function_end_locus;
      warning (0, "%H%<noreturn%> function does return", &location);
#else
      if (!locus)
	locus = &cfun->function_end_locus;
      warning (0, "%H%<noreturn%> function does return", locus);
#endif
    }

  /* If we see "return;" in some basic block, then we do reach the end
     without returning a value.  */
  else if (warn_return_type
	   && !TREE_NO_WARNING (cfun->decl)
	   && EDGE_COUNT (EXIT_BLOCK_PTR->preds) > 0
	   && !VOID_TYPE_P (TREE_TYPE (TREE_TYPE (cfun->decl))))
    {
      FOR_EACH_EDGE (e, ei, EXIT_BLOCK_PTR->preds)
	{
	  tree last = last_stmt (e->src);
	  if (TREE_CODE (last) == RETURN_EXPR
	      && TREE_OPERAND (last, 0) == NULL
	      && !TREE_NO_WARNING (last))
	    {
#ifdef USE_MAPPED_LOCATION
	      location = EXPR_LOCATION (last);
	      if (location == UNKNOWN_LOCATION)
		  location = cfun->function_end_locus;
	      warning (0, "%Hcontrol reaches end of non-void function", &location);
#else
	      locus = EXPR_LOCUS (last);
	      if (!locus)
		locus = &cfun->function_end_locus;
	      warning (0, "%Hcontrol reaches end of non-void function", locus);
#endif
	      TREE_NO_WARNING (cfun->decl) = 1;
	      break;
	    }
	}
    }
  return 0;
}


/* Given a basic block B which ends with a conditional and has
   precisely two successors, determine which of the edges is taken if
   the conditional is true and which is taken if the conditional is
   false.  Set TRUE_EDGE and FALSE_EDGE appropriately.  */

void
extract_true_false_edges_from_block (basic_block b,
				     edge *true_edge,
				     edge *false_edge)
{
  edge e = EDGE_SUCC (b, 0);

  if (e->flags & EDGE_TRUE_VALUE)
    {
      *true_edge = e;
      *false_edge = EDGE_SUCC (b, 1);
    }
  else
    {
      *false_edge = e;
      *true_edge = EDGE_SUCC (b, 1);
    }
}

struct tree_opt_pass pass_warn_function_return =
{
  NULL,					/* name */
  NULL,					/* gate */
  execute_warn_function_return,		/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  0,					/* tv_id */
  PROP_cfg,				/* properties_required */
  0,					/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  0,					/* todo_flags_finish */
  0					/* letter */
};

/* Emit noreturn warnings.  */

static unsigned int
execute_warn_function_noreturn (void)
{
  if (warn_missing_noreturn
      && !TREE_THIS_VOLATILE (cfun->decl)
      && EDGE_COUNT (EXIT_BLOCK_PTR->preds) == 0
      && !lang_hooks.function.missing_noreturn_ok_p (cfun->decl))
    warning (OPT_Wmissing_noreturn, "%Jfunction might be possible candidate "
	     "for attribute %<noreturn%>",
	     cfun->decl);
  return 0;
}

struct tree_opt_pass pass_warn_function_noreturn =
{
  NULL,					/* name */
  NULL,					/* gate */
  execute_warn_function_noreturn,	/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  0,					/* tv_id */
  PROP_cfg,				/* properties_required */
  0,					/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  0,					/* todo_flags_finish */
  0					/* letter */
};
