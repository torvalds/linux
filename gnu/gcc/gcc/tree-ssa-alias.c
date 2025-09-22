/* Alias analysis for trees.
   Copyright (C) 2004, 2005 Free Software Foundation, Inc.
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
#include "timevar.h"
#include "expr.h"
#include "ggc.h"
#include "langhooks.h"
#include "flags.h"
#include "function.h"
#include "diagnostic.h"
#include "tree-dump.h"
#include "tree-gimple.h"
#include "tree-flow.h"
#include "tree-inline.h"
#include "tree-pass.h"
#include "tree-ssa-structalias.h"
#include "convert.h"
#include "params.h"
#include "ipa-type-escape.h"
#include "vec.h"
#include "bitmap.h"
#include "vecprim.h"
#include "pointer-set.h"

/* Obstack used to hold grouping bitmaps and other temporary bitmaps used by
   aliasing  */
static bitmap_obstack alias_obstack;

/* 'true' after aliases have been computed (see compute_may_aliases).  */
bool aliases_computed_p;

/* Structure to map a variable to its alias set and keep track of the
   virtual operands that will be needed to represent it.  */
struct alias_map_d
{
  /* Variable and its alias set.  */
  tree var;
  HOST_WIDE_INT set;

  /* Total number of virtual operands that will be needed to represent
     all the aliases of VAR.  */
  long total_alias_vops;

  /* Nonzero if the aliases for this memory tag have been grouped
     already.  Used in group_aliases.  */
  unsigned int grouped_p : 1;

  /* Set of variables aliased with VAR.  This is the exact same
     information contained in VAR_ANN (VAR)->MAY_ALIASES, but in
     bitmap form to speed up alias grouping.  */
  bitmap may_aliases;
};


/* Counters used to display statistics on alias analysis.  */
struct alias_stats_d
{
  unsigned int alias_queries;
  unsigned int alias_mayalias;
  unsigned int alias_noalias;
  unsigned int simple_queries;
  unsigned int simple_resolved;
  unsigned int tbaa_queries;
  unsigned int tbaa_resolved;
  unsigned int structnoaddress_queries;
  unsigned int structnoaddress_resolved;
};


/* Local variables.  */
static struct alias_stats_d alias_stats;

/* Local functions.  */
static void compute_flow_insensitive_aliasing (struct alias_info *);
static void finalize_ref_all_pointers (struct alias_info *);
static void dump_alias_stats (FILE *);
static bool may_alias_p (tree, HOST_WIDE_INT, tree, HOST_WIDE_INT, bool);
static tree create_memory_tag (tree type, bool is_type_tag);
static tree get_tmt_for (tree, struct alias_info *);
static tree get_nmt_for (tree);
static void add_may_alias (tree, tree);
static void replace_may_alias (tree, size_t, tree);
static struct alias_info *init_alias_info (void);
static void delete_alias_info (struct alias_info *);
static void compute_flow_sensitive_aliasing (struct alias_info *);
static void setup_pointers_and_addressables (struct alias_info *);
static void create_global_var (void);
static void maybe_create_global_var (struct alias_info *ai);
static void group_aliases (struct alias_info *);
static void set_pt_anything (tree ptr);

/* Global declarations.  */

/* Call clobbered variables in the function.  If bit I is set, then
   REFERENCED_VARS (I) is call-clobbered.  */
bitmap call_clobbered_vars;

/* Addressable variables in the function.  If bit I is set, then
   REFERENCED_VARS (I) has had its address taken.  Note that
   CALL_CLOBBERED_VARS and ADDRESSABLE_VARS are not related.  An
   addressable variable is not necessarily call-clobbered (e.g., a
   local addressable whose address does not escape) and not all
   call-clobbered variables are addressable (e.g., a local static
   variable).  */
bitmap addressable_vars;

/* When the program has too many call-clobbered variables and call-sites,
   this variable is used to represent the clobbering effects of function
   calls.  In these cases, all the call clobbered variables in the program
   are forced to alias this variable.  This reduces compile times by not
   having to keep track of too many V_MAY_DEF expressions at call sites.  */
tree global_var;

/* qsort comparison function to sort type/name tags by DECL_UID.  */

static int
sort_tags_by_id (const void *pa, const void *pb)
{
  tree a = *(tree *)pa;
  tree b = *(tree *)pb;
 
  return DECL_UID (a) - DECL_UID (b);
}

/* Initialize WORKLIST to contain those memory tags that are marked call
   clobbered.  Initialized WORKLIST2 to contain the reasons these
   memory tags escaped.  */

static void
init_transitive_clobber_worklist (VEC (tree, heap) **worklist,
				  VEC (int, heap) **worklist2)
{
  referenced_var_iterator rvi;
  tree curr;

  FOR_EACH_REFERENCED_VAR (curr, rvi)
    {
      if (MTAG_P (curr) && is_call_clobbered (curr))
	{
	  VEC_safe_push (tree, heap, *worklist, curr);
	  VEC_safe_push (int, heap, *worklist2, var_ann (curr)->escape_mask);
	}
    }
}

/* Add ALIAS to WORKLIST (and the reason for escaping REASON to WORKLIST2) if
   ALIAS is not already marked call clobbered, and is a memory
   tag.  */

static void
add_to_worklist (tree alias, VEC (tree, heap) **worklist,
		 VEC (int, heap) **worklist2,
		 int reason)
{
  if (MTAG_P (alias) && !is_call_clobbered (alias))
    {
      VEC_safe_push (tree, heap, *worklist, alias);
      VEC_safe_push (int, heap, *worklist2, reason);
    }
}

/* Mark aliases of TAG as call clobbered, and place any tags on the
   alias list that were not already call clobbered on WORKLIST.  */

static void
mark_aliases_call_clobbered (tree tag, VEC (tree, heap) **worklist,
			     VEC (int, heap) **worklist2)
{
  unsigned int i;
  VEC (tree, gc) *ma;
  tree entry;
  var_ann_t ta = var_ann (tag);

  if (!MTAG_P (tag))
    return;
  ma = may_aliases (tag);
  if (!ma)
    return;

  for (i = 0; VEC_iterate (tree, ma, i, entry); i++)
    {
      if (!unmodifiable_var_p (entry))
	{
	  add_to_worklist (entry, worklist, worklist2, ta->escape_mask);
	  mark_call_clobbered (entry, ta->escape_mask);
	}
    }
}

/* Tags containing global vars need to be marked as global.
   Tags containing call clobbered vars need to be marked as call
   clobbered. */

static void
compute_tag_properties (void)
{
  referenced_var_iterator rvi;
  tree tag;
  bool changed = true;
  VEC (tree, heap) *taglist = NULL;

  FOR_EACH_REFERENCED_VAR (tag, rvi)
    {
      if (!MTAG_P (tag) || TREE_CODE (tag) == STRUCT_FIELD_TAG)
	continue;
      VEC_safe_push (tree, heap, taglist, tag);
    }

  /* We sort the taglist by DECL_UID, for two reasons.
     1. To get a sequential ordering to make the bitmap accesses
     faster.
     2. Because of the way we compute aliases, it's more likely that
     an earlier tag is included in a later tag, and this will reduce
     the number of iterations.

     If we had a real tag graph, we would just topo-order it and be
     done with it.  */
  qsort (VEC_address (tree, taglist),
	 VEC_length (tree, taglist),
	 sizeof (tree),
	 sort_tags_by_id);

  /* Go through each tag not marked as global, and if it aliases
     global vars, mark it global. 
     
     If the tag contains call clobbered vars, mark it call
     clobbered.  

     This loop iterates because tags may appear in the may-aliases
     list of other tags when we group.  */

  while (changed)
    {
      unsigned int k;

      changed = false;      
      for (k = 0; VEC_iterate (tree, taglist, k, tag); k++)
	{
	  VEC (tree, gc) *ma;
	  unsigned int i;
	  tree entry;
	  bool tagcc = is_call_clobbered (tag);
	  bool tagglobal = MTAG_GLOBAL (tag);
	  
	  if (tagcc && tagglobal)
	    continue;
	  
	  ma = may_aliases (tag);
	  if (!ma)
	    continue;

	  for (i = 0; VEC_iterate (tree, ma, i, entry); i++)
	    {
	      /* Call clobbered entries cause the tag to be marked
		 call clobbered.  */
	      if (!tagcc && is_call_clobbered (entry))
		{
		  mark_call_clobbered (tag, var_ann (entry)->escape_mask);
		  tagcc = true;
		  changed = true;
		}

	      /* Global vars cause the tag to be marked global.  */
	      if (!tagglobal && is_global_var (entry))
		{
		  MTAG_GLOBAL (tag) = true;
		  changed = true;
		  tagglobal = true;
		}

	      /* Early exit once both global and cc are set, since the
		 loop can't do any more than that.  */
	      if (tagcc && tagglobal)
		break;
	    }
	}
    }
  VEC_free (tree, heap, taglist);
}

/* Set up the initial variable clobbers and globalness.
   When this function completes, only tags whose aliases need to be
   clobbered will be set clobbered.  Tags clobbered because they   
   contain call clobbered vars are handled in compute_tag_properties.  */

static void
set_initial_properties (struct alias_info *ai)
{
  unsigned int i;
  referenced_var_iterator rvi;
  tree var;
  tree ptr;

  FOR_EACH_REFERENCED_VAR (var, rvi)
    {
      if (is_global_var (var) 
	  && (!var_can_have_subvars (var)
	      || get_subvars_for_var (var) == NULL))
	{
	  if (!unmodifiable_var_p (var))
	    mark_call_clobbered (var, ESCAPE_IS_GLOBAL);
	}
      else if (TREE_CODE (var) == PARM_DECL
	       && default_def (var)
	       && POINTER_TYPE_P (TREE_TYPE (var)))
	{
	  tree def = default_def (var);
	  get_ptr_info (def)->value_escapes_p = 1;
	  get_ptr_info (def)->escape_mask |= ESCAPE_IS_PARM;	  
	}
    }

  for (i = 0; VEC_iterate (tree, ai->processed_ptrs, i, ptr); i++)
    {
      struct ptr_info_def *pi = SSA_NAME_PTR_INFO (ptr);
      var_ann_t v_ann = var_ann (SSA_NAME_VAR (ptr));
      
      if (pi->value_escapes_p)
	{
	  /* If PTR escapes then its associated memory tags and
	     pointed-to variables are call-clobbered.  */
	  if (pi->name_mem_tag)
	    mark_call_clobbered (pi->name_mem_tag, pi->escape_mask);

	  if (v_ann->symbol_mem_tag)
	    mark_call_clobbered (v_ann->symbol_mem_tag, pi->escape_mask);

	  if (pi->pt_vars)
	    {
	      bitmap_iterator bi;
	      unsigned int j;	      
	      EXECUTE_IF_SET_IN_BITMAP (pi->pt_vars, 0, j, bi)
		if (!unmodifiable_var_p (referenced_var (j)))
		  mark_call_clobbered (referenced_var (j), pi->escape_mask);
	    }
	}

      /* If the name tag is call clobbered, so is the symbol tag
	 associated with the base VAR_DECL.  */
      if (pi->name_mem_tag
	  && v_ann->symbol_mem_tag
	  && is_call_clobbered (pi->name_mem_tag))
	mark_call_clobbered (v_ann->symbol_mem_tag, pi->escape_mask);

      /* Name tags and symbol tags that we don't know where they point
	 to, might point to global memory, and thus, are clobbered.

         FIXME:  This is not quite right.  They should only be
         clobbered if value_escapes_p is true, regardless of whether
         they point to global memory or not.
         So removing this code and fixing all the bugs would be nice.
         It is the cause of a bunch of clobbering.  */
      if ((pi->pt_global_mem || pi->pt_anything) 
	  && pi->is_dereferenced && pi->name_mem_tag)
	{
	  mark_call_clobbered (pi->name_mem_tag, ESCAPE_IS_GLOBAL);
	  MTAG_GLOBAL (pi->name_mem_tag) = true;
	}
      
      if ((pi->pt_global_mem || pi->pt_anything) 
	  && pi->is_dereferenced
	  && v_ann->symbol_mem_tag)
	{
	  mark_call_clobbered (v_ann->symbol_mem_tag, ESCAPE_IS_GLOBAL);
	  MTAG_GLOBAL (v_ann->symbol_mem_tag) = true;
	}
    }
}


/* This variable is set to true if we are updating the used alone
   information for SMTs, or are in a pass that is going to break it
   temporarily.  */
bool updating_used_alone;

/* Compute which variables need to be marked call clobbered because
   their tag is call clobbered, and which tags need to be marked
   global because they contain global variables.  */

static void
compute_call_clobbered (struct alias_info *ai)
{
  VEC (tree, heap) *worklist = NULL;
  VEC(int,heap) *worklist2 = NULL;
  
  set_initial_properties (ai);
  init_transitive_clobber_worklist (&worklist, &worklist2);
  while (VEC_length (tree, worklist) != 0)
    {
      tree curr = VEC_pop (tree, worklist);
      int reason = VEC_pop (int, worklist2);
      
      mark_call_clobbered (curr, reason);
      mark_aliases_call_clobbered (curr, &worklist, &worklist2);
    }
  VEC_free (tree, heap, worklist);
  VEC_free (int, heap, worklist2);
  compute_tag_properties ();
}


/* Helper for recalculate_used_alone.  Return a conservatively correct
   answer as to whether STMT may make a store on the LHS to SYM.  */

static bool
lhs_may_store_to (tree stmt, tree sym ATTRIBUTE_UNUSED)
{
  tree lhs = TREE_OPERAND (stmt, 0);
  
  lhs = get_base_address (lhs);
  
  if (!lhs)
    return false;

  if (TREE_CODE (lhs) == SSA_NAME)
    return false;
  /* We could do better here by looking at the type tag of LHS, but it
     is unclear whether this is worth it. */
  return true;
}

