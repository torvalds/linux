/* Top-level control of tree optimizations.
   Copyright 2001, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.
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
#include "expr.h"
#include "diagnostic.h"
#include "basic-block.h"
#include "flags.h"
#include "tree-flow.h"
#include "tree-dump.h"
#include "timevar.h"
#include "function.h"
#include "langhooks.h"
#include "toplev.h"
#include "flags.h"
#include "cgraph.h"
#include "tree-inline.h"
#include "tree-mudflap.h"
#include "tree-pass.h"
#include "ggc.h"
#include "cgraph.h"
#include "graph.h"
#include "cfgloop.h"
#include "except.h"


/* Gate: execute, or not, all of the non-trivial optimizations.  */

static bool
gate_all_optimizations (void)
{
  return (optimize >= 1
	  /* Don't bother doing anything if the program has errors.  */
	  && !(errorcount || sorrycount));
}

struct tree_opt_pass pass_all_optimizations =
{
  NULL,					/* name */
  gate_all_optimizations,		/* gate */
  NULL,					/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  0,					/* tv_id */
  0,					/* properties_required */
  0,					/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  0,					/* todo_flags_finish */
  0					/* letter */
};

struct tree_opt_pass pass_early_local_passes =
{
  NULL,					/* name */
  gate_all_optimizations,		/* gate */
  NULL,					/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  0,					/* tv_id */
  0,					/* properties_required */
  0,					/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  0,					/* todo_flags_finish */
  0					/* letter */
};

/* Pass: cleanup the CFG just before expanding trees to RTL.
   This is just a round of label cleanups and case node grouping
   because after the tree optimizers have run such cleanups may
   be necessary.  */

static unsigned int
execute_cleanup_cfg_pre_ipa (void)
{
  cleanup_tree_cfg ();
  return 0;
}

struct tree_opt_pass pass_cleanup_cfg =
{
  "cleanup_cfg",			/* name */
  NULL,					/* gate */
  execute_cleanup_cfg_pre_ipa,		/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  0,					/* tv_id */
  PROP_cfg,				/* properties_required */
  0,					/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  TODO_dump_func,					/* todo_flags_finish */
  0					/* letter */
};


/* Pass: cleanup the CFG just before expanding trees to RTL.
   This is just a round of label cleanups and case node grouping
   because after the tree optimizers have run such cleanups may
   be necessary.  */

static unsigned int
execute_cleanup_cfg_post_optimizing (void)
{
  fold_cond_expr_cond ();
  cleanup_tree_cfg ();
  cleanup_dead_labels ();
  group_case_labels ();
  return 0;
}

struct tree_opt_pass pass_cleanup_cfg_post_optimizing =
{
  "final_cleanup",			/* name */
  NULL,					/* gate */
  execute_cleanup_cfg_post_optimizing,	/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  0,					/* tv_id */
  PROP_cfg,				/* properties_required */
  0,					/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  TODO_dump_func,					/* todo_flags_finish */
  0					/* letter */
};

/* Pass: do the actions required to finish with tree-ssa optimization
   passes.  */

static unsigned int
execute_free_datastructures (void)
{
  /* ??? This isn't the right place for this.  Worse, it got computed
     more or less at random in various passes.  */
  free_dominance_info (CDI_DOMINATORS);
  free_dominance_info (CDI_POST_DOMINATORS);

  /* Remove the ssa structures.  Do it here since this includes statement
     annotations that need to be intact during disband_implicit_edges.  */
  delete_tree_ssa ();
  return 0;
}

