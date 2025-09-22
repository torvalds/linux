/* Interprocedural analyses.
   Copyright (C) 2005 Free Software Foundation, Inc.

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

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tree.h"
#include "langhooks.h"
#include "ggc.h"
#include "target.h"
#include "cgraph.h"
#include "ipa-prop.h"
#include "tree-flow.h"
#include "tree-pass.h"
#include "flags.h"
#include "timevar.h"

/* This file contains interfaces that can be used for various IPA 
   optimizations:

   - ipa_methodlist interface - It is used to create and handle a temporary 
   worklist used in  the propagation stage of IPCP. (can be used for more 
   IPA optimizations).  

   - ipa_callsite interface - for each callsite this interface creates and 
   handles ipa_edge structure associated with it.

   - ipa_method interface - for each method this interface creates and 
   handles ipa_node structure associated with it.  */

/* ipa_methodlist interface.  */

/* Create a new worklist node.  */
static inline ipa_methodlist_p
ipa_create_methodlist_node (void)
{
  return (ipa_methodlist_p) xcalloc (1, sizeof (struct ipa_methodlist));
}

/* Return true if worklist WL is empty.  */
bool
ipa_methodlist_not_empty (ipa_methodlist_p wl)
{
  return (wl != NULL);
}

/* Return the method in worklist element WL.  */
static inline struct cgraph_node *
ipa_methodlist_method (ipa_methodlist_p wl)
{
  return wl->method_p;
}

/* Make worklist element WL point to method MT in the callgraph.  */
static inline void
ipa_methodlist_method_set (ipa_methodlist_p wl, struct cgraph_node *mt)
{
  wl->method_p = mt;
}

/* Return the next element in the worklist following worklist 
   element WL.  */
static inline ipa_methodlist_p
ipa_methodlist_next_method (ipa_methodlist_p wl)
{
  return wl->next_method;
}

/* Set worklist element WL1 to point to worklist element WL2.  */
static inline void
ipa_methodlist_next_method_set (ipa_methodlist_p wl1, ipa_methodlist_p wl2)
{
  wl1->next_method = wl2;
}

/* Initialize worklist to contain all methods.  */
ipa_methodlist_p
ipa_methodlist_init (void)
{
  struct cgraph_node *node;
  ipa_methodlist_p wl;

  wl = NULL;
  for (node = cgraph_nodes; node; node = node->next)
    ipa_add_method (&wl, node);

  return wl;
}

/* Add method MT to the worklist. Set worklist element WL  
   to point to MT.  */
void
ipa_add_method (ipa_methodlist_p * wl, struct cgraph_node *mt)
{
  ipa_methodlist_p temp;

  temp = ipa_create_methodlist_node ();
  ipa_methodlist_method_set (temp, mt);
  ipa_methodlist_next_method_set (temp, *wl);
  *wl = temp;
}

/* Remove a method from the worklist. WL points to the first 
   element in the list, which is removed.  */
struct cgraph_node *
ipa_remove_method (ipa_methodlist_p * wl)
{
  ipa_methodlist_p first;
  struct cgraph_node *return_method;

  first = *wl;
  *wl = ipa_methodlist_next_method (*wl);
  return_method = ipa_methodlist_method (first);
  free (first);
  return return_method;
}

/* ipa_method interface.  */

/* Return number of formals of method MT.  */
int
ipa_method_formal_count (struct cgraph_node *mt)
{
  return IPA_NODE_REF (mt)->ipa_arg_num;
}

/* Set number of formals of method MT to I.  */
void
ipa_method_formal_count_set (struct cgraph_node *mt, int i)
{
  IPA_NODE_REF (mt)->ipa_arg_num = i;
}

/* Return whether I-th formal of MT is modified in MT.  */
static inline bool
ipa_method_is_modified (struct cgraph_node *mt, int i)
{
  return IPA_NODE_REF (mt)->ipa_mod[i];
}