/* Recalculate the used_alone information for SMTs . */

void 
recalculate_used_alone (void)
{
  VEC (tree, heap) *calls = NULL;
  block_stmt_iterator bsi;
  basic_block bb;
  tree stmt;
  size_t i;
  referenced_var_iterator rvi;
  tree var;
  
  /* First, reset all the SMT used alone bits to zero.  */
  updating_used_alone = true;
  FOR_EACH_REFERENCED_VAR (var, rvi)
    if (TREE_CODE (var) == SYMBOL_MEMORY_TAG)
      {
	SMT_OLD_USED_ALONE (var) = SMT_USED_ALONE (var);
	SMT_USED_ALONE (var) = 0;
      }

  /* Walk all the statements.
     Calls get put into a list of statements to update, since we will
     need to update operands on them if we make any changes.
     If we see a bare use of a SMT anywhere in a real virtual use or virtual
     def, mark the SMT as used alone, and for renaming.  */
  FOR_EACH_BB (bb)
    {
      for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
	{
	  bool iscall = false;
	  ssa_op_iter iter;

	  stmt = bsi_stmt (bsi);
	  
	  if (TREE_CODE (stmt) == CALL_EXPR
	      || (TREE_CODE (stmt) == MODIFY_EXPR 
		  && TREE_CODE (TREE_OPERAND (stmt, 1)) == CALL_EXPR))
	    {
	      iscall = true;
	      VEC_safe_push (tree, heap, calls, stmt);	    
	    }
	  
	  FOR_EACH_SSA_TREE_OPERAND (var, stmt, iter, 
				     SSA_OP_VUSE | SSA_OP_VIRTUAL_DEFS)
	    {
	      tree svar = var;
	      
	      if (TREE_CODE (var) == SSA_NAME)
		svar = SSA_NAME_VAR (var);
	      
	      if (TREE_CODE (svar) == SYMBOL_MEMORY_TAG)
		{
		  /* We only care about the LHS on calls.  */
		  if (iscall && !lhs_may_store_to (stmt, svar))
		    continue;

		  if (!SMT_USED_ALONE (svar))
		    {
		      SMT_USED_ALONE (svar) = true;
		      
		      /* Only need to mark for renaming if it wasn't
			 used alone before.  */
		      if (!SMT_OLD_USED_ALONE (svar))
			mark_sym_for_renaming (svar);
		    }
		}
	    }
	}	           
    }
  
  /* Update the operands on all the calls we saw.  */
  if (calls)
    {
      for (i = 0; VEC_iterate (tree, calls, i, stmt); i++)
	update_stmt (stmt);
    }
  
  /* We need to mark SMT's that are no longer used for renaming so the
     symbols go away, or else verification will be angry with us, even
     though they are dead.  */
  FOR_EACH_REFERENCED_VAR (var, rvi)
    if (TREE_CODE (var) == SYMBOL_MEMORY_TAG)
      {
	if (SMT_OLD_USED_ALONE (var) && !SMT_USED_ALONE (var))
	  mark_sym_for_renaming (var);
      }

  VEC_free (tree, heap, calls);
  updating_used_alone = false;
}

/* Compute may-alias information for every variable referenced in function
   FNDECL.

   Alias analysis proceeds in 3 main phases:

   1- Points-to and escape analysis.

   This phase walks the use-def chains in the SSA web looking for three
   things:

	* Assignments of the form P_i = &VAR
	* Assignments of the form P_i = malloc()
	* Pointers and ADDR_EXPR that escape the current function.

   The concept of 'escaping' is the same one used in the Java world.  When
   a pointer or an ADDR_EXPR escapes, it means that it has been exposed
   outside of the current function.  So, assignment to global variables,
   function arguments and returning a pointer are all escape sites, as are
   conversions between pointers and integers.

   This is where we are currently limited.  Since not everything is renamed
   into SSA, we lose track of escape properties when a pointer is stashed
   inside a field in a structure, for instance.  In those cases, we are
   assuming that the pointer does escape.

   We use escape analysis to determine whether a variable is
   call-clobbered.  Simply put, if an ADDR_EXPR escapes, then the variable
   is call-clobbered.  If a pointer P_i escapes, then all the variables
   pointed-to by P_i (and its memory tag) also escape.

   2- Compute flow-sensitive aliases

   We have two classes of memory tags.  Memory tags associated with the
   pointed-to data type of the pointers in the program.  These tags are
   called "symbol memory tag" (SMT).  The other class are those associated
   with SSA_NAMEs, called "name memory tag" (NMT). The basic idea is that
   when adding operands for an INDIRECT_REF *P_i, we will first check
   whether P_i has a name tag, if it does we use it, because that will have
   more precise aliasing information.  Otherwise, we use the standard symbol
   tag.

   In this phase, we go through all the pointers we found in points-to
   analysis and create alias sets for the name memory tags associated with
   each pointer P_i.  If P_i escapes, we mark call-clobbered the variables
   it points to and its tag.


   3- Compute flow-insensitive aliases

   This pass will compare the alias set of every symbol memory tag and
   every addressable variable found in the program.  Given a symbol
   memory tag SMT and an addressable variable V.  If the alias sets of
   SMT and V conflict (as computed by may_alias_p), then V is marked
   as an alias tag and added to the alias set of SMT.

   For instance, consider the following function:

	    foo (int i)
	    {
	      int *p, a, b;
	    
	      if (i > 10)
	        p = &a;
	      else
	        p = &b;
	    
	      *p = 3;
	      a = b + 2;
	      return *p;
	    }

   After aliasing analysis has finished, the symbol memory tag for pointer
   'p' will have two aliases, namely variables 'a' and 'b'.  Every time
   pointer 'p' is dereferenced, we want to mark the operation as a
   potential reference to 'a' and 'b'.

	    foo (int i)
	    {
	      int *p, a, b;

	      if (i_2 > 10)
		p_4 = &a;
	      else
		p_6 = &b;
	      # p_1 = PHI <p_4(1), p_6(2)>;

	      # a_7 = V_MAY_DEF <a_3>;
	      # b_8 = V_MAY_DEF <b_5>;
	      *p_1 = 3;

	      # a_9 = V_MAY_DEF <a_7>
	      # VUSE <b_8>
	      a_9 = b_8 + 2;

	      # VUSE <a_9>;
	      # VUSE <b_8>;
	      return *p_1;
	    }

   In certain cases, the list of may aliases for a pointer may grow too
   large.  This may cause an explosion in the number of virtual operands
   inserted in the code.  Resulting in increased memory consumption and
   compilation time.

   When the number of virtual operands needed to represent aliased
   loads and stores grows too large (configurable with @option{--param
   max-aliased-vops}), alias sets are grouped to avoid severe
   compile-time slow downs and memory consumption.  See group_aliases.  */

static unsigned int
compute_may_aliases (void)
{
  struct alias_info *ai;
  
  memset (&alias_stats, 0, sizeof (alias_stats));

  /* Initialize aliasing information.  */
  ai = init_alias_info ();

  /* For each pointer P_i, determine the sets of variables that P_i may
     point-to.  For every addressable variable V, determine whether the
     address of V escapes the current function, making V call-clobbered
     (i.e., whether &V is stored in a global variable or if its passed as a
     function call argument).  */
  compute_points_to_sets (ai);

  /* Collect all pointers and addressable variables, compute alias sets,
     create memory tags for pointers and promote variables whose address is
     not needed anymore.  */
  setup_pointers_and_addressables (ai);

  /* Compute flow-sensitive, points-to based aliasing for all the name
     memory tags.  Note that this pass needs to be done before flow
     insensitive analysis because it uses the points-to information
     gathered before to mark call-clobbered symbol tags.  */
  compute_flow_sensitive_aliasing (ai);

  /* Compute type-based flow-insensitive aliasing for all the type
     memory tags.  */
  compute_flow_insensitive_aliasing (ai);
  
  /* Compute call clobbering information.  */
  compute_call_clobbered (ai);

  /* Determine if we need to enable alias grouping.  */
  if (ai->total_alias_vops >= MAX_ALIASED_VOPS)
    group_aliases (ai);

  /* If the program has too many call-clobbered variables and/or function
     calls, create .GLOBAL_VAR and use it to model call-clobbering
     semantics at call sites.  This reduces the number of virtual operands
     considerably, improving compile times at the expense of lost
     aliasing precision.  */
  maybe_create_global_var (ai);

  /* If the program contains ref-all pointers, finalize may-alias information
     for them.  This pass needs to be run after call-clobbering information
     has been computed.  */
  if (ai->ref_all_symbol_mem_tag)
    finalize_ref_all_pointers (ai);

  /* Debugging dumps.  */
  if (dump_file)
    {
      dump_referenced_vars (dump_file);
      if (dump_flags & TDF_STATS)
	dump_alias_stats (dump_file);
      dump_points_to_info (dump_file);
      dump_alias_info (dump_file);
    }

  /* Deallocate memory used by aliasing data structures.  */
  delete_alias_info (ai);

  updating_used_alone = true;
  {
    block_stmt_iterator bsi;
    basic_block bb;
    FOR_EACH_BB (bb)
      {
        for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
          {
            update_stmt_if_modified (bsi_stmt (bsi));
          }
      }
  }
  recalculate_used_alone ();
  updating_used_alone = false;
  return 0;
}


struct tree_opt_pass pass_may_alias = 
{
  "alias",				/* name */
  NULL,					/* gate */
  compute_may_aliases,			/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  TV_TREE_MAY_ALIAS,			/* tv_id */
  PROP_cfg | PROP_ssa,			/* properties_required */
  PROP_alias,				/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  TODO_dump_func | TODO_update_ssa
    | TODO_ggc_collect | TODO_verify_ssa
    | TODO_verify_stmts, 		/* todo_flags_finish */
  0					/* letter */
};


/* Data structure used to count the number of dereferences to PTR
   inside an expression.  */
struct count_ptr_d
{
  tree ptr;
  unsigned count;
};


/* Helper for count_uses_and_derefs.  Called by walk_tree to look for
   (ALIGN/MISALIGNED_)INDIRECT_REF nodes for the pointer passed in DATA.  */

static tree
count_ptr_derefs (tree *tp, int *walk_subtrees, void *data)
{
  struct count_ptr_d *count_p = (struct count_ptr_d *) data;

  /* Do not walk inside ADDR_EXPR nodes.  In the expression &ptr->fld,
     pointer 'ptr' is *not* dereferenced, it is simply used to compute
     the address of 'fld' as 'ptr + offsetof(fld)'.  */
  if (TREE_CODE (*tp) == ADDR_EXPR)
    {
      *walk_subtrees = 0;
      return NULL_TREE;
    }

  if (INDIRECT_REF_P (*tp) && TREE_OPERAND (*tp, 0) == count_p->ptr)
    count_p->count++;

  return NULL_TREE;
}


/* Count the number of direct and indirect uses for pointer PTR in
   statement STMT.  The two counts are stored in *NUM_USES_P and
   *NUM_DEREFS_P respectively.  *IS_STORE_P is set to 'true' if at
   least one of those dereferences is a store operation.  */

void
count_uses_and_derefs (tree ptr, tree stmt, unsigned *num_uses_p,
		       unsigned *num_derefs_p, bool *is_store)
{
  ssa_op_iter i;
  tree use;

  *num_uses_p = 0;
  *num_derefs_p = 0;
  *is_store = false;

  /* Find out the total number of uses of PTR in STMT.  */
  FOR_EACH_SSA_TREE_OPERAND (use, stmt, i, SSA_OP_USE)
    if (use == ptr)
      (*num_uses_p)++;

  /* Now count the number of indirect references to PTR.  This is
     truly awful, but we don't have much choice.  There are no parent
     pointers inside INDIRECT_REFs, so an expression like
     '*x_1 = foo (x_1, *x_1)' needs to be traversed piece by piece to
     find all the indirect and direct uses of x_1 inside.  The only
     shortcut we can take is the fact that GIMPLE only allows
     INDIRECT_REFs inside the expressions below.  */
  if (TREE_CODE (stmt) == MODIFY_EXPR
      || (TREE_CODE (stmt) == RETURN_EXPR
	  && TREE_CODE (TREE_OPERAND (stmt, 0)) == MODIFY_EXPR)
      || TREE_CODE (stmt) == ASM_EXPR
      || TREE_CODE (stmt) == CALL_EXPR)
    {
      tree lhs, rhs;

      if (TREE_CODE (stmt) == MODIFY_EXPR)
	{
	  lhs = TREE_OPERAND (stmt, 0);
	  rhs = TREE_OPERAND (stmt, 1);
	}
      else if (TREE_CODE (stmt) == RETURN_EXPR)
	{
	  tree e = TREE_OPERAND (stmt, 0);
	  lhs = TREE_OPERAND (e, 0);
	  rhs = TREE_OPERAND (e, 1);
	}
      else if (TREE_CODE (stmt) == ASM_EXPR)
	{
	  lhs = ASM_OUTPUTS (stmt);
	  rhs = ASM_INPUTS (stmt);
	}
      else
	{
	  lhs = NULL_TREE;
	  rhs = stmt;
	}

      if (lhs && (TREE_CODE (lhs) == TREE_LIST || EXPR_P (lhs)))
	{
	  struct count_ptr_d count;
	  count.ptr = ptr;
	  count.count = 0;
	  walk_tree (&lhs, count_ptr_derefs, &count, NULL);
	  *is_store = true;
	  *num_derefs_p = count.count;
	}

      if (rhs && (TREE_CODE (rhs) == TREE_LIST || EXPR_P (rhs)))
	{
	  struct count_ptr_d count;
	  count.ptr = ptr;
	  count.count = 0;
	  walk_tree (&rhs, count_ptr_derefs, &count, NULL);
	  *num_derefs_p += count.count;
	}
    }

  gcc_assert (*num_uses_p >= *num_derefs_p);
}