struct tree_opt_pass pass_free_datastructures =
{
  NULL,					/* name */
  NULL,					/* gate */
  execute_free_datastructures,			/* execute */
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
/* Pass: free cfg annotations.  */

static unsigned int
execute_free_cfg_annotations (void)
{
  basic_block bb;
  block_stmt_iterator bsi;

  /* Emit gotos for implicit jumps.  */
  disband_implicit_edges ();

  /* Remove annotations from every tree in the function.  */
  FOR_EACH_BB (bb)
    for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
      {
	tree stmt = bsi_stmt (bsi);
	ggc_free (stmt->common.ann);
	stmt->common.ann = NULL;
      }

  /* And get rid of annotations we no longer need.  */
  delete_tree_cfg_annotations ();

#ifdef ENABLE_CHECKING
  /* Once the statement annotations have been removed, we can verify
     the integrity of statements in the EH throw table.  */
  verify_eh_throw_table_statements ();
#endif
  return 0;
}

struct tree_opt_pass pass_free_cfg_annotations =
{
  NULL,					/* name */
  NULL,					/* gate */
  execute_free_cfg_annotations,		/* execute */
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

/* Return true if BB has at least one abnormal outgoing edge.  */

static inline bool
has_abnormal_outgoing_edge_p (basic_block bb)
{
  edge e;
  edge_iterator ei;

  FOR_EACH_EDGE (e, ei, bb->succs)
    if (e->flags & EDGE_ABNORMAL)
      return true;

  return false;
}

/* Pass: fixup_cfg.  IPA passes, compilation of earlier functions or inlining
   might have changed some properties, such as marked functions nothrow or
   added calls that can potentially go to non-local labels.  Remove redundant
   edges and basic blocks, and create new ones if necessary.  */

static unsigned int
execute_fixup_cfg (void)
{
  basic_block bb;
  block_stmt_iterator bsi;

  if (cfun->eh)
    FOR_EACH_BB (bb)
      {
	for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
	  {
	    tree stmt = bsi_stmt (bsi);
	    tree call = get_call_expr_in (stmt);

	    if (call && call_expr_flags (call) & (ECF_CONST | ECF_PURE))
	      TREE_SIDE_EFFECTS (call) = 0;
	    if (!tree_could_throw_p (stmt) && lookup_stmt_eh_region (stmt))
	      remove_stmt_from_eh_region (stmt);
	  }
	tree_purge_dead_eh_edges (bb);
      }

  if (current_function_has_nonlocal_label)
    FOR_EACH_BB (bb)
      {
	for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
	  {
	    tree stmt = bsi_stmt (bsi);
	    if (tree_can_make_abnormal_goto (stmt))
	      {
		if (stmt == bsi_stmt (bsi_last (bb)))
		  {
		    if (!has_abnormal_outgoing_edge_p (bb))
		      make_abnormal_goto_edges (bb, true);
		  }
		else
		  {
		    edge e = split_block (bb, stmt);
		    bb = e->src;
		    make_abnormal_goto_edges (bb, true);
		  }
		break;
	      }
	  }
      }

  cleanup_tree_cfg ();

  /* Dump a textual representation of the flowgraph.  */
  if (dump_file)
    dump_tree_cfg (dump_file, dump_flags);

  return 0;
}

struct tree_opt_pass pass_fixup_cfg =
{
  "fixupcfg",				/* name */
  NULL,					/* gate */
  execute_fixup_cfg,			/* execute */
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

/* Do the actions required to initialize internal data structures used
   in tree-ssa optimization passes.  */

static unsigned int
execute_init_datastructures (void)
{
  /* Allocate hash tables, arrays and other structures.  */
  init_tree_ssa ();
  return 0;
}

struct tree_opt_pass pass_init_datastructures =
{
  NULL,					/* name */
  NULL,					/* gate */
  execute_init_datastructures,		/* execute */
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

void
tree_lowering_passes (tree fn)
{
  tree saved_current_function_decl = current_function_decl;

  current_function_decl = fn;
  push_cfun (DECL_STRUCT_FUNCTION (fn));
  tree_register_cfg_hooks ();
  bitmap_obstack_initialize (NULL);
  execute_pass_list (all_lowering_passes);
  free_dominance_info (CDI_POST_DOMINATORS);
  compact_blocks ();
  current_function_decl = saved_current_function_decl;
  bitmap_obstack_release (NULL);
  pop_cfun ();
}

/* Update recursively all inlined_to pointers of functions
   inlined into NODE to INLINED_TO.  */
static void
update_inlined_to_pointers (struct cgraph_node *node,
			    struct cgraph_node *inlined_to)
{
  struct cgraph_edge *e;
  for (e = node->callees; e; e = e->next_callee)
    {
      if (e->callee->global.inlined_to)
	{
	  e->callee->global.inlined_to = inlined_to;
	  update_inlined_to_pointers (e->callee, inlined_to);
	}
    }
}


/* For functions-as-trees languages, this performs all optimization and
   compilation for FNDECL.  */

void
tree_rest_of_compilation (tree fndecl)
{
  location_t saved_loc;
  struct cgraph_node *node;

  timevar_push (TV_EXPAND);

  gcc_assert (!flag_unit_at_a_time || cgraph_global_info_ready);

  node = cgraph_node (fndecl);

  /* We might need the body of this function so that we can expand
     it inline somewhere else.  */
  if (cgraph_preserve_function_body_p (fndecl))
    save_inline_function_body (node);

  /* Initialize the RTL code for the function.  */
  current_function_decl = fndecl;
  saved_loc = input_location;
  input_location = DECL_SOURCE_LOCATION (fndecl);
  init_function_start (fndecl);

  /* Even though we're inside a function body, we still don't want to
     call expand_expr to calculate the size of a variable-sized array.
     We haven't necessarily assigned RTL to all variables yet, so it's
     not safe to try to expand expressions involving them.  */
  cfun->x_dont_save_pending_sizes_p = 1;
  cfun->after_inlining = true;

  if (flag_inline_trees)
    {
      struct cgraph_edge *e;
      for (e = node->callees; e; e = e->next_callee)
	if (!e->inline_failed || warn_inline)
	  break;
      if (e)
	{
	  timevar_push (TV_INTEGRATION);
	  optimize_inline_calls (fndecl);
	  timevar_pop (TV_INTEGRATION);
	}
    }
  /* In non-unit-at-a-time we must mark all referenced functions as needed.
     */
  if (!flag_unit_at_a_time)
    {
      struct cgraph_edge *e;
      for (e = node->callees; e; e = e->next_callee)
	if (e->callee->analyzed)
          cgraph_mark_needed_node (e->callee);
    }

  /* We are not going to maintain the cgraph edges up to date.
     Kill it so it won't confuse us.  */
  cgraph_node_remove_callees (node);


  /* Initialize the default bitmap obstack.  */
  bitmap_obstack_initialize (NULL);
  bitmap_obstack_initialize (&reg_obstack); /* FIXME, only at RTL generation*/
  
  tree_register_cfg_hooks ();
  /* Perform all tree transforms and optimizations.  */
  execute_pass_list (all_passes);
  
  bitmap_obstack_release (&reg_obstack);

  /* Release the default bitmap obstack.  */
  bitmap_obstack_release (NULL);
  
  DECL_SAVED_TREE (fndecl) = NULL;
  cfun = 0;

  /* If requested, warn about function definitions where the function will
     return a value (usually of some struct or union type) which itself will
     take up a lot of stack space.  */
  if (warn_larger_than && !DECL_EXTERNAL (fndecl) && TREE_TYPE (fndecl))
    {
      tree ret_type = TREE_TYPE (TREE_TYPE (fndecl));

      if (ret_type && TYPE_SIZE_UNIT (ret_type)
	  && TREE_CODE (TYPE_SIZE_UNIT (ret_type)) == INTEGER_CST
	  && 0 < compare_tree_int (TYPE_SIZE_UNIT (ret_type),
				   larger_than_size))
	{
	  unsigned int size_as_int
	    = TREE_INT_CST_LOW (TYPE_SIZE_UNIT (ret_type));

	  if (compare_tree_int (TYPE_SIZE_UNIT (ret_type), size_as_int) == 0)
	    warning (0, "size of return value of %q+D is %u bytes",
                     fndecl, size_as_int);
	  else
	    warning (0, "size of return value of %q+D is larger than %wd bytes",
                     fndecl, larger_than_size);
	}
    }

  if (!flag_inline_trees)
    {
      DECL_SAVED_TREE (fndecl) = NULL;
      if (DECL_STRUCT_FUNCTION (fndecl) == 0
	  && !cgraph_node (fndecl)->origin)
	{
	  /* Stop pointing to the local nodes about to be freed.
	     But DECL_INITIAL must remain nonzero so we know this
	     was an actual function definition.
	     For a nested function, this is done in c_pop_function_context.
	     If rest_of_compilation set this to 0, leave it 0.  */
	  if (DECL_INITIAL (fndecl) != 0)
	    DECL_INITIAL (fndecl) = error_mark_node;
	}
    }

  input_location = saved_loc;

  ggc_collect ();
  timevar_pop (TV_EXPAND);
}
