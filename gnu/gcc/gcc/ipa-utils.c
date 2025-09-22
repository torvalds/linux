/* Utilities for ipa analysis.
   Copyright (C) 2005 Free Software Foundation, Inc.
   Contributed by Kenneth Zadeck <zadeck@naturalbridge.com>

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

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "tree-flow.h"
#include "tree-inline.h"
#include "tree-pass.h"
#include "langhooks.h"
#include "pointer-set.h"
#include "ggc.h"
#include "ipa-utils.h"
#include "ipa-reference.h"
#include "c-common.h"
#include "tree-gimple.h"
#include "cgraph.h"
#include "output.h"
#include "flags.h"
#include "timevar.h"
#include "diagnostic.h"
#include "langhooks.h"

/* Debugging function for postorder and inorder code. NOTE is a string
   that is printed before the nodes are printed.  ORDER is an array of
   cgraph_nodes that has COUNT useful nodes in it.  */

void 
ipa_utils_print_order (FILE* out, 
		       const char * note, 
		       struct cgraph_node** order, 
		       int count) 
{
  int i;
  fprintf (out, "\n\n ordered call graph: %s\n", note);
  
  for (i = count - 1; i >= 0; i--)
    dump_cgraph_node(dump_file, order[i]);
  fprintf (out, "\n");
  fflush(out);
}


struct searchc_env {
  struct cgraph_node **stack;
  int stack_size;
  struct cgraph_node **result;
  int order_pos;
  splay_tree nodes_marked_new;
  bool reduce;
  int count;
};

/* This is an implementation of Tarjan's strongly connected region
   finder as reprinted in Aho Hopcraft and Ullman's The Design and
   Analysis of Computer Programs (1975) pages 192-193.  This version
   has been customized for cgraph_nodes.  The env parameter is because
   it is recursive and there are no nested functions here.  This
   function should only be called from itself or
   cgraph_reduced_inorder.  ENV is a stack env and would be
   unnecessary if C had nested functions.  V is the node to start
   searching from.  */

static void
searchc (struct searchc_env* env, struct cgraph_node *v) 
{
  struct cgraph_edge *edge;
  struct ipa_dfs_info *v_info = v->aux;
  
  /* mark node as old */
  v_info->new = false;
  splay_tree_remove (env->nodes_marked_new, v->uid);
  
  v_info->dfn_number = env->count;
  v_info->low_link = env->count;
  env->count++;
  env->stack[(env->stack_size)++] = v;
  v_info->on_stack = true;
  
  for (edge = v->callees; edge; edge = edge->next_callee)
    {
      struct ipa_dfs_info * w_info;
      struct cgraph_node *w = edge->callee;
      /* Bypass the clones and only look at the master node.  Skip
	 external and other bogus nodes.  */
      w = cgraph_master_clone (w);
      if (w && w->aux) 
	{
	  w_info = w->aux;
	  if (w_info->new) 
	    {
	      searchc (env, w);
	      v_info->low_link =
		(v_info->low_link < w_info->low_link) ?
		v_info->low_link : w_info->low_link;
	    } 
	  else 
	    if ((w_info->dfn_number < v_info->dfn_number) 
		&& (w_info->on_stack)) 
	      v_info->low_link =
		(w_info->dfn_number < v_info->low_link) ?
		w_info->dfn_number : v_info->low_link;
	}
    }


  if (v_info->low_link == v_info->dfn_number) 
    {
      struct cgraph_node *last = NULL;
      struct cgraph_node *x;
      struct ipa_dfs_info *x_info;
      do {
	x = env->stack[--(env->stack_size)];
	x_info = x->aux;
	x_info->on_stack = false;
	
	if (env->reduce) 
	  {
	    x_info->next_cycle = last;
	    last = x;
	  } 
	else 
	  env->result[env->order_pos++] = x;
      } 
      while (v != x);
      if (env->reduce) 
	env->result[env->order_pos++] = v;
    }
}

/* Topsort the call graph by caller relation.  Put the result in ORDER.

   The REDUCE flag is true if you want the cycles reduced to single
   nodes.  Only consider nodes that have the output bit set. */

int
ipa_utils_reduced_inorder (struct cgraph_node **order, 
			   bool reduce, bool allow_overwritable)
{
  struct cgraph_node *node;
  struct searchc_env env;
  splay_tree_node result;
  env.stack = XCNEWVEC (struct cgraph_node *, cgraph_n_nodes);
  env.stack_size = 0;
  env.result = order;
  env.order_pos = 0;
  env.nodes_marked_new = splay_tree_new (splay_tree_compare_ints, 0, 0);
  env.count = 1;
  env.reduce = reduce;
  
  for (node = cgraph_nodes; node; node = node->next) 
    if ((node->analyzed)
	&& (cgraph_is_master_clone (node) 
	 || (allow_overwritable 
	     && (cgraph_function_body_availability (node) == 
		 AVAIL_OVERWRITABLE))))
      {
	/* Reuse the info if it is already there.  */
	struct ipa_dfs_info *info = node->aux;
	if (!info)
	  info = xcalloc (1, sizeof (struct ipa_dfs_info));
	info->new = true;
	info->on_stack = false;
	info->next_cycle = NULL;
	node->aux = info;
	
	splay_tree_insert (env.nodes_marked_new,
			   (splay_tree_key)node->uid, 
			   (splay_tree_value)node);
      } 
    else 
      node->aux = NULL;
  result = splay_tree_min (env.nodes_marked_new);
  while (result)
    {
      node = (struct cgraph_node *)result->value;
      searchc (&env, node);
      result = splay_tree_min (env.nodes_marked_new);
    }
  splay_tree_delete (env.nodes_marked_new);
  free (env.stack);

  return env.order_pos;
}


/* Given a memory reference T, will return the variable at the bottom
   of the access.  Unlike get_base_address, this will recurse thru
   INDIRECT_REFS.  */

tree
get_base_var (tree t)
{
  if ((TREE_CODE (t) == EXC_PTR_EXPR) || (TREE_CODE (t) == FILTER_EXPR))
    return t;

  while (!SSA_VAR_P (t) 
	 && (!CONSTANT_CLASS_P (t))
	 && TREE_CODE (t) != LABEL_DECL
	 && TREE_CODE (t) != FUNCTION_DECL
	 && TREE_CODE (t) != CONST_DECL)
    {
      t = TREE_OPERAND (t, 0);
    }
  return t;
} 