/* Initialize the data structures used for alias analysis.  */

static struct alias_info *
init_alias_info (void)
{
  struct alias_info *ai;
  referenced_var_iterator rvi;
  tree var;

  bitmap_obstack_initialize (&alias_obstack);
  ai = XCNEW (struct alias_info);
  ai->ssa_names_visited = sbitmap_alloc (num_ssa_names);
  sbitmap_zero (ai->ssa_names_visited);
  ai->processed_ptrs = VEC_alloc (tree, heap, 50);
  ai->written_vars = BITMAP_ALLOC (&alias_obstack);
  ai->dereferenced_ptrs_store = BITMAP_ALLOC (&alias_obstack);
  ai->dereferenced_ptrs_load = BITMAP_ALLOC (&alias_obstack);

  /* If aliases have been computed before, clear existing information.  */
  if (aliases_computed_p)
    {
      unsigned i;
  
      /* Similarly, clear the set of addressable variables.  In this
	 case, we can just clear the set because addressability is
	 only computed here.  */
      bitmap_clear (addressable_vars);

      /* Clear flow-insensitive alias information from each symbol.  */
      FOR_EACH_REFERENCED_VAR (var, rvi)
	{
	  var_ann_t ann = var_ann (var);
	  
	  ann->is_aliased = 0;
	  ann->may_aliases = NULL;
	  NUM_REFERENCES_CLEAR (ann);

	  /* Since we are about to re-discover call-clobbered
	     variables, clear the call-clobbered flag.  Variables that
	     are intrinsically call-clobbered (globals, local statics,
	     etc) will not be marked by the aliasing code, so we can't
	     remove them from CALL_CLOBBERED_VARS.  

	     NB: STRUCT_FIELDS are still call clobbered if they are for
	     a global variable, so we *don't* clear their call clobberedness
	     just because they are tags, though we will clear it if they
	     aren't for global variables.  */
	  if (TREE_CODE (var) == NAME_MEMORY_TAG
	      || TREE_CODE (var) == SYMBOL_MEMORY_TAG
	      || !is_global_var (var))
	    clear_call_clobbered (var);
	}

      /* Clear flow-sensitive points-to information from each SSA name.  */
      for (i = 1; i < num_ssa_names; i++)
	{
	  tree name = ssa_name (i);

	  if (!name || !POINTER_TYPE_P (TREE_TYPE (name)))
	    continue;

	  if (SSA_NAME_PTR_INFO (name))
	    {
	      struct ptr_info_def *pi = SSA_NAME_PTR_INFO (name);

	      /* Clear all the flags but keep the name tag to
		 avoid creating new temporaries unnecessarily.  If
		 this pointer is found to point to a subset or
		 superset of its former points-to set, then a new
		 tag will need to be created in create_name_tags.  */
	      pi->pt_anything = 0;
	      pi->pt_null = 0;
	      pi->value_escapes_p = 0;
	      pi->is_dereferenced = 0;
	      if (pi->pt_vars)
		bitmap_clear (pi->pt_vars);
	    }
	}
    }

  /* Next time, we will need to reset alias information.  */
  aliases_computed_p = true;

  return ai;
}


/* Deallocate memory used by alias analysis.  */

static void
delete_alias_info (struct alias_info *ai)
{
  size_t i;
  referenced_var_iterator rvi;
  tree var;

  sbitmap_free (ai->ssa_names_visited);
  VEC_free (tree, heap, ai->processed_ptrs);

  for (i = 0; i < ai->num_addressable_vars; i++)
    free (ai->addressable_vars[i]);
  
  FOR_EACH_REFERENCED_VAR(var, rvi)
    {
      var_ann_t ann = var_ann (var);
      NUM_REFERENCES_CLEAR (ann);
    }

  free (ai->addressable_vars);

  for (i = 0; i < ai->num_pointers; i++)
    free (ai->pointers[i]);
  free (ai->pointers);

  BITMAP_FREE (ai->written_vars);
  BITMAP_FREE (ai->dereferenced_ptrs_store);
  BITMAP_FREE (ai->dereferenced_ptrs_load);
  bitmap_obstack_release (&alias_obstack);
  free (ai);

  delete_points_to_sets ();
}

/* Used for hashing to identify pointer infos with identical
   pt_vars bitmaps.  */
static int
eq_ptr_info (const void *p1, const void *p2)
{
  const struct ptr_info_def *n1 = (const struct ptr_info_def *) p1;
  const struct ptr_info_def *n2 = (const struct ptr_info_def *) p2;
  return bitmap_equal_p (n1->pt_vars, n2->pt_vars);
}

static hashval_t
ptr_info_hash (const void *p)
{
  const struct ptr_info_def *n = (const struct ptr_info_def *) p;
  return bitmap_hash (n->pt_vars);
}

/* Create name tags for all the pointers that have been dereferenced.
   We only create a name tag for a pointer P if P is found to point to
   a set of variables (so that we can alias them to *P) or if it is
   the result of a call to malloc (which means that P cannot point to
   anything else nor alias any other variable).

   If two pointers P and Q point to the same set of variables, they
   are assigned the same name tag.  */

static void
create_name_tags (void)
{
  size_t i;
  VEC (tree, heap) *with_ptvars = NULL;
  tree ptr;
  htab_t ptr_hash;

  /* Collect the list of pointers with a non-empty points to set.  */
  for (i = 1; i < num_ssa_names; i++)
    {
      tree ptr = ssa_name (i);
      struct ptr_info_def *pi;

      if (!ptr
	  || !POINTER_TYPE_P (TREE_TYPE (ptr))
	  || !SSA_NAME_PTR_INFO (ptr))
	continue;

      pi = SSA_NAME_PTR_INFO (ptr);

      if (pi->pt_anything || !pi->is_dereferenced)
	{
	  /* No name tags for pointers that have not been
	     dereferenced or point to an arbitrary location.  */
	  pi->name_mem_tag = NULL_TREE;
	  continue;
	}

      /* Set pt_anything on the pointers without pt_vars filled in so
	 that they are assigned a symbol tag.  */
      if (pi->pt_vars && !bitmap_empty_p (pi->pt_vars))	
	VEC_safe_push (tree, heap, with_ptvars, ptr);
      else
	set_pt_anything (ptr);
    }
  
  /* If we didn't find any pointers with pt_vars set, we're done.  */
  if (!with_ptvars)
    return;

  ptr_hash = htab_create (10, ptr_info_hash, eq_ptr_info, NULL);
  /* Now go through the pointers with pt_vars, and find a name tag
     with the same pt_vars as this pointer, or create one if one
     doesn't exist.  */
  for (i = 0; VEC_iterate (tree, with_ptvars, i, ptr); i++)
    {
      struct ptr_info_def *pi = SSA_NAME_PTR_INFO (ptr);
      tree old_name_tag = pi->name_mem_tag;
      struct ptr_info_def **slot;
      
      /* If PTR points to a set of variables, check if we don't
	 have another pointer Q with the same points-to set before
	 creating a tag.  If so, use Q's tag instead of creating a
	 new one.
	 
	 This is important for not creating unnecessary symbols
	 and also for copy propagation.  If we ever need to
	 propagate PTR into Q or vice-versa, we would run into
	 problems if they both had different name tags because
	 they would have different SSA version numbers (which
	 would force us to take the name tags in and out of SSA).  */

      slot = (struct ptr_info_def **) htab_find_slot (ptr_hash, pi, INSERT);
      if (*slot)
        pi->name_mem_tag = (*slot)->name_mem_tag;
      else
	{
	  *slot = pi;
	  /* If we didn't find a pointer with the same points-to set
	     as PTR, create a new name tag if needed.  */
	  if (pi->name_mem_tag == NULL_TREE)
	    pi->name_mem_tag = get_nmt_for (ptr);
	}
      
      /* If the new name tag computed for PTR is different than
	 the old name tag that it used to have, then the old tag
	 needs to be removed from the IL, so we mark it for
	 renaming.  */
      if (old_name_tag && old_name_tag != pi->name_mem_tag)
	mark_sym_for_renaming (old_name_tag);
      
      TREE_THIS_VOLATILE (pi->name_mem_tag)
	|= TREE_THIS_VOLATILE (TREE_TYPE (TREE_TYPE (ptr)));
      
      /* Mark the new name tag for renaming.  */
      mark_sym_for_renaming (pi->name_mem_tag);
    }
  htab_delete (ptr_hash);

  VEC_free (tree, heap, with_ptvars);
}


/* For every pointer P_i in AI->PROCESSED_PTRS, create may-alias sets for
   the name memory tag (NMT) associated with P_i.  If P_i escapes, then its
   name tag and the variables it points-to are call-clobbered.  Finally, if
   P_i escapes and we could not determine where it points to, then all the
   variables in the same alias set as *P_i are marked call-clobbered.  This
   is necessary because we must assume that P_i may take the address of any
   variable in the same alias set.  */

static void
compute_flow_sensitive_aliasing (struct alias_info *ai)
{
  size_t i;
  tree ptr;
  
  for (i = 0; VEC_iterate (tree, ai->processed_ptrs, i, ptr); i++)
    {
      if (!find_what_p_points_to (ptr))
	set_pt_anything (ptr);
    }

  create_name_tags ();

  for (i = 0; VEC_iterate (tree, ai->processed_ptrs, i, ptr); i++)
    {
      unsigned j;
      struct ptr_info_def *pi = SSA_NAME_PTR_INFO (ptr);
      var_ann_t v_ann = var_ann (SSA_NAME_VAR (ptr));
      bitmap_iterator bi;


      /* Set up aliasing information for PTR's name memory tag (if it has
	 one).  Note that only pointers that have been dereferenced will
	 have a name memory tag.  */
      if (pi->name_mem_tag && pi->pt_vars)
	EXECUTE_IF_SET_IN_BITMAP (pi->pt_vars, 0, j, bi)
	  {
	    add_may_alias (pi->name_mem_tag, referenced_var (j));
	    add_may_alias (v_ann->symbol_mem_tag, referenced_var (j));
	  }
    }
}


/* Compute type-based alias sets.  Traverse all the pointers and
   addressable variables found in setup_pointers_and_addressables.
   
   For every pointer P in AI->POINTERS and addressable variable V in
   AI->ADDRESSABLE_VARS, add V to the may-alias sets of P's symbol
   memory tag (SMT) if their alias sets conflict.  V is then marked as
   an alias tag so that the operand scanner knows that statements
   containing V have aliased operands.  */

static void
compute_flow_insensitive_aliasing (struct alias_info *ai)
{
  size_t i;

  /* Initialize counter for the total number of virtual operands that
     aliasing will introduce.  When AI->TOTAL_ALIAS_VOPS goes beyond the
     threshold set by --params max-alias-vops, we enable alias
     grouping.  */
  ai->total_alias_vops = 0;

  /* For every pointer P, determine which addressable variables may alias
     with P's symbol memory tag.  */
  for (i = 0; i < ai->num_pointers; i++)
    {
      size_t j;
      struct alias_map_d *p_map = ai->pointers[i];
      tree tag = var_ann (p_map->var)->symbol_mem_tag;
      var_ann_t tag_ann = var_ann (tag);
      tree var;

      /* Call-clobbering information is not finalized yet at this point.  */
      if (PTR_IS_REF_ALL (p_map->var))
	continue;

      p_map->total_alias_vops = 0;
      p_map->may_aliases = BITMAP_ALLOC (&alias_obstack);

      /* Add any pre-existing may_aliases to the bitmap used to represent
	 TAG's alias set in case we need to group aliases.  */
      for (j = 0; VEC_iterate (tree, tag_ann->may_aliases, j, var); ++j)
	bitmap_set_bit (p_map->may_aliases, DECL_UID (var));

      for (j = 0; j < ai->num_addressable_vars; j++)
	{
	  struct alias_map_d *v_map;
	  var_ann_t v_ann;
	  bool tag_stored_p, var_stored_p;
	  
	  v_map = ai->addressable_vars[j];
	  var = v_map->var;
	  v_ann = var_ann (var);

	  /* Skip memory tags and variables that have never been
	     written to.  We also need to check if the variables are
	     call-clobbered because they may be overwritten by
	     function calls.

	     Note this is effectively random accessing elements in
	     the sparse bitset, which can be highly inefficient.
	     So we first check the call_clobbered status of the
	     tag and variable before querying the bitmap.  */
	  tag_stored_p = is_call_clobbered (tag)
	                 || bitmap_bit_p (ai->written_vars, DECL_UID (tag));
	  var_stored_p = is_call_clobbered (var)
	                 || bitmap_bit_p (ai->written_vars, DECL_UID (var));
	  if (!tag_stored_p && !var_stored_p)
	    continue;
	     
	  if (may_alias_p (p_map->var, p_map->set, var, v_map->set, false))
	    {
	      size_t num_tag_refs, num_var_refs;

	      num_tag_refs = NUM_REFERENCES (tag_ann);
	      num_var_refs = NUM_REFERENCES (v_ann);

	      /* Add VAR to TAG's may-aliases set.  */

	      /* We should never have a var with subvars here, because
	         they shouldn't get into the set of addressable vars */
	      gcc_assert (!var_can_have_subvars (var)
			  || get_subvars_for_var (var) == NULL);

	      add_may_alias (tag, var);
	      /* Update the bitmap used to represent TAG's alias set
		 in case we need to group aliases.  */
	      bitmap_set_bit (p_map->may_aliases, DECL_UID (var));

	      /* Update the total number of virtual operands due to
		 aliasing.  Since we are adding one more alias to TAG's
		 may-aliases set, the total number of virtual operands due
		 to aliasing will be increased by the number of references
		 made to VAR and TAG (every reference to TAG will also
		 count as a reference to VAR).  */
	      ai->total_alias_vops += (num_var_refs + num_tag_refs);
	      p_map->total_alias_vops += (num_var_refs + num_tag_refs);


	    }
	}
    }

  /* Since this analysis is based exclusively on symbols, it fails to
     handle cases where two pointers P and Q have different memory
     tags with conflicting alias set numbers but no aliased symbols in
     common.

     For example, suppose that we have two memory tags SMT.1 and SMT.2
     such that
     
     		may-aliases (SMT.1) = { a }
		may-aliases (SMT.2) = { b }

     and the alias set number of SMT.1 conflicts with that of SMT.2.
     Since they don't have symbols in common, loads and stores from
     SMT.1 and SMT.2 will seem independent of each other, which will
     lead to the optimizers making invalid transformations (see
     testsuite/gcc.c-torture/execute/pr15262-[12].c).

     To avoid this problem, we do a final traversal of AI->POINTERS
     looking for pairs of pointers that have no aliased symbols in
     common and yet have conflicting alias set numbers.  */
  for (i = 0; i < ai->num_pointers; i++)
    {
      size_t j;
      struct alias_map_d *p_map1 = ai->pointers[i];
      tree tag1 = var_ann (p_map1->var)->symbol_mem_tag;
      bitmap may_aliases1 = p_map1->may_aliases;

      if (PTR_IS_REF_ALL (p_map1->var))
	continue;

      for (j = i + 1; j < ai->num_pointers; j++)
	{
	  struct alias_map_d *p_map2 = ai->pointers[j];
	  tree tag2 = var_ann (p_map2->var)->symbol_mem_tag;
	  bitmap may_aliases2 = p_map2->may_aliases;

	  if (PTR_IS_REF_ALL (p_map2->var))
	    continue;

	  /* If the pointers may not point to each other, do nothing.  */
	  if (!may_alias_p (p_map1->var, p_map1->set, tag2, p_map2->set, true))
	    continue;

	  /* The two pointers may alias each other.  If they already have
	     symbols in common, do nothing.  */
	  if (bitmap_intersect_p (may_aliases1, may_aliases2))
	    continue;

	  if (!bitmap_empty_p (may_aliases2))
	    {
	      unsigned int k;
	      bitmap_iterator bi;

	      /* Add all the aliases for TAG2 into TAG1's alias set.
		 FIXME, update grouping heuristic counters.  */
	      EXECUTE_IF_SET_IN_BITMAP (may_aliases2, 0, k, bi)
		add_may_alias (tag1, referenced_var (k));
	      bitmap_ior_into (may_aliases1, may_aliases2);
	    }
	  else
	    {
	      /* Since TAG2 does not have any aliases of its own, add
		 TAG2 itself to the alias set of TAG1.  */
	      add_may_alias (tag1, tag2);
	      bitmap_set_bit (may_aliases1, DECL_UID (tag2));
	    }
	}
    }
  
  if (dump_file)
    fprintf (dump_file, "\n%s: Total number of aliased vops: %ld\n",
	     get_name (current_function_decl),
	     ai->total_alias_vops);
}