/* Return the tree of I-th formal of MT.  */
tree
ipa_method_get_tree (struct cgraph_node *mt, int i)
{
  return IPA_NODE_REF (mt)->ipa_param_tree[i];
}

/* Create tree map structure for MT.  */
static inline void
ipa_method_tree_map_create (struct cgraph_node *mt)
{
  IPA_NODE_REF (mt)->ipa_param_tree =
    XCNEWVEC (tree, ipa_method_formal_count (mt));
}

/* Create modify structure for MT.  */
static inline void
ipa_method_modify_create (struct cgraph_node *mt)
{
  ((struct ipa_node *) mt->aux)->ipa_mod =
    XCNEWVEC (bool, ipa_method_formal_count (mt));
}

/* Set modify of I-th formal of MT to VAL.  */
static inline void
ipa_method_modify_set (struct cgraph_node *mt, int i, bool val)
{
  IPA_NODE_REF (mt)->ipa_mod[i] = val;
}

/* Return index of the formal whose tree is PTREE in method MT.  */
static int
ipa_method_tree_map (struct cgraph_node *mt, tree ptree)
{
  int i, count;

  count = ipa_method_formal_count (mt);
  for (i = 0; i < count; i++)
    if (IPA_NODE_REF (mt)->ipa_param_tree[i] == ptree)
      return i;

  return -1;
}

/* Insert the formal trees to the ipa_param_tree array in method MT.  */
void
ipa_method_compute_tree_map (struct cgraph_node *mt)
{
  tree fndecl;
  tree fnargs;
  tree parm;
  int param_num;

  ipa_method_tree_map_create (mt);
  fndecl = mt->decl;
  fnargs = DECL_ARGUMENTS (fndecl);
  param_num = 0;
  for (parm = fnargs; parm; parm = TREE_CHAIN (parm))
    {
      IPA_NODE_REF (mt)->ipa_param_tree[param_num] = parm;
      param_num++;
    }
}

/* Count number of formals in MT. Insert the result to the 
   ipa_node.  */
void
ipa_method_formal_compute_count (struct cgraph_node *mt)
{
  tree fndecl;
  tree fnargs;
  tree parm;
  int param_num;

  fndecl = mt->decl;
  fnargs = DECL_ARGUMENTS (fndecl);
  param_num = 0;
  for (parm = fnargs; parm; parm = TREE_CHAIN (parm))
    param_num++;
  ipa_method_formal_count_set (mt, param_num);
}

/* Check STMT to detect whether a formal is modified within MT,
   the appropriate entry is updated in the ipa_mod array of ipa_node
   (associated with MT).  */
static void
ipa_method_modify_stmt (struct cgraph_node *mt, tree stmt)
{
  int i, j;

  switch (TREE_CODE (stmt))
    {
    case MODIFY_EXPR:
      if (TREE_CODE (TREE_OPERAND (stmt, 0)) == PARM_DECL)
	{
	  i = ipa_method_tree_map (mt, TREE_OPERAND (stmt, 0));
	  if (i >= 0)
            ipa_method_modify_set (mt, i, true);
	}
      break;
    case ASM_EXPR:
      /* Asm code could modify any of the parameters.  */
      for (j = 0; j < ipa_method_formal_count (mt); j++)
	ipa_method_modify_set (mt, j, true);
      break;
    default:
      break;
    }
}

/* Initialize ipa_mod array of MT.  */
static void
ipa_method_modify_init (struct cgraph_node *mt)
{
  int i, count;

  ipa_method_modify_create (mt);
  count = ipa_method_formal_count (mt);
  for (i = 0; i < count; i++)
    ipa_method_modify_set (mt, i, false);
}

/* The modify computation driver for MT. Compute which formal arguments 
   of method MT are locally modified.  Formals may be modified in MT 
   if their address is taken, or if
   they appear on the left hand side of an assignment.  */