/* Finalize may-alias information for ref-all pointers.  Traverse all
   the addressable variables found in setup_pointers_and_addressables.

   If flow-sensitive alias analysis has attached a name memory tag to
   a ref-all pointer, we will use it for the dereferences because that
   will have more precise aliasing information.  But if there is no
   name tag, we will use a special symbol tag that aliases all the
   call-clobbered addressable variables.  */

static void
finalize_ref_all_pointers (struct alias_info *ai)
{
  size_t i;

  if (global_var)
    add_may_alias (ai->ref_all_symbol_mem_tag, global_var);
  else
    {
      /* First add the real call-clobbered variables.  */
      for (i = 0; i < ai->num_addressable_vars; i++)
	{
	  tree var = ai->addressable_vars[i]->var;
	  if (is_call_clobbered (var))
	    add_may_alias (ai->ref_all_symbol_mem_tag, var);
        }

      /* Then add the call-clobbered pointer memory tags.  See
	 compute_flow_insensitive_aliasing for the rationale.  */
      for (i = 0; i < ai->num_pointers; i++)
	{
	  tree ptr = ai->pointers[i]->var, tag;
	  if (PTR_IS_REF_ALL (ptr))
	    continue;
	  tag = var_ann (ptr)->symbol_mem_tag;
	  if (is_call_clobbered (tag))
	    add_may_alias (ai->ref_all_symbol_mem_tag, tag);
	}
    }
}


/* Comparison function for qsort used in group_aliases.  */

static int
total_alias_vops_cmp (const void *p, const void *q)
{
  const struct alias_map_d **p1 = (const struct alias_map_d **)p;
  const struct alias_map_d **p2 = (const struct alias_map_d **)q;
  long n1 = (*p1)->total_alias_vops;
  long n2 = (*p2)->total_alias_vops;

  /* We want to sort in descending order.  */
  return (n1 > n2 ? -1 : (n1 == n2) ? 0 : 1);
}

/* Group all the aliases for TAG to make TAG represent all the
   variables in its alias set.  Update the total number
   of virtual operands due to aliasing (AI->TOTAL_ALIAS_VOPS).  This
   function will make TAG be the unique alias tag for all the
   variables in its may-aliases.  So, given:

   	may-aliases(TAG) = { V1, V2, V3 }

   This function will group the variables into:

   	may-aliases(V1) = { TAG }
	may-aliases(V2) = { TAG }
	may-aliases(V2) = { TAG }  */

static void
group_aliases_into (tree tag, bitmap tag_aliases, struct alias_info *ai)
{
  unsigned int i;
  var_ann_t tag_ann = var_ann (tag);
  size_t num_tag_refs = NUM_REFERENCES (tag_ann);
  bitmap_iterator bi;

  EXECUTE_IF_SET_IN_BITMAP (tag_aliases, 0, i, bi)
    {
      tree var = referenced_var (i);
      var_ann_t ann = var_ann (var);

      /* Make TAG the unique alias of VAR.  */
      ann->is_aliased = 0;
      ann->may_aliases = NULL;

      /* Note that VAR and TAG may be the same if the function has no
	 addressable variables (see the discussion at the end of
	 setup_pointers_and_addressables).  */
      if (var != tag)
	add_may_alias (var, tag);

      /* Reduce total number of virtual operands contributed
	 by TAG on behalf of VAR.  Notice that the references to VAR
	 itself won't be removed.  We will merely replace them with
	 references to TAG.  */
      ai->total_alias_vops -= num_tag_refs;
    }

  /* We have reduced the number of virtual operands that TAG makes on
     behalf of all the variables formerly aliased with it.  However,
     we have also "removed" all the virtual operands for TAG itself,
     so we add them back.  */
  ai->total_alias_vops += num_tag_refs;

  /* TAG no longer has any aliases.  */
  tag_ann->may_aliases = NULL;
}


/* Group may-aliases sets to reduce the number of virtual operands due
   to aliasing.

     1- Sort the list of pointers in decreasing number of contributed
	virtual operands.

     2- Take the first entry in AI->POINTERS and revert the role of
	the memory tag and its aliases.  Usually, whenever an aliased
	variable Vi is found to alias with a memory tag T, we add Vi
	to the may-aliases set for T.  Meaning that after alias
	analysis, we will have:

		may-aliases(T) = { V1, V2, V3, ..., Vn }

	This means that every statement that references T, will get 'n'
	virtual operands for each of the Vi tags.  But, when alias
	grouping is enabled, we make T an alias tag and add it to the
	alias set of all the Vi variables:

		may-aliases(V1) = { T }
		may-aliases(V2) = { T }
		...
		may-aliases(Vn) = { T }

	This has two effects: (a) statements referencing T will only get
	a single virtual operand, and, (b) all the variables Vi will now
	appear to alias each other.  So, we lose alias precision to
	improve compile time.  But, in theory, a program with such a high
	level of aliasing should not be very optimizable in the first
	place.

     3- Since variables may be in the alias set of more than one
	memory tag, the grouping done in step (2) needs to be extended
	to all the memory tags that have a non-empty intersection with
	the may-aliases set of tag T.  For instance, if we originally
	had these may-aliases sets:

		may-aliases(T) = { V1, V2, V3 }
		may-aliases(R) = { V2, V4 }

	In step (2) we would have reverted the aliases for T as:

		may-aliases(V1) = { T }
		may-aliases(V2) = { T }
		may-aliases(V3) = { T }

	But note that now V2 is no longer aliased with R.  We could
	add R to may-aliases(V2), but we are in the process of
	grouping aliases to reduce virtual operands so what we do is
	add V4 to the grouping to obtain:

		may-aliases(V1) = { T }
		may-aliases(V2) = { T }
		may-aliases(V3) = { T }
		may-aliases(V4) = { T }

     4- If the total number of virtual operands due to aliasing is
	still above the threshold set by max-alias-vops, go back to (2).  */

static void
group_aliases (struct alias_info *ai)
{
  size_t i;
  tree ptr;

  /* Sort the POINTERS array in descending order of contributed
     virtual operands.  */
  qsort (ai->pointers, ai->num_pointers, sizeof (struct alias_map_d *),
         total_alias_vops_cmp);

  /* For every pointer in AI->POINTERS, reverse the roles of its tag
     and the tag's may-aliases set.  */
  for (i = 0; i < ai->num_pointers; i++)
    {
      size_t j;
      tree tag1 = var_ann (ai->pointers[i]->var)->symbol_mem_tag;
      bitmap tag1_aliases = ai->pointers[i]->may_aliases;

      /* Skip tags that have been grouped already.  */
      if (ai->pointers[i]->grouped_p)
	continue;

      /* See if TAG1 had any aliases in common with other symbol tags.
	 If we find a TAG2 with common aliases with TAG1, add TAG2's
	 aliases into TAG1.  */
      for (j = i + 1; j < ai->num_pointers; j++)
	{
	  bitmap tag2_aliases = ai->pointers[j]->may_aliases;

          if (bitmap_intersect_p (tag1_aliases, tag2_aliases))
	    {
	      tree tag2 = var_ann (ai->pointers[j]->var)->symbol_mem_tag;

	      bitmap_ior_into (tag1_aliases, tag2_aliases);

	      /* TAG2 does not need its aliases anymore.  */
	      bitmap_clear (tag2_aliases);
	      var_ann (tag2)->may_aliases = NULL;

	      /* TAG1 is the unique alias of TAG2.  */
	      add_may_alias (tag2, tag1);

	      ai->pointers[j]->grouped_p = true;
	    }
	}

      /* Now group all the aliases we collected into TAG1.  */
      group_aliases_into (tag1, tag1_aliases, ai);

      /* If we've reduced total number of virtual operands below the
	 threshold, stop.  */
      if (ai->total_alias_vops < MAX_ALIASED_VOPS)
	break;
    }

  /* Finally, all the variables that have been grouped cannot be in
     the may-alias set of name memory tags.  Suppose that we have
     grouped the aliases in this code so that may-aliases(a) = SMT.20

     	p_5 = &a;
	...
	# a_9 = V_MAY_DEF <a_8>
	p_5->field = 0
	... Several modifications to SMT.20 ... 
	# VUSE <a_9>
	x_30 = p_5->field

     Since p_5 points to 'a', the optimizers will try to propagate 0
     into p_5->field, but that is wrong because there have been
     modifications to 'SMT.20' in between.  To prevent this we have to
     replace 'a' with 'SMT.20' in the name tag of p_5.  */
  for (i = 0; VEC_iterate (tree, ai->processed_ptrs, i, ptr); i++)
    {
      size_t j;
      tree name_tag = SSA_NAME_PTR_INFO (ptr)->name_mem_tag;
      VEC(tree,gc) *aliases;
      tree alias;
      
      if (name_tag == NULL_TREE)
	continue;

      aliases = var_ann (name_tag)->may_aliases;
      for (j = 0; VEC_iterate (tree, aliases, j, alias); j++)
	{
	  var_ann_t ann = var_ann (alias);

	  if ((!MTAG_P (alias)
	       || TREE_CODE (alias) == STRUCT_FIELD_TAG)
	      && ann->may_aliases)
	    {
	      tree new_alias;

	      gcc_assert (VEC_length (tree, ann->may_aliases) == 1);

	      new_alias = VEC_index (tree, ann->may_aliases, 0);
	      replace_may_alias (name_tag, j, new_alias);
	    }
	}
    }

  if (dump_file)
    fprintf (dump_file,
	     "%s: Total number of aliased vops after grouping: %ld%s\n",
	     get_name (current_function_decl),
	     ai->total_alias_vops,
	     (ai->total_alias_vops < 0) ? " (negative values are OK)" : "");
}


/* Create a new alias set entry for VAR in AI->ADDRESSABLE_VARS.  */

static void
create_alias_map_for (tree var, struct alias_info *ai)
{
  struct alias_map_d *alias_map;
  alias_map = XCNEW (struct alias_map_d);
  alias_map->var = var;
  alias_map->set = get_alias_set (var);
  ai->addressable_vars[ai->num_addressable_vars++] = alias_map;
}


/* Create memory tags for all the dereferenced pointers and build the
   ADDRESSABLE_VARS and POINTERS arrays used for building the may-alias
   sets.  Based on the address escape and points-to information collected
   earlier, this pass will also clear the TREE_ADDRESSABLE flag from those
   variables whose address is not needed anymore.  */

static void
setup_pointers_and_addressables (struct alias_info *ai)
{
  size_t n_vars, num_addressable_vars, num_pointers;
  referenced_var_iterator rvi;
  tree var;
  VEC (tree, heap) *varvec = NULL;
  safe_referenced_var_iterator srvi;

  /* Size up the arrays ADDRESSABLE_VARS and POINTERS.  */
  num_addressable_vars = num_pointers = 0;
  
  FOR_EACH_REFERENCED_VAR (var, rvi)
    {
      if (may_be_aliased (var))
	num_addressable_vars++;

      if (POINTER_TYPE_P (TREE_TYPE (var)))
	{
	  /* Since we don't keep track of volatile variables, assume that
	     these pointers are used in indirect store operations.  */
	  if (TREE_THIS_VOLATILE (var))
	    bitmap_set_bit (ai->dereferenced_ptrs_store, DECL_UID (var));

	  num_pointers++;
	}
    }

  /* Create ADDRESSABLE_VARS and POINTERS.  Note that these arrays are
     always going to be slightly bigger than we actually need them
     because some TREE_ADDRESSABLE variables will be marked
     non-addressable below and only pointers with unique symbol tags are
     going to be added to POINTERS.  */
  ai->addressable_vars = XCNEWVEC (struct alias_map_d *, num_addressable_vars);
  ai->pointers = XCNEWVEC (struct alias_map_d *, num_pointers);
  ai->num_addressable_vars = 0;
  ai->num_pointers = 0;

  /* Since we will be creating symbol memory tags within this loop,
     cache the value of NUM_REFERENCED_VARS to avoid processing the
     additional tags unnecessarily.  */
  n_vars = num_referenced_vars;

  FOR_EACH_REFERENCED_VAR_SAFE (var, varvec, srvi)
    {
      var_ann_t v_ann = var_ann (var);
      subvar_t svars;

      /* Name memory tags already have flow-sensitive aliasing
	 information, so they need not be processed by
	 compute_flow_insensitive_aliasing.  Similarly, symbol memory
	 tags are already accounted for when we process their
	 associated pointer. 
      
         Structure fields, on the other hand, have to have some of this
         information processed for them, but it's pointless to mark them
         non-addressable (since they are fake variables anyway).  */
      if (MTAG_P (var) && TREE_CODE (var) != STRUCT_FIELD_TAG)
	continue;

      /* Remove the ADDRESSABLE flag from every addressable variable whose
         address is not needed anymore.  This is caused by the propagation
         of ADDR_EXPR constants into INDIRECT_REF expressions and the
         removal of dead pointer assignments done by the early scalar
         cleanup passes.  */
      if (TREE_ADDRESSABLE (var))
	{
	  if (!bitmap_bit_p (addressable_vars, DECL_UID (var))
	      && TREE_CODE (var) != RESULT_DECL
	      && !is_global_var (var))
	    {
	      bool okay_to_mark = true;

	      /* Since VAR is now a regular GIMPLE register, we will need
		 to rename VAR into SSA afterwards.  */
	      mark_sym_for_renaming (var);

	      /* If VAR can have sub-variables, and any of its
		 sub-variables has its address taken, then we cannot
		 remove the addressable flag from VAR.  */
	      if (var_can_have_subvars (var)
		  && (svars = get_subvars_for_var (var)))
		{
		  subvar_t sv;

		  for (sv = svars; sv; sv = sv->next)
		    {	      
		      if (bitmap_bit_p (addressable_vars, DECL_UID (sv->var)))
			okay_to_mark = false;
		      mark_sym_for_renaming (sv->var);
		    }
		}

	      /* The address of VAR is not needed, remove the
		 addressable bit, so that it can be optimized as a
		 regular variable.  */
	      if (okay_to_mark)
		mark_non_addressable (var);
	    }
	}

      /* Global variables and addressable locals may be aliased.  Create an
         entry in ADDRESSABLE_VARS for VAR.  */
      if (may_be_aliased (var)	  
	  && (!var_can_have_subvars (var) 
	      || get_subvars_for_var (var) == NULL))
	{
	  create_alias_map_for (var, ai);
	  mark_sym_for_renaming (var);
	}

      /* Add pointer variables that have been dereferenced to the POINTERS
         array and create a symbol memory tag for them.  */
      if (POINTER_TYPE_P (TREE_TYPE (var)))
	{
	  if ((bitmap_bit_p (ai->dereferenced_ptrs_store, DECL_UID (var))
	       || bitmap_bit_p (ai->dereferenced_ptrs_load, DECL_UID (var))))
	    {
	      tree tag;
	      var_ann_t t_ann;

	      /* If pointer VAR still doesn't have a memory tag
		 associated with it, create it now or re-use an
		 existing one.  */
	      tag = get_tmt_for (var, ai);
	      t_ann = var_ann (tag);

	      /* The symbol tag will need to be renamed into SSA
		 afterwards. Note that we cannot do this inside
		 get_tmt_for because aliasing may run multiple times
		 and we only create symbol tags the first time.  */
	      mark_sym_for_renaming (tag);

	      /* Similarly, if pointer VAR used to have another type
		 tag, we will need to process it in the renamer to
		 remove the stale virtual operands.  */
	      if (v_ann->symbol_mem_tag)
		mark_sym_for_renaming (v_ann->symbol_mem_tag);

	      /* Associate the tag with pointer VAR.  */
	      v_ann->symbol_mem_tag = tag;

	      /* If pointer VAR has been used in a store operation,
		 then its memory tag must be marked as written-to.  */
	      if (bitmap_bit_p (ai->dereferenced_ptrs_store, DECL_UID (var)))
		bitmap_set_bit (ai->written_vars, DECL_UID (tag));

	      /* All the dereferences of pointer VAR count as
		 references of TAG.  Since TAG can be associated with
		 several pointers, add the dereferences of VAR to the
		 TAG.  */
	      NUM_REFERENCES_SET (t_ann, 
				  NUM_REFERENCES (t_ann)
				  + NUM_REFERENCES (v_ann));
	    }
	  else
	    {
	      /* The pointer has not been dereferenced.  If it had a
		 symbol memory tag, remove it and mark the old tag for
		 renaming to remove it out of the IL.  */
	      var_ann_t ann = var_ann (var);
	      tree tag = ann->symbol_mem_tag;
	      if (tag)
		{
		  mark_sym_for_renaming (tag);
		  ann->symbol_mem_tag = NULL_TREE;
		}
	    }
	}
    }
  VEC_free (tree, heap, varvec);
}


/* Determine whether to use .GLOBAL_VAR to model call clobbering semantics. At
   every call site, we need to emit V_MAY_DEF expressions to represent the
   clobbering effects of the call for variables whose address escapes the
   current function.

   One approach is to group all call-clobbered variables into a single
   representative that is used as an alias of every call-clobbered variable
   (.GLOBAL_VAR).  This works well, but it ties the optimizer hands because
   references to any call clobbered variable is a reference to .GLOBAL_VAR.

   The second approach is to emit a clobbering V_MAY_DEF for every 
   call-clobbered variable at call sites.  This is the preferred way in terms 
   of optimization opportunities but it may create too many V_MAY_DEF operands
   if there are many call clobbered variables and function calls in the 
   function.

   To decide whether or not to use .GLOBAL_VAR we multiply the number of
   function calls found by the number of call-clobbered variables.  If that
   product is beyond a certain threshold, as determined by the parameterized
   values shown below, we use .GLOBAL_VAR.

   FIXME.  This heuristic should be improved.  One idea is to use several
   .GLOBAL_VARs of different types instead of a single one.  The thresholds
   have been derived from a typical bootstrap cycle, including all target
   libraries. Compile times were found increase by ~1% compared to using
   .GLOBAL_VAR.  */

static void
maybe_create_global_var (struct alias_info *ai)
{
  unsigned i, n_clobbered;
  bitmap_iterator bi;
  
  /* No need to create it, if we have one already.  */
  if (global_var == NULL_TREE)
    {
      /* Count all the call-clobbered variables.  */
      n_clobbered = 0;
      EXECUTE_IF_SET_IN_BITMAP (call_clobbered_vars, 0, i, bi)
	{
	  n_clobbered++;
	}

      /* If the number of virtual operands that would be needed to
	 model all the call-clobbered variables is larger than
	 GLOBAL_VAR_THRESHOLD, create .GLOBAL_VAR.

	 Also create .GLOBAL_VAR if there are no call-clobbered
	 variables and the program contains a mixture of pure/const
	 and regular function calls.  This is to avoid the problem
	 described in PR 20115:

	      int X;
	      int func_pure (void) { return X; }
	      int func_non_pure (int a) { X += a; }
	      int foo ()
	      {
	 	int a = func_pure ();
		func_non_pure (a);
		a = func_pure ();
		return a;
	      }

	 Since foo() has no call-clobbered variables, there is
	 no relationship between the calls to func_pure and
	 func_non_pure.  Since func_pure has no side-effects, value
	 numbering optimizations elide the second call to func_pure.
	 So, if we have some pure/const and some regular calls in the
	 program we create .GLOBAL_VAR to avoid missing these
	 relations.  */
      if (ai->num_calls_found * n_clobbered >= (size_t) GLOBAL_VAR_THRESHOLD
	  || (n_clobbered == 0
	      && ai->num_calls_found > 0
	      && ai->num_pure_const_calls_found > 0
	      && ai->num_calls_found > ai->num_pure_const_calls_found))
	create_global_var ();
    }

  /* Mark all call-clobbered symbols for renaming.  Since the initial
     rewrite into SSA ignored all call sites, we may need to rename
     .GLOBAL_VAR and the call-clobbered variables.   */
  EXECUTE_IF_SET_IN_BITMAP (call_clobbered_vars, 0, i, bi)
    {
      tree var = referenced_var (i);

      /* If the function has calls to clobbering functions and
	 .GLOBAL_VAR has been created, make it an alias for all
	 call-clobbered variables.  */
      if (global_var && var != global_var)
	{
	  add_may_alias (var, global_var);
	  gcc_assert (!get_subvars_for_var (var));
	}
      
      mark_sym_for_renaming (var);
    }
}


/* Return TRUE if pointer PTR may point to variable VAR.
   
   MEM_ALIAS_SET is the alias set for the memory location pointed-to by PTR
	This is needed because when checking for type conflicts we are
	interested in the alias set of the memory location pointed-to by
	PTR.  The alias set of PTR itself is irrelevant.
   
   VAR_ALIAS_SET is the alias set for VAR.  */

static bool
may_alias_p (tree ptr, HOST_WIDE_INT mem_alias_set,
	     tree var, HOST_WIDE_INT var_alias_set,
	     bool alias_set_only)
{
  tree mem;

  alias_stats.alias_queries++;
  alias_stats.simple_queries++;

  /* By convention, a variable cannot alias itself.  */
  mem = var_ann (ptr)->symbol_mem_tag;
  if (mem == var)
    {
      alias_stats.alias_noalias++;
      alias_stats.simple_resolved++;
      return false;
    }

  /* If -fargument-noalias-global is > 2, pointer arguments may
     not point to anything else.  */
  if (flag_argument_noalias > 2 && TREE_CODE (ptr) == PARM_DECL)
    {
      alias_stats.alias_noalias++;
      alias_stats.simple_resolved++;
      return false;
    }

  /* If -fargument-noalias-global is > 1, pointer arguments may
     not point to global variables.  */
  if (flag_argument_noalias > 1 && is_global_var (var)
      && TREE_CODE (ptr) == PARM_DECL)
    {
      alias_stats.alias_noalias++;
      alias_stats.simple_resolved++;
      return false;
    }

  /* If either MEM or VAR is a read-only global and the other one
     isn't, then PTR cannot point to VAR.  */
  if ((unmodifiable_var_p (mem) && !unmodifiable_var_p (var))
      || (unmodifiable_var_p (var) && !unmodifiable_var_p (mem)))
    {
      alias_stats.alias_noalias++;
      alias_stats.simple_resolved++;
      return false;
    }

  gcc_assert (TREE_CODE (mem) == SYMBOL_MEMORY_TAG);

  alias_stats.tbaa_queries++;

  /* If the alias sets don't conflict then MEM cannot alias VAR.  */
  if (!alias_sets_conflict_p (mem_alias_set, var_alias_set))
    {
      alias_stats.alias_noalias++;
      alias_stats.tbaa_resolved++;
      return false;
    }

  /* If var is a record or union type, ptr cannot point into var
     unless there is some operation explicit address operation in the
     program that can reference a field of the ptr's dereferenced
     type.  This also assumes that the types of both var and ptr are
     contained within the compilation unit, and that there is no fancy
     addressing arithmetic associated with any of the types
     involved.  */

  if ((mem_alias_set != 0) && (var_alias_set != 0))
    {
      tree ptr_type = TREE_TYPE (ptr);
      tree var_type = TREE_TYPE (var);
      
      /* The star count is -1 if the type at the end of the pointer_to 
	 chain is not a record or union type. */ 
      if ((!alias_set_only) && 
	  ipa_type_escape_star_count_of_interesting_type (var_type) >= 0)
	{
	  int ptr_star_count = 0;
	  
	  /* Ipa_type_escape_star_count_of_interesting_type is a little to
	     restrictive for the pointer type, need to allow pointers to
	     primitive types as long as those types cannot be pointers
	     to everything.  */
	  while (POINTER_TYPE_P (ptr_type))
	    /* Strip the *'s off.  */ 
	    {
	      ptr_type = TREE_TYPE (ptr_type);
	      ptr_star_count++;
	    }
	  
	  /* There does not appear to be a better test to see if the 
	     pointer type was one of the pointer to everything 
	     types.  */
	  
	  if (ptr_star_count > 0)
	    {
	      alias_stats.structnoaddress_queries++;
	      if (ipa_type_escape_field_does_not_clobber_p (var_type, 
							    TREE_TYPE (ptr))) 
		{
		  alias_stats.structnoaddress_resolved++;
		  alias_stats.alias_noalias++;
		  return false;
		}
	    }
	  else if (ptr_star_count == 0)
	    {
	      /* If ptr_type was not really a pointer to type, it cannot 
		 alias.  */ 
	      alias_stats.structnoaddress_queries++;
	      alias_stats.structnoaddress_resolved++;
	      alias_stats.alias_noalias++;
	      return false;
	    }
	}
    }

  alias_stats.alias_mayalias++;
  return true;
}