void
ipa_method_compute_modify (struct cgraph_node *mt)
{
  tree decl;
  tree body;
  int j, count;
  basic_block bb;
  struct function *func;
  block_stmt_iterator bsi;
  tree stmt, parm_tree;

  ipa_method_modify_init (mt);
  decl = mt->decl;
  count = ipa_method_formal_count (mt);
  /* ??? Handle pending sizes case. Set all parameters 
     of the method to be modified.  */
  if (DECL_UNINLINABLE (decl))
    {
      for (j = 0; j < count; j++)
	ipa_method_modify_set (mt, j, true);
      return;
    }
  /* Formals whose address is taken are considered modified.  */
  for (j = 0; j < count; j++)
    {
      parm_tree = ipa_method_get_tree (mt, j);
      if (TREE_ADDRESSABLE (parm_tree))
	ipa_method_modify_set (mt, j, true);
    }
  body = DECL_SAVED_TREE (decl);
  if (body != NULL)
    {
      func = DECL_STRUCT_FUNCTION (decl);
      FOR_EACH_BB_FN (bb, func)
      {
	for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
	  {
	    stmt = bsi_stmt (bsi);
	    ipa_method_modify_stmt (mt, stmt);
	  }
      }
    }
}


/* ipa_callsite interface.  */

/* Return number of arguments in callsite CS.  */
int
ipa_callsite_param_count (struct cgraph_edge *cs)
{
  return IPA_EDGE_REF (cs)->ipa_param_num;
}

/* Set number of arguments in callsite CS to I.  */
void
ipa_callsite_param_count_set (struct cgraph_edge *cs, int i)
{
  IPA_EDGE_REF (cs)->ipa_param_num = i;
}

/* Return the jump function (ipa_jump_func struct) for argument I of 
   callsite CS.  */
struct ipa_jump_func *
ipa_callsite_param (struct cgraph_edge *cs, int i)
{
  return &(IPA_EDGE_REF (cs)->ipa_param_map[i]);
}

/* return the callee (cgraph_node) of callsite CS.  */
struct cgraph_node *
ipa_callsite_callee (struct cgraph_edge *cs)
{
  return cs->callee;
}

/* Set field 'type' of jump function (ipa_jump_func struct) of argument I 
   in callsite CS.  */
static inline void
ipa_callsite_param_set_type (struct cgraph_edge *cs, int i,
			     enum jump_func_type type1)
{
  IPA_EDGE_REF (cs)->ipa_param_map[i].type = type1;
}

/* Set FORMAL as 'info_type' field of jump function (ipa_jump_func struct)
   of argument I of callsite CS.  */
static inline void
ipa_callsite_param_set_info_type_formal (struct cgraph_edge *cs, int i,
					 unsigned int formal)
{
  ipa_callsite_param (cs, i)->info_type.formal_id = formal;
}

/* Set int-valued INFO_TYPE1 as 'info_type' field of 
   jump function (ipa_jump_func struct) of argument I of callsite CS.  */
static inline void
ipa_callsite_param_set_info_type (struct cgraph_edge *cs, int i, tree info_type1)
{
  ipa_callsite_param (cs, i)->info_type.value = info_type1;
}

/* Allocate space for callsite CS.  */
static inline void
ipa_callsite_param_map_create (struct cgraph_edge *cs)
{
  IPA_EDGE_REF (cs)->ipa_param_map =
    XCNEWVEC (struct ipa_jump_func, ipa_callsite_param_count (cs));
}

/* Return the call expr tree related to callsite CS.  */
static inline tree
ipa_callsite_tree (struct cgraph_edge *cs)
{
  return cs->call_stmt;
}

/* Return the caller (cgraph_node) of CS.  */
static inline struct cgraph_node *
ipa_callsite_caller (struct cgraph_edge *cs)
{
  return cs->caller;
}

/* Count number of arguments callsite CS has and store it in 
   ipa_edge structure corresponding to this callsite.  */