/* Add ALIAS to the set of variables that may alias VAR.  */

static void
add_may_alias (tree var, tree alias)
{
  size_t i;
  var_ann_t v_ann = get_var_ann (var);
  var_ann_t a_ann = get_var_ann (alias);
  tree al;

  /* Don't allow self-referential aliases.  */
  gcc_assert (var != alias);

  /* ALIAS must be addressable if it's being added to an alias set.  */
#if 1
  TREE_ADDRESSABLE (alias) = 1;
#else
  gcc_assert (may_be_aliased (alias));
#endif

  if (v_ann->may_aliases == NULL)
    v_ann->may_aliases = VEC_alloc (tree, gc, 2);

  /* Avoid adding duplicates.  */
  for (i = 0; VEC_iterate (tree, v_ann->may_aliases, i, al); i++)
    if (alias == al)
      return;

  VEC_safe_push (tree, gc, v_ann->may_aliases, alias);
  a_ann->is_aliased = 1;
}


/* Replace alias I in the alias sets of VAR with NEW_ALIAS.  */

static void
replace_may_alias (tree var, size_t i, tree new_alias)
{
  var_ann_t v_ann = var_ann (var);
  VEC_replace (tree, v_ann->may_aliases, i, new_alias);
}


/* Mark pointer PTR as pointing to an arbitrary memory location.  */

static void
set_pt_anything (tree ptr)
{
  struct ptr_info_def *pi = get_ptr_info (ptr);

  pi->pt_anything = 1;
  pi->pt_vars = NULL;

  /* The pointer used to have a name tag, but we now found it pointing
     to an arbitrary location.  The name tag needs to be renamed and
     disassociated from PTR.  */
  if (pi->name_mem_tag)
    {
      mark_sym_for_renaming (pi->name_mem_tag);
      pi->name_mem_tag = NULL_TREE;
    }
}


/* Return true if STMT is an "escape" site from the current function.  Escape
   sites those statements which might expose the address of a variable
   outside the current function.  STMT is an escape site iff:

   	1- STMT is a function call, or
	2- STMT is an __asm__ expression, or
	3- STMT is an assignment to a non-local variable, or
	4- STMT is a return statement.

   Return the type of escape site found, if we found one, or NO_ESCAPE
   if none.  */

enum escape_type
is_escape_site (tree stmt)
{
  tree call = get_call_expr_in (stmt);
  if (call != NULL_TREE)
    {
      if (!TREE_SIDE_EFFECTS (call))
	return ESCAPE_TO_PURE_CONST;

      return ESCAPE_TO_CALL;
    }
  else if (TREE_CODE (stmt) == ASM_EXPR)
    return ESCAPE_TO_ASM;
  else if (TREE_CODE (stmt) == MODIFY_EXPR)
    {
      tree lhs = TREE_OPERAND (stmt, 0);

      /* Get to the base of _REF nodes.  */
      if (TREE_CODE (lhs) != SSA_NAME)
	lhs = get_base_address (lhs);

      /* If we couldn't recognize the LHS of the assignment, assume that it
	 is a non-local store.  */
      if (lhs == NULL_TREE)
	return ESCAPE_UNKNOWN;

      if (TREE_CODE (TREE_OPERAND (stmt, 1)) == NOP_EXPR
	  || TREE_CODE (TREE_OPERAND (stmt, 1)) == CONVERT_EXPR
	  || TREE_CODE (TREE_OPERAND (stmt, 1)) == VIEW_CONVERT_EXPR)
	{
	  tree from = TREE_TYPE (TREE_OPERAND (TREE_OPERAND (stmt, 1), 0));
	  tree to = TREE_TYPE (TREE_OPERAND (stmt, 1));

	  /* If the RHS is a conversion between a pointer and an integer, the
	     pointer escapes since we can't track the integer.  */
	  if (POINTER_TYPE_P (from) && !POINTER_TYPE_P (to))
	    return ESCAPE_BAD_CAST;

	  /* Same if the RHS is a conversion between a regular pointer and a
	     ref-all pointer since we can't track the SMT of the former.  */
	  if (POINTER_TYPE_P (from) && !TYPE_REF_CAN_ALIAS_ALL (from)
	      && POINTER_TYPE_P (to) && TYPE_REF_CAN_ALIAS_ALL (to))
	    return ESCAPE_BAD_CAST;
	}

      /* If the LHS is an SSA name, it can't possibly represent a non-local
	 memory store.  */
      if (TREE_CODE (lhs) == SSA_NAME)
	return NO_ESCAPE;

      /* FIXME: LHS is not an SSA_NAME.  Even if it's an assignment to a
	 local variables we cannot be sure if it will escape, because we
	 don't have information about objects not in SSA form.  Need to
	 implement something along the lines of

	 J.-D. Choi, M. Gupta, M. J. Serrano, V. C. Sreedhar, and S. P.
	 Midkiff, ``Escape analysis for java,'' in Proceedings of the
	 Conference on Object-Oriented Programming Systems, Languages, and
	 Applications (OOPSLA), pp. 1-19, 1999.  */
      return ESCAPE_STORED_IN_GLOBAL;
    }
  else if (TREE_CODE (stmt) == RETURN_EXPR)
    return ESCAPE_TO_RETURN;

  return NO_ESCAPE;
}

/* Create a new memory tag of type TYPE.
   Does NOT push it into the current binding.  */

static tree
create_tag_raw (enum tree_code code, tree type, const char *prefix)
{
  tree tmp_var;
  tree new_type;

  /* Make the type of the variable writable.  */
  new_type = build_type_variant (type, 0, 0);
  TYPE_ATTRIBUTES (new_type) = TYPE_ATTRIBUTES (type);

  tmp_var = build_decl (code, create_tmp_var_name (prefix),
			type);
  /* Make the variable writable.  */
  TREE_READONLY (tmp_var) = 0;

  /* It doesn't start out global.  */
  MTAG_GLOBAL (tmp_var) = 0;
  TREE_STATIC (tmp_var) = 0;
  TREE_USED (tmp_var) = 1;

  return tmp_var;
}

/* Create a new memory tag of type TYPE.  If IS_TYPE_TAG is true, the tag
   is considered to represent all the pointers whose pointed-to types are
   in the same alias set class.  Otherwise, the tag represents a single
   SSA_NAME pointer variable.  */

static tree
create_memory_tag (tree type, bool is_type_tag)
{
  var_ann_t ann;
  tree tag = create_tag_raw (is_type_tag ? SYMBOL_MEMORY_TAG : NAME_MEMORY_TAG,
			     type, (is_type_tag) ? "SMT" : "NMT");

  /* By default, memory tags are local variables.  Alias analysis will
     determine whether they should be considered globals.  */
  DECL_CONTEXT (tag) = current_function_decl;

  /* Memory tags are by definition addressable.  */
  TREE_ADDRESSABLE (tag) = 1;

  ann = get_var_ann (tag);
  ann->symbol_mem_tag = NULL_TREE;

  /* Add the tag to the symbol table.  */
  add_referenced_var (tag);

  return tag;
}


/* Create a name memory tag to represent a specific SSA_NAME pointer P_i.
   This is used if P_i has been found to point to a specific set of
   variables or to a non-aliased memory location like the address returned
   by malloc functions.  */

static tree
get_nmt_for (tree ptr)
{
  struct ptr_info_def *pi = get_ptr_info (ptr);
  tree tag = pi->name_mem_tag;

  if (tag == NULL_TREE)
    tag = create_memory_tag (TREE_TYPE (TREE_TYPE (ptr)), false);
  return tag;
}


/* Return the symbol memory tag associated to pointer PTR.  A memory
   tag is an artificial variable that represents the memory location
   pointed-to by PTR.  It is used to model the effects of pointer
   de-references on addressable variables.
   
   AI points to the data gathered during alias analysis.  This
   function populates the array AI->POINTERS.  */

static tree
get_tmt_for (tree ptr, struct alias_info *ai)
{
  size_t i;
  tree tag;
  tree tag_type = TREE_TYPE (TREE_TYPE (ptr));
  HOST_WIDE_INT tag_set = get_alias_set (tag_type);

  /* We use a unique memory tag for all the ref-all pointers.  */
  if (PTR_IS_REF_ALL (ptr))
    {
      if (!ai->ref_all_symbol_mem_tag)
	ai->ref_all_symbol_mem_tag = create_memory_tag (void_type_node, true);
      return ai->ref_all_symbol_mem_tag;
    }

  /* To avoid creating unnecessary memory tags, only create one memory tag
     per alias set class.  Note that it may be tempting to group
     memory tags based on conflicting alias sets instead of
     equivalence.  That would be wrong because alias sets are not
     necessarily transitive (as demonstrated by the libstdc++ test
     23_containers/vector/cons/4.cc).  Given three alias sets A, B, C
     such that conflicts (A, B) == true and conflicts (A, C) == true,
     it does not necessarily follow that conflicts (B, C) == true.  */
  for (i = 0, tag = NULL_TREE; i < ai->num_pointers; i++)
    {
      struct alias_map_d *curr = ai->pointers[i];
      tree curr_tag = var_ann (curr->var)->symbol_mem_tag;
      if (tag_set == curr->set)
	{
	  tag = curr_tag;
	  break;
	}
    }

  /* If VAR cannot alias with any of the existing memory tags, create a new
     tag for PTR and add it to the POINTERS array.  */
  if (tag == NULL_TREE)
    {
      struct alias_map_d *alias_map;

      /* If PTR did not have a symbol tag already, create a new SMT.*
	 artificial variable representing the memory location
	 pointed-to by PTR.  */
      if (var_ann (ptr)->symbol_mem_tag == NULL_TREE)
	tag = create_memory_tag (tag_type, true);
      else
	tag = var_ann (ptr)->symbol_mem_tag;

      /* Add PTR to the POINTERS array.  Note that we are not interested in
	 PTR's alias set.  Instead, we cache the alias set for the memory that
	 PTR points to.  */
      alias_map = XCNEW (struct alias_map_d);
      alias_map->var = ptr;
      alias_map->set = tag_set;
      ai->pointers[ai->num_pointers++] = alias_map;
    }

  /* If the pointed-to type is volatile, so is the tag.  */
  TREE_THIS_VOLATILE (tag) |= TREE_THIS_VOLATILE (tag_type);

  /* Make sure that the symbol tag has the same alias set as the
     pointed-to type.  */
  gcc_assert (tag_set == get_alias_set (tag));

  return tag;
}


/* Create GLOBAL_VAR, an artificial global variable to act as a
   representative of all the variables that may be clobbered by function
   calls.  */

static void
create_global_var (void)
{
  global_var = build_decl (VAR_DECL, get_identifier (".GLOBAL_VAR"),
                           void_type_node);
  DECL_ARTIFICIAL (global_var) = 1;
  TREE_READONLY (global_var) = 0;
  DECL_EXTERNAL (global_var) = 1;
  TREE_STATIC (global_var) = 1;
  TREE_USED (global_var) = 1;
  DECL_CONTEXT (global_var) = NULL_TREE;
  TREE_THIS_VOLATILE (global_var) = 0;
  TREE_ADDRESSABLE (global_var) = 0;

  create_var_ann (global_var);
  mark_call_clobbered (global_var, ESCAPE_UNKNOWN);
  add_referenced_var (global_var);
  mark_sym_for_renaming (global_var);
}


/* Dump alias statistics on FILE.  */

static void 
dump_alias_stats (FILE *file)
{
  const char *funcname
    = lang_hooks.decl_printable_name (current_function_decl, 2);
  fprintf (file, "\nAlias statistics for %s\n\n", funcname);
  fprintf (file, "Total alias queries:\t%u\n", alias_stats.alias_queries);
  fprintf (file, "Total alias mayalias results:\t%u\n", 
	   alias_stats.alias_mayalias);
  fprintf (file, "Total alias noalias results:\t%u\n",
	   alias_stats.alias_noalias);
  fprintf (file, "Total simple queries:\t%u\n",
	   alias_stats.simple_queries);
  fprintf (file, "Total simple resolved:\t%u\n",
	   alias_stats.simple_resolved);
  fprintf (file, "Total TBAA queries:\t%u\n",
	   alias_stats.tbaa_queries);
  fprintf (file, "Total TBAA resolved:\t%u\n",
	   alias_stats.tbaa_resolved);
  fprintf (file, "Total non-addressable structure type queries:\t%u\n",
	   alias_stats.structnoaddress_queries);
  fprintf (file, "Total non-addressable structure type resolved:\t%u\n",
	   alias_stats.structnoaddress_resolved);
}
  

/* Dump alias information on FILE.  */

void
dump_alias_info (FILE *file)
{
  size_t i;
  const char *funcname
    = lang_hooks.decl_printable_name (current_function_decl, 2);
  referenced_var_iterator rvi;
  tree var;

  fprintf (file, "\nFlow-insensitive alias information for %s\n\n", funcname);

  fprintf (file, "Aliased symbols\n\n");
  
  FOR_EACH_REFERENCED_VAR (var, rvi)
    {
      if (may_be_aliased (var))
	dump_variable (file, var);
    }

  fprintf (file, "\nDereferenced pointers\n\n");

  FOR_EACH_REFERENCED_VAR (var, rvi)
    {
      var_ann_t ann = var_ann (var);
      if (ann->symbol_mem_tag)
	dump_variable (file, var);
    }

  fprintf (file, "\nSymbol memory tags\n\n");
  
  FOR_EACH_REFERENCED_VAR (var, rvi)
    {
      if (TREE_CODE (var) == SYMBOL_MEMORY_TAG)
	dump_variable (file, var);
    }

  fprintf (file, "\n\nFlow-sensitive alias information for %s\n\n", funcname);

  fprintf (file, "SSA_NAME pointers\n\n");
  for (i = 1; i < num_ssa_names; i++)
    {
      tree ptr = ssa_name (i);
      struct ptr_info_def *pi;
      
      if (ptr == NULL_TREE)
	continue;

      pi = SSA_NAME_PTR_INFO (ptr);
      if (!SSA_NAME_IN_FREE_LIST (ptr)
	  && pi
	  && pi->name_mem_tag)
	dump_points_to_info_for (file, ptr);
    }

  fprintf (file, "\nName memory tags\n\n");
  
  FOR_EACH_REFERENCED_VAR (var, rvi)
    {
      if (TREE_CODE (var) == NAME_MEMORY_TAG)
	dump_variable (file, var);
    }

  fprintf (file, "\n");
}


/* Dump alias information on stderr.  */

void
debug_alias_info (void)
{
  dump_alias_info (stderr);
}


/* Return the alias information associated with pointer T.  It creates a
   new instance if none existed.  */

struct ptr_info_def *
get_ptr_info (tree t)
{
  struct ptr_info_def *pi;

  gcc_assert (POINTER_TYPE_P (TREE_TYPE (t)));

  pi = SSA_NAME_PTR_INFO (t);
  if (pi == NULL)
    {
      pi = GGC_NEW (struct ptr_info_def);
      memset ((void *)pi, 0, sizeof (*pi));
      SSA_NAME_PTR_INFO (t) = pi;
    }

  return pi;
}


/* Dump points-to information for SSA_NAME PTR into FILE.  */

void
dump_points_to_info_for (FILE *file, tree ptr)
{
  struct ptr_info_def *pi = SSA_NAME_PTR_INFO (ptr);

  print_generic_expr (file, ptr, dump_flags);

  if (pi)
    {
      if (pi->name_mem_tag)
	{
	  fprintf (file, ", name memory tag: ");
	  print_generic_expr (file, pi->name_mem_tag, dump_flags);
	}

      if (pi->is_dereferenced)
	fprintf (file, ", is dereferenced");

      if (pi->value_escapes_p)
	fprintf (file, ", its value escapes");

      if (pi->pt_anything)
	fprintf (file, ", points-to anything");

      if (pi->pt_null)
	fprintf (file, ", points-to NULL");

      if (pi->pt_vars)
	{
	  unsigned ix;
	  bitmap_iterator bi;

	  fprintf (file, ", points-to vars: { ");
	  EXECUTE_IF_SET_IN_BITMAP (pi->pt_vars, 0, ix, bi)
	    {
	      print_generic_expr (file, referenced_var (ix), dump_flags);
	      fprintf (file, " ");
	    }
	  fprintf (file, "}");
	}
    }

  fprintf (file, "\n");
}


/* Dump points-to information for VAR into stderr.  */

void
debug_points_to_info_for (tree var)
{
  dump_points_to_info_for (stderr, var);
}


/* Dump points-to information into FILE.  NOTE: This function is slow, as
   it needs to traverse the whole CFG looking for pointer SSA_NAMEs.  */

void
dump_points_to_info (FILE *file)
{
  basic_block bb;
  block_stmt_iterator si;
  ssa_op_iter iter;
  const char *fname =
    lang_hooks.decl_printable_name (current_function_decl, 2);
  referenced_var_iterator rvi;
  tree var;

  fprintf (file, "\n\nPointed-to sets for pointers in %s\n\n", fname);

  /* First dump points-to information for the default definitions of
     pointer variables.  This is necessary because default definitions are
     not part of the code.  */
  FOR_EACH_REFERENCED_VAR (var, rvi)
    {
      if (POINTER_TYPE_P (TREE_TYPE (var)))
	{
	  tree def = default_def (var);
	  if (def)
	    dump_points_to_info_for (file, def);
	}
    }

  /* Dump points-to information for every pointer defined in the program.  */
  FOR_EACH_BB (bb)
    {
      tree phi;

      for (phi = phi_nodes (bb); phi; phi = PHI_CHAIN (phi))
	{
	  tree ptr = PHI_RESULT (phi);
	  if (POINTER_TYPE_P (TREE_TYPE (ptr)))
	    dump_points_to_info_for (file, ptr);
	}

	for (si = bsi_start (bb); !bsi_end_p (si); bsi_next (&si))
	  {
	    tree stmt = bsi_stmt (si);
	    tree def;
	    FOR_EACH_SSA_TREE_OPERAND (def, stmt, iter, SSA_OP_DEF)
	      if (POINTER_TYPE_P (TREE_TYPE (def)))
		dump_points_to_info_for (file, def);
	  }
    }

  fprintf (file, "\n");
}


/* Dump points-to info pointed to by PTO into STDERR.  */

void
debug_points_to_info (void)
{
  dump_points_to_info (stderr);
}

/* Dump to FILE the list of variables that may be aliasing VAR.  */

void
dump_may_aliases_for (FILE *file, tree var)
{
  VEC(tree, gc) *aliases;
  
  if (TREE_CODE (var) == SSA_NAME)
    var = SSA_NAME_VAR (var);

  aliases = var_ann (var)->may_aliases;
  if (aliases)
    {
      size_t i;
      tree al;
      fprintf (file, "{ ");
      for (i = 0; VEC_iterate (tree, aliases, i, al); i++)
	{
	  print_generic_expr (file, al, dump_flags);
	  fprintf (file, " ");
	}
      fprintf (file, "}");
    }
}


/* Dump to stderr the list of variables that may be aliasing VAR.  */

void
debug_may_aliases_for (tree var)
{
  dump_may_aliases_for (stderr, var);
}

/* Return true if VAR may be aliased.  */

bool
may_be_aliased (tree var)
{
  /* Obviously.  */
  if (TREE_ADDRESSABLE (var))
    return true;

  /* Globally visible variables can have their addresses taken by other
     translation units.  */

  if (MTAG_P (var)
      && (MTAG_GLOBAL (var) || TREE_PUBLIC (var)))
    return true;
  else if (!MTAG_P (var)
      && (DECL_EXTERNAL (var) || TREE_PUBLIC (var)))
    return true;

  /* Automatic variables can't have their addresses escape any other way.
     This must be after the check for global variables, as extern declarations
     do not have TREE_STATIC set.  */
  if (!TREE_STATIC (var))
    return false;

  /* If we're in unit-at-a-time mode, then we must have seen all occurrences
     of address-of operators, and so we can trust TREE_ADDRESSABLE.  Otherwise
     we can only be sure the variable isn't addressable if it's local to the
     current function.  */
  if (flag_unit_at_a_time)
    return false;
  if (decl_function_context (var) == current_function_decl)
    return false;

  return true;
}


/* Given two symbols return TRUE if one is in the alias set of the other.  */
bool
is_aliased_with (tree tag, tree sym)
{
  size_t i;
  VEC(tree,gc) *aliases;
  tree al;

  if (var_ann (sym)->is_aliased)
    {
      aliases = var_ann (tag)->may_aliases;

      if (aliases == NULL)
	return false;

      for (i = 0; VEC_iterate (tree, aliases, i, al); i++)
	if (al == sym)
	  return true;
    }
  else
    {
      aliases = var_ann (sym)->may_aliases;

      if (aliases == NULL)
	return false;

      for (i = 0; VEC_iterate (tree, aliases, i, al); i++)
	if (al == tag)
	  return true;
    }

  return false;
}


/* Given two tags return TRUE if their may-alias sets intersect.  */

bool 
may_aliases_intersect (tree tag1, tree tag2)
{ 
  struct pointer_set_t *set1 = pointer_set_create ();
  unsigned i;
  VEC(tree,gc) *may_aliases1 = may_aliases (tag1); 
  VEC(tree,gc) *may_aliases2 = may_aliases (tag2);
  tree sym;
    
  /* Insert all the symbols from the first may-alias set into the
     pointer-set.  */
  for (i = 0; VEC_iterate (tree, may_aliases1, i, sym); i++)
    pointer_set_insert (set1, sym);

  /* Go through the second may-alias set and check if it contains symbols that
     are common with the first set.  */
  for (i = 0; VEC_iterate (tree, may_aliases2, i, sym); i++)
    if (pointer_set_contains (set1, sym))
      {
       pointer_set_destroy (set1); 
       return true;
      }
  
  pointer_set_destroy (set1);
  return false;
} 


/* The following is based on code in add_stmt_operand to ensure that the
   same defs/uses/vdefs/vuses will be found after replacing a reference
   to var (or ARRAY_REF to var) with an INDIRECT_REF to ptr whose value
   is the address of var.  Return a memtag for the ptr, after adding the 
   proper may_aliases to it (which are the aliases of var, if it has any,
   or var itself).  */

static tree
add_may_alias_for_new_tag (tree tag, tree var)
{
  var_ann_t v_ann = var_ann (var);
  VEC(tree, gc) *aliases = v_ann->may_aliases;

  /* Case 1: |aliases| == 1  */
  if ((aliases != NULL)
      && (VEC_length (tree, aliases) == 1))
    {
      tree ali = VEC_index (tree, aliases, 0);

      if (TREE_CODE (ali) == SYMBOL_MEMORY_TAG)
        return ali;
    }

  /* Case 2: |aliases| == 0  */
  if (aliases == NULL)
    add_may_alias (tag, var);
  else
    {
      /* Case 3: |aliases| > 1  */
      unsigned i;
      tree al;

      for (i = 0; VEC_iterate (tree, aliases, i, al); i++)
        add_may_alias (tag, al);
    }

  return tag;
}

/* Create a new symbol tag for PTR.  Construct the may-alias list of this type
   tag so that it has the aliasing of VAR, or of the relevant subvars of VAR
   according to the location accessed by EXPR.

   Note, the set of aliases represented by the new symbol tag are not marked
   for renaming.  */

void
new_type_alias (tree ptr, tree var, tree expr)
{
  var_ann_t p_ann = var_ann (ptr);
  tree tag_type = TREE_TYPE (TREE_TYPE (ptr));
  tree tag;
  subvar_t svars;
  tree ali = NULL_TREE;
  HOST_WIDE_INT offset, size, maxsize;
  tree ref;

  gcc_assert (p_ann->symbol_mem_tag == NULL_TREE);
  gcc_assert (!MTAG_P (var));

  ref = get_ref_base_and_extent (expr, &offset, &size, &maxsize);
  gcc_assert (ref);

  tag = create_memory_tag (tag_type, true);
  p_ann->symbol_mem_tag = tag;

  /* Add VAR to the may-alias set of PTR's new symbol tag.  If VAR has
     subvars, add the subvars to the tag instead of the actual var.  */
  if (var_can_have_subvars (var)
      && (svars = get_subvars_for_var (var)))
    {
      subvar_t sv;
      VEC (tree, heap) *overlaps = NULL;
      unsigned int len;

      for (sv = svars; sv; sv = sv->next)
	{
          bool exact;

          if (overlap_subvar (offset, maxsize, sv->var, &exact))
            VEC_safe_push (tree, heap, overlaps, sv->var);
        }
      len = VEC_length (tree, overlaps);
      if (dump_file && (dump_flags & TDF_DETAILS))
        fprintf (dump_file, "\nnumber of overlapping subvars = %u\n", len);
      gcc_assert (len);

      if (len == 1)
        ali = add_may_alias_for_new_tag (tag, VEC_index (tree, overlaps, 0));
      else if (len > 1)
        {
	  unsigned int k;
	  tree sv_var;

	  for (k = 0; VEC_iterate (tree, overlaps, k, sv_var); k++)
	    {
	      ali = add_may_alias_for_new_tag (tag, sv_var);

	      if (ali != tag)
		{
		  /* Can happen only if 'Case 1' of add_may_alias_for_new_tag
		     took place.  Since more than one svar was found, we add 
		     'ali' as one of the may_aliases of the new tag.  */ 
		  add_may_alias (tag, ali);
		  ali = tag;
		}
	    }
	}
    }
  else
    ali = add_may_alias_for_new_tag (tag, var);

  p_ann->symbol_mem_tag = ali;
  TREE_READONLY (tag) = TREE_READONLY (var);
  MTAG_GLOBAL (tag) = is_global_var (var);
}

/* This represents the used range of a variable.  */

typedef struct used_part
{
  HOST_WIDE_INT minused;
  HOST_WIDE_INT maxused;
  /* True if we have an explicit use/def of some portion of this variable,
     even if it is all of it. i.e. a.b = 5 or temp = a.b.  */
  bool explicit_uses;
  /* True if we have an implicit use/def of some portion of this
     variable.  Implicit uses occur when we can't tell what part we
     are referencing, and have to make conservative assumptions.  */
  bool implicit_uses;
  /* True if the structure is only written to or taken its address.  */
  bool write_only;
} *used_part_t;

/* An array of used_part structures, indexed by variable uid.  */

static htab_t used_portions;

struct used_part_map
{
  unsigned int uid;
  used_part_t to;
};

/* Return true if the uid in the two used part maps are equal.  */

static int
used_part_map_eq (const void *va, const void *vb)
{
  const struct used_part_map *a = (const struct used_part_map *) va;
  const struct used_part_map *b = (const struct used_part_map *) vb;
  return (a->uid == b->uid);
}

/* Hash a from uid in a used_part_map.  */

static unsigned int
used_part_map_hash (const void *item)
{
  return ((const struct used_part_map *)item)->uid;
}

/* Free a used part map element.  */

static void 
free_used_part_map (void *item)
{
  free (((struct used_part_map *)item)->to);
  free (item);
}

/* Lookup a used_part structure for a UID.  */