void
ipa_callsite_compute_count (struct cgraph_edge *cs)
{
  tree call_tree;
  tree arg;
  int arg_num;

  call_tree = get_call_expr_in (ipa_callsite_tree (cs));
  gcc_assert (TREE_CODE (call_tree) == CALL_EXPR);
  arg = TREE_OPERAND (call_tree, 1);
  arg_num = 0;
  for (; arg != NULL_TREE; arg = TREE_CHAIN (arg))
    arg_num++;
  ipa_callsite_param_count_set (cs, arg_num);
}

/* Compute jump function for all arguments of callsite CS 
   and insert the information in the ipa_param_map array 
   in the ipa_edge corresponding to this callsite. (Explanation 
   on jump functions is in ipa-prop.h).  */
void
ipa_callsite_compute_param (struct cgraph_edge *cs)
{
  tree call_tree;
  tree arg, cst_decl;
  int arg_num;
  int i;
  struct cgraph_node *mt;

  if (ipa_callsite_param_count (cs) == 0)
    return;
  ipa_callsite_param_map_create (cs);
  call_tree = get_call_expr_in (ipa_callsite_tree (cs));
  gcc_assert (TREE_CODE (call_tree) == CALL_EXPR);
  arg = TREE_OPERAND (call_tree, 1);
  arg_num = 0;

  for (; arg != NULL_TREE; arg = TREE_CHAIN (arg))
    {
      /* If the formal parameter was passed as argument, we store 
         FORMAL_IPATYPE and its index in the caller as the jump function 
         of this argument.  */
      if (TREE_CODE (TREE_VALUE (arg)) == PARM_DECL)
	{
	  mt = ipa_callsite_caller (cs);
	  i = ipa_method_tree_map (mt, TREE_VALUE (arg));
	  if (i < 0 || ipa_method_is_modified (mt, i))
	    ipa_callsite_param_set_type (cs, arg_num, UNKNOWN_IPATYPE);
	  else
	    {
	      ipa_callsite_param_set_type (cs, arg_num, FORMAL_IPATYPE);
	      ipa_callsite_param_set_info_type_formal (cs, arg_num, i);
	    }
	}
      /* If a constant value was passed as argument, 
         we store CONST_IPATYPE and its value as the jump function 
         of this argument.  */
      else if (TREE_CODE (TREE_VALUE (arg)) == INTEGER_CST
	       || TREE_CODE (TREE_VALUE (arg)) == REAL_CST)
	{
	  ipa_callsite_param_set_type (cs, arg_num, CONST_IPATYPE);
	  ipa_callsite_param_set_info_type (cs, arg_num,
					    TREE_VALUE (arg));
	}
      /* This is for the case of Fortran. If the address of a const_decl 
         was passed as argument then we store 
         CONST_IPATYPE_REF/CONST_IPATYPE_REF and the constant 
         value as the jump function corresponding to this argument.  */
      else if (TREE_CODE (TREE_VALUE (arg)) == ADDR_EXPR
	       && TREE_CODE (TREE_OPERAND (TREE_VALUE (arg), 0)) ==
	       CONST_DECL)
	{
	  cst_decl = TREE_OPERAND (TREE_VALUE (arg), 0);
	  if (TREE_CODE (DECL_INITIAL (cst_decl)) == INTEGER_CST
	      || TREE_CODE (DECL_INITIAL (cst_decl)) == REAL_CST)
	    {
	      ipa_callsite_param_set_type (cs, arg_num,
					   CONST_IPATYPE_REF);
	      ipa_callsite_param_set_info_type (cs, arg_num,
						DECL_INITIAL (cst_decl));
	    }
	}
      else
	ipa_callsite_param_set_type (cs, arg_num, UNKNOWN_IPATYPE);
      arg_num++;
    }
}

/* Return type of jump function JF.  */
enum jump_func_type
get_type (struct ipa_jump_func *jf)
{
  return jf->type;
}

/* Return info type of jump function JF.  */
union parameter_info *
ipa_jf_get_info_type (struct ipa_jump_func *jf)
{
  return &(jf->info_type);
}