static used_part_t
up_lookup (unsigned int uid)
{
  struct used_part_map *h, in;
  in.uid = uid;
  h = (struct used_part_map *) htab_find_with_hash (used_portions, &in, uid);
  if (!h)
    return NULL;
  return h->to;
}

/* Insert the pair UID, TO into the used part hashtable.  */
 
static void 
up_insert (unsigned int uid, used_part_t to)
{ 
  struct used_part_map *h;
  void **loc;

  h = XNEW (struct used_part_map);
  h->uid = uid;
  h->to = to;
  loc = htab_find_slot_with_hash (used_portions, h,
				  uid, INSERT);
  if (*loc != NULL)
    free (*loc);
  *(struct used_part_map **)  loc = h;
}


/* Given a variable uid, UID, get or create the entry in the used portions
   table for the variable.  */

static used_part_t
get_or_create_used_part_for (size_t uid)
{
  used_part_t up;
  if ((up = up_lookup (uid)) == NULL)
    {
      up = XCNEW (struct used_part);
      up->minused = INT_MAX;
      up->maxused = 0;
      up->explicit_uses = false;
      up->implicit_uses = false;
      up->write_only = true;
    }

  return up;
}


/* Create and return a structure sub-variable for field type FIELD at
   offset OFFSET, with size SIZE, of variable VAR.  */

static tree
create_sft (tree var, tree field, unsigned HOST_WIDE_INT offset,
	    unsigned HOST_WIDE_INT size)
{
  var_ann_t ann;
  tree subvar = create_tag_raw (STRUCT_FIELD_TAG, field, "SFT");

  /* We need to copy the various flags from VAR to SUBVAR, so that
     they are is_global_var iff the original variable was.  */
  DECL_CONTEXT (subvar) = DECL_CONTEXT (var);
  MTAG_GLOBAL (subvar) = DECL_EXTERNAL (var);
  TREE_PUBLIC  (subvar) = TREE_PUBLIC (var);
  TREE_STATIC (subvar) = TREE_STATIC (var);
  TREE_READONLY (subvar) = TREE_READONLY (var);
  TREE_ADDRESSABLE (subvar) = TREE_ADDRESSABLE (var);

  /* Add the new variable to REFERENCED_VARS.  */
  ann = get_var_ann (subvar);
  ann->symbol_mem_tag = NULL;  	
  add_referenced_var (subvar);
  SFT_PARENT_VAR (subvar) = var;
  SFT_OFFSET (subvar) = offset;
  SFT_SIZE (subvar) = size;
  return subvar;
}


/* Given an aggregate VAR, create the subvariables that represent its
   fields.  */

static void
create_overlap_variables_for (tree var)
{
  VEC(fieldoff_s,heap) *fieldstack = NULL;
  used_part_t up;
  size_t uid = DECL_UID (var);

  up = up_lookup (uid);
  if (!up
      || up->write_only)
    return;

  push_fields_onto_fieldstack (TREE_TYPE (var), &fieldstack, 0, NULL);
  if (VEC_length (fieldoff_s, fieldstack) != 0)
    {
      subvar_t *subvars;
      fieldoff_s *fo;
      bool notokay = false;
      int fieldcount = 0;
      int i;
      HOST_WIDE_INT lastfooffset = -1;
      HOST_WIDE_INT lastfosize = -1;
      tree lastfotype = NULL_TREE;

      /* Not all fields have DECL_SIZE set, and those that don't, we don't
	 know their size, and thus, can't handle.
	 The same is true of fields with DECL_SIZE that is not an integer
	 constant (such as variable sized fields).
	 Fields with offsets which are not constant will have an offset < 0 
	 We *could* handle fields that are constant sized arrays, but
	 currently don't.  Doing so would require some extra changes to
	 tree-ssa-operands.c.  */

      for (i = 0; VEC_iterate (fieldoff_s, fieldstack, i, fo); i++)
	{
	  if (!fo->size
	      || TREE_CODE (fo->size) != INTEGER_CST
	      || fo->offset < 0)
	    {
	      notokay = true;
	      break;
	    }
          fieldcount++;
	}

      /* The current heuristic we use is as follows:
	 If the variable has no used portions in this function, no
	 structure vars are created for it.
	 Otherwise,
         If the variable has less than SALIAS_MAX_IMPLICIT_FIELDS,
	 we always create structure vars for them.
	 If the variable has more than SALIAS_MAX_IMPLICIT_FIELDS, and
	 some explicit uses, we create structure vars for them.
	 If the variable has more than SALIAS_MAX_IMPLICIT_FIELDS, and
	 no explicit uses, we do not create structure vars for them.
      */
      
      if (fieldcount >= SALIAS_MAX_IMPLICIT_FIELDS
	  && !up->explicit_uses)
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    {
	      fprintf (dump_file, "Variable ");
	      print_generic_expr (dump_file, var, 0);
	      fprintf (dump_file, " has no explicit uses in this function, and is > SALIAS_MAX_IMPLICIT_FIELDS, so skipping\n");
	    }
	  notokay = true;
	}
      
      /* Bail out, if we can't create overlap variables.  */
      if (notokay)
	{
	  VEC_free (fieldoff_s, heap, fieldstack);
	  return;
	}
      
      /* Otherwise, create the variables.  */
      subvars = lookup_subvars_for_var (var);
      
      sort_fieldstack (fieldstack);

      for (i = VEC_length (fieldoff_s, fieldstack);
	   VEC_iterate (fieldoff_s, fieldstack, --i, fo);)
	{
	  subvar_t sv;
	  HOST_WIDE_INT fosize;
	  tree currfotype;

	  fosize = TREE_INT_CST_LOW (fo->size);
	  currfotype = fo->type;

	  /* If this field isn't in the used portion,
	     or it has the exact same offset and size as the last
	     field, skip it.  */

	  if (((fo->offset <= up->minused
		&& fo->offset + fosize <= up->minused)
	       || fo->offset >= up->maxused)
	      || (fo->offset == lastfooffset
		  && fosize == lastfosize
		  && currfotype == lastfotype))
	    continue;
	  sv = GGC_NEW (struct subvar);
	  sv->next = *subvars;
	  sv->var = create_sft (var, fo->type, fo->offset, fosize);

	  if (dump_file)
	    {
	      fprintf (dump_file, "structure field tag %s created for var %s",
		       get_name (sv->var), get_name (var));
	      fprintf (dump_file, " offset " HOST_WIDE_INT_PRINT_DEC,
		       SFT_OFFSET (sv->var));
	      fprintf (dump_file, " size " HOST_WIDE_INT_PRINT_DEC,
		       SFT_SIZE (sv->var));
	      fprintf (dump_file, "\n");
	    }
	  
	  lastfotype = currfotype;
	  lastfooffset = fo->offset;
	  lastfosize = fosize;
	  *subvars = sv;
	}

      /* Once we have created subvars, the original is no longer call
	 clobbered on its own.  Its call clobbered status depends
	 completely on the call clobbered status of the subvars.

	 add_referenced_var in the above loop will take care of
	 marking subvars of global variables as call clobbered for us
	 to start, since they are global as well.  */
      clear_call_clobbered (var);
    }

  VEC_free (fieldoff_s, heap, fieldstack);
}


/* Find the conservative answer to the question of what portions of what 
   structures are used by this statement.  We assume that if we have a
   component ref with a known size + offset, that we only need that part
   of the structure.  For unknown cases, or cases where we do something
   to the whole structure, we assume we need to create fields for the 
   entire structure.  */

static tree
find_used_portions (tree *tp, int *walk_subtrees, void *lhs_p)
{
  switch (TREE_CODE (*tp))
    {
    case MODIFY_EXPR:
      /* Recurse manually here to track whether the use is in the
	 LHS of an assignment.  */
      find_used_portions (&TREE_OPERAND (*tp, 0), walk_subtrees, tp);
      return find_used_portions (&TREE_OPERAND (*tp, 1), walk_subtrees, NULL);
    case REALPART_EXPR:
    case IMAGPART_EXPR:
    case COMPONENT_REF:
    case ARRAY_REF:
      {
	HOST_WIDE_INT bitsize;
	HOST_WIDE_INT bitmaxsize;
	HOST_WIDE_INT bitpos;
	tree ref;
	ref = get_ref_base_and_extent (*tp, &bitpos, &bitsize, &bitmaxsize);
	if (DECL_P (ref)
	    && var_can_have_subvars (ref)
	    && bitmaxsize != -1)
	  {
	    size_t uid = DECL_UID (ref);
	    used_part_t up;

	    up = get_or_create_used_part_for (uid);	    

	    if (bitpos <= up->minused)
	      up->minused = bitpos;
	    if ((bitpos + bitmaxsize >= up->maxused))
	      up->maxused = bitpos + bitmaxsize;

	    if (bitsize == bitmaxsize)
	      up->explicit_uses = true;
	    else
	      up->implicit_uses = true;
	    if (!lhs_p)
	      up->write_only = false;
	    up_insert (uid, up);

	    *walk_subtrees = 0;
	    return NULL_TREE;
	  }
      }
      break;
      /* This is here to make sure we mark the entire base variable as used
	 when you take its address.  Because our used portion analysis is
	 simple, we aren't looking at casts or pointer arithmetic to see what
	 happens when you take the address.  */
    case ADDR_EXPR:
      {
	tree var = get_base_address (TREE_OPERAND (*tp, 0));

	if (var 
	    && DECL_P (var)
	    && DECL_SIZE (var)
	    && var_can_have_subvars (var)
	    && TREE_CODE (DECL_SIZE (var)) == INTEGER_CST)
	  {
	    used_part_t up;
	    size_t uid = DECL_UID (var);
	    
	    up = get_or_create_used_part_for (uid);
 
	    up->minused = 0;
	    up->maxused = TREE_INT_CST_LOW (DECL_SIZE (var));
	    up->implicit_uses = true;
	    if (!lhs_p)
	      up->write_only = false;

	    up_insert (uid, up);
	    *walk_subtrees = 0;
	    return NULL_TREE;
	  }
      }
      break;
    case CALL_EXPR:
      {
	tree *arg;
	for (arg = &TREE_OPERAND (*tp, 1); *arg; arg = &TREE_CHAIN (*arg))
	  {
	    if (TREE_CODE (TREE_VALUE (*arg)) != ADDR_EXPR)
              find_used_portions (&TREE_VALUE (*arg), walk_subtrees, NULL);
	  }
	*walk_subtrees = 0;
	return NULL_TREE;
      }
    case VAR_DECL:
    case PARM_DECL:
    case RESULT_DECL:
      {
	tree var = *tp;
	if (DECL_SIZE (var)
	    && var_can_have_subvars (var)
	    && TREE_CODE (DECL_SIZE (var)) == INTEGER_CST)
	  {
	    used_part_t up;
	    size_t uid = DECL_UID (var);
	    
	    up = get_or_create_used_part_for (uid);
 
	    up->minused = 0;
	    up->maxused = TREE_INT_CST_LOW (DECL_SIZE (var));
	    up->implicit_uses = true;

	    up_insert (uid, up);
	    *walk_subtrees = 0;
	    return NULL_TREE;
	  }
      }
      break;
      
    default:
      break;
      
    }
  return NULL_TREE;
}

/* Create structure field variables for structures used in this function.  */

static unsigned int
create_structure_vars (void)
{
  basic_block bb;
  safe_referenced_var_iterator rvi;
  VEC (tree, heap) *varvec = NULL;
  tree var;

  used_portions = htab_create (10, used_part_map_hash, used_part_map_eq, 
                               free_used_part_map);
  
  FOR_EACH_BB (bb)
    {
      block_stmt_iterator bsi;
      for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
	{
	  walk_tree_without_duplicates (bsi_stmt_ptr (bsi), 
					find_used_portions,
					NULL);
	}
    }
  FOR_EACH_REFERENCED_VAR_SAFE (var, varvec, rvi)
    {
      /* The C++ FE creates vars without DECL_SIZE set, for some reason.  */
      if (var 	  
	  && DECL_SIZE (var)
	  && var_can_have_subvars (var)
	  && !MTAG_P (var)
	  && TREE_CODE (DECL_SIZE (var)) == INTEGER_CST)
	create_overlap_variables_for (var);
    }
  htab_delete (used_portions);
  VEC_free (tree, heap, varvec);
  return 0;
}

static bool
gate_structure_vars (void)
{
  return flag_tree_salias != 0;
}

struct tree_opt_pass pass_create_structure_vars = 
{
  "salias",		 /* name */
  gate_structure_vars,	 /* gate */
  create_structure_vars, /* execute */
  NULL,			 /* sub */
  NULL,			 /* next */
  0,			 /* static_pass_number */
  0,			 /* tv_id */
  PROP_cfg,		 /* properties_required */
  0,			 /* properties_provided */
  0,			 /* properties_destroyed */
  0,			 /* todo_flags_start */
  TODO_dump_func,	 /* todo_flags_finish */
  0			 /* letter */
};

/* Reset the DECL_CALL_CLOBBERED flags on our referenced vars.  In
   theory, this only needs to be done for globals.  */

static unsigned int
reset_cc_flags (void)
{
  tree var;
  referenced_var_iterator rvi;

  FOR_EACH_REFERENCED_VAR (var, rvi)
    DECL_CALL_CLOBBERED (var) = false;
  return 0;
}

struct tree_opt_pass pass_reset_cc_flags =
{
  NULL,		 /* name */
  NULL,  	 /* gate */
  reset_cc_flags, /* execute */
  NULL,			 /* sub */
  NULL,			 /* next */
  0,			 /* static_pass_number */
  0,			 /* tv_id */
  PROP_referenced_vars |PROP_cfg, /* properties_required */
  0,			 /* properties_provided */
  0,			 /* properties_destroyed */
  0,			 /* todo_flags_start */
  0,         	         /* todo_flags_finish */
  0			 /* letter */
};