/* Allocate and initialize ipa_node structure.  
   cgraph_node NODE points to the new allocated ipa_node.  */
void
ipa_node_create (struct cgraph_node *node)
{
  node->aux = xcalloc (1, sizeof (struct ipa_node));
}

/* Allocate and initialize ipa_node structure for all
   nodes in callgraph.  */
void
ipa_nodes_create (void)
{
  struct cgraph_node *node;

  for (node = cgraph_nodes; node; node = node->next)
    ipa_node_create (node);
}

/* Allocate and initialize ipa_edge structure.  */
void
ipa_edges_create (void)
{
  struct cgraph_node *node;
  struct cgraph_edge *cs;

  for (node = cgraph_nodes; node; node = node->next)
    for (cs = node->callees; cs; cs = cs->next_callee)
      cs->aux = xcalloc (1, sizeof (struct ipa_edge));
}

/* Free ipa_node structure.  */
void
ipa_nodes_free (void)
{
  struct cgraph_node *node;

  for (node = cgraph_nodes; node; node = node->next)
    {
      free (node->aux);
      node->aux = NULL;
    }
}

/* Free ipa_edge structure.  */
void
ipa_edges_free (void)
{
  struct cgraph_node *node;
  struct cgraph_edge *cs;

  for (node = cgraph_nodes; node; node = node->next)
    for (cs = node->callees; cs; cs = cs->next_callee)
      {
	free (cs->aux);
	cs->aux = NULL;
      }
}

/* Free ipa data structures of ipa_node and ipa_edge.  */
void
ipa_free (void)
{
  struct cgraph_node *node;
  struct cgraph_edge *cs;

  for (node = cgraph_nodes; node; node = node->next)
    {
      if (node->aux == NULL)
	continue;
      if (IPA_NODE_REF (node)->ipcp_cval)
	free (IPA_NODE_REF (node)->ipcp_cval);
      if (IPA_NODE_REF (node)->ipa_param_tree)
	free (IPA_NODE_REF (node)->ipa_param_tree);
      if (IPA_NODE_REF (node)->ipa_mod)
	free (IPA_NODE_REF (node)->ipa_mod);
      for (cs = node->callees; cs; cs = cs->next_callee)
	{
	  if (cs->aux)
	    if (IPA_EDGE_REF (cs)->ipa_param_map)
	      free (IPA_EDGE_REF (cs)->ipa_param_map);
	}
    }
}

/* Print ipa_tree_map data structures of all methods in the 
   callgraph to F.  */
void
ipa_method_tree_print (FILE * f)
{
  int i, count;
  tree temp;
  struct cgraph_node *node;

  fprintf (f, "\nPARAM TREE MAP PRINT\n");
  for (node = cgraph_nodes; node; node = node->next)
    {
      fprintf (f, "method  %s Trees :: \n", cgraph_node_name (node));
      count = ipa_method_formal_count (node);
      for (i = 0; i < count; i++)
	{
	  temp = ipa_method_get_tree (node, i);
	  if (TREE_CODE (temp) == PARM_DECL)
	    fprintf (f, "  param [%d] : %s\n", i,
		     (*lang_hooks.decl_printable_name) (temp, 2));
	}

    }
}

/* Print ipa_modify data structures of all methods in the 
   callgraph to F.  */
void
ipa_method_modify_print (FILE * f)
{
  int i, count;
  bool temp;
  struct cgraph_node *node;

  fprintf (f, "\nMODIFY PRINT\n");
  for (node = cgraph_nodes; node; node = node->next)
    {
      fprintf (f, "method  %s :: \n", cgraph_node_name (node));
      count = ipa_method_formal_count (node);
      for (i = 0; i < count; i++)
	{
	  temp = ipa_method_is_modified (node, i);
	  if (temp)
	    fprintf (f, " param [%d] true \n", i);
	  else
	    fprintf (f, " param [%d] false \n", i);
	}
    }
}
