/* Inline functions for tree-flow.h
   Copyright (C) 2001, 2003, 2005, 2006 Free Software Foundation, Inc.
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

#ifndef _TREE_FLOW_INLINE_H
#define _TREE_FLOW_INLINE_H 1

/* Inline functions for manipulating various data structures defined in
   tree-flow.h.  See tree-flow.h for documentation.  */

/* Initialize the hashtable iterator HTI to point to hashtable TABLE */

static inline void *
first_htab_element (htab_iterator *hti, htab_t table)
{
  hti->htab = table;
  hti->slot = table->entries;
  hti->limit = hti->slot + htab_size (table);
  do
    {
      PTR x = *(hti->slot);
      if (x != HTAB_EMPTY_ENTRY && x != HTAB_DELETED_ENTRY)
	break;
    } while (++(hti->slot) < hti->limit);
  
  if (hti->slot < hti->limit)
    return *(hti->slot);
  return NULL;
}

/* Return current non-empty/deleted slot of the hashtable pointed to by HTI,
   or NULL if we have  reached the end.  */

static inline bool
end_htab_p (htab_iterator *hti)
{
  if (hti->slot >= hti->limit)
    return true;
  return false;
}

/* Advance the hashtable iterator pointed to by HTI to the next element of the
   hashtable.  */

static inline void *
next_htab_element (htab_iterator *hti)
{
  while (++(hti->slot) < hti->limit)
    {
      PTR x = *(hti->slot);
      if (x != HTAB_EMPTY_ENTRY && x != HTAB_DELETED_ENTRY)
	return x;
    };
  return NULL;
}

/* Initialize ITER to point to the first referenced variable in the
   referenced_vars hashtable, and return that variable.  */

static inline tree
first_referenced_var (referenced_var_iterator *iter)
{
  struct int_tree_map *itm;
  itm = (struct int_tree_map *) first_htab_element (&iter->hti,
                                                    referenced_vars);
  if (!itm) 
    return NULL;
  return itm->to;
}

/* Return true if we have hit the end of the referenced variables ITER is
   iterating through.  */

static inline bool
end_referenced_vars_p (referenced_var_iterator *iter)
{
  return end_htab_p (&iter->hti);
}

/* Make ITER point to the next referenced_var in the referenced_var hashtable,
   and return that variable.  */

static inline tree
next_referenced_var (referenced_var_iterator *iter)
{
  struct int_tree_map *itm;
  itm = (struct int_tree_map *) next_htab_element (&iter->hti);
  if (!itm) 
    return NULL;
  return itm->to;
} 

/* Fill up VEC with the variables in the referenced vars hashtable.  */

static inline void
fill_referenced_var_vec (VEC (tree, heap) **vec)
{
  referenced_var_iterator rvi;
  tree var;
  *vec = NULL;
  FOR_EACH_REFERENCED_VAR (var, rvi)
    VEC_safe_push (tree, heap, *vec, var);
}

/* Return the variable annotation for T, which must be a _DECL node.
   Return NULL if the variable annotation doesn't already exist.  */
static inline var_ann_t
var_ann (tree t)
{
  gcc_assert (t);
  gcc_assert (DECL_P (t));
  gcc_assert (TREE_CODE (t) != FUNCTION_DECL);
  gcc_assert (!t->common.ann || t->common.ann->common.type == VAR_ANN);

  return (var_ann_t) t->common.ann;
}

/* Return the variable annotation for T, which must be a _DECL node.
   Create the variable annotation if it doesn't exist.  */
static inline var_ann_t
get_var_ann (tree var)
{
  var_ann_t ann = var_ann (var);
  return (ann) ? ann : create_var_ann (var);
}

/* Return the function annotation for T, which must be a FUNCTION_DECL node.
   Return NULL if the function annotation doesn't already exist.  */
static inline function_ann_t
function_ann (tree t)
{
  gcc_assert (t);
  gcc_assert (TREE_CODE (t) == FUNCTION_DECL);
  gcc_assert (!t->common.ann || t->common.ann->common.type == FUNCTION_ANN);

  return (function_ann_t) t->common.ann;
}

/* Return the function annotation for T, which must be a FUNCTION_DECL node.
   Create the function annotation if it doesn't exist.  */
static inline function_ann_t
get_function_ann (tree var)
{
  function_ann_t ann = function_ann (var);
  gcc_assert (!var->common.ann || var->common.ann->common.type == FUNCTION_ANN);
  return (ann) ? ann : create_function_ann (var);
}

/* Return the statement annotation for T, which must be a statement
   node.  Return NULL if the statement annotation doesn't exist.  */
static inline stmt_ann_t
stmt_ann (tree t)
{
#ifdef ENABLE_CHECKING
  gcc_assert (is_gimple_stmt (t));
#endif
  gcc_assert (!t->common.ann || t->common.ann->common.type == STMT_ANN);
  return (stmt_ann_t) t->common.ann;
}

/* Return the statement annotation for T, which must be a statement
   node.  Create the statement annotation if it doesn't exist.  */
static inline stmt_ann_t
get_stmt_ann (tree stmt)
{
  stmt_ann_t ann = stmt_ann (stmt);
  return (ann) ? ann : create_stmt_ann (stmt);
}

/* Return the annotation type for annotation ANN.  */
static inline enum tree_ann_type
ann_type (tree_ann_t ann)
{
  return ann->common.type;
}

/* Return the basic block for statement T.  */
static inline basic_block
bb_for_stmt (tree t)
{
  stmt_ann_t ann;

  if (TREE_CODE (t) == PHI_NODE)
    return PHI_BB (t);

  ann = stmt_ann (t);
  return ann ? ann->bb : NULL;
}

/* Return the may_aliases varray for variable VAR, or NULL if it has
   no may aliases.  */
static inline VEC(tree, gc) *
may_aliases (tree var)
{
  var_ann_t ann = var_ann (var);
  return ann ? ann->may_aliases : NULL;
}

/* Return the line number for EXPR, or return -1 if we have no line
   number information for it.  */
static inline int
get_lineno (tree expr)
{
  if (expr == NULL_TREE)
    return -1;

  if (TREE_CODE (expr) == COMPOUND_EXPR)
    expr = TREE_OPERAND (expr, 0);

  if (! EXPR_HAS_LOCATION (expr))
    return -1;

  return EXPR_LINENO (expr);
}

/* Return the file name for EXPR, or return "???" if we have no
   filename information.  */
static inline const char *
get_filename (tree expr)
{
  const char *filename;
  if (expr == NULL_TREE)
    return "???";

  if (TREE_CODE (expr) == COMPOUND_EXPR)
    expr = TREE_OPERAND (expr, 0);

  if (EXPR_HAS_LOCATION (expr) && (filename = EXPR_FILENAME (expr)))
    return filename;
  else
    return "???";
}

/* Return true if T is a noreturn call.  */
static inline bool
noreturn_call_p (tree t)
{
  tree call = get_call_expr_in (t);
  return call != 0 && (call_expr_flags (call) & ECF_NORETURN) != 0;
}

/* Mark statement T as modified.  */
static inline void
mark_stmt_modified (tree t)
{
  stmt_ann_t ann;
  if (TREE_CODE (t) == PHI_NODE)
    return;

  ann = stmt_ann (t);
  if (ann == NULL)
    ann = create_stmt_ann (t);
  else if (noreturn_call_p (t))
    VEC_safe_push (tree, gc, modified_noreturn_calls, t);
  ann->modified = 1;
}

/* Mark statement T as modified, and update it.  */
static inline void
update_stmt (tree t)
{
  if (TREE_CODE (t) == PHI_NODE)
    return;
  mark_stmt_modified (t);
  update_stmt_operands (t);
}

static inline void
update_stmt_if_modified (tree t)
{
  if (stmt_modified_p (t))
    update_stmt_operands (t);
}

/* Return true if T is marked as modified, false otherwise.  */
static inline bool
stmt_modified_p (tree t)
{
  stmt_ann_t ann = stmt_ann (t);

  /* Note that if the statement doesn't yet have an annotation, we consider it
     modified.  This will force the next call to update_stmt_operands to scan 
     the statement.  */
  return ann ? ann->modified : true;
}

/* Delink an immediate_uses node from its chain.  */
static inline void
delink_imm_use (ssa_use_operand_t *linknode)
{
  /* Return if this node is not in a list.  */
  if (linknode->prev == NULL)
    return;

  linknode->prev->next = linknode->next;
  linknode->next->prev = linknode->prev;
  linknode->prev = NULL;
  linknode->next = NULL;
}

/* Link ssa_imm_use node LINKNODE into the chain for LIST.  */
static inline void
link_imm_use_to_list (ssa_use_operand_t *linknode, ssa_use_operand_t *list)
{
  /* Link the new node at the head of the list.  If we are in the process of 
     traversing the list, we won't visit any new nodes added to it.  */
  linknode->prev = list;
  linknode->next = list->next;
  list->next->prev = linknode;
  list->next = linknode;
}

/* Link ssa_imm_use node LINKNODE into the chain for DEF.  */
static inline void
link_imm_use (ssa_use_operand_t *linknode, tree def)
{
  ssa_use_operand_t *root;

  if (!def || TREE_CODE (def) != SSA_NAME)
    linknode->prev = NULL;
  else
    {
      root = &(SSA_NAME_IMM_USE_NODE (def));
#ifdef ENABLE_CHECKING
      if (linknode->use)
        gcc_assert (*(linknode->use) == def);
#endif
      link_imm_use_to_list (linknode, root);
    }
}

/* Set the value of a use pointed to by USE to VAL.  */
static inline void
set_ssa_use_from_ptr (use_operand_p use, tree val)
{
  delink_imm_use (use);
  *(use->use) = val;
  link_imm_use (use, val);
}

/* Link ssa_imm_use node LINKNODE into the chain for DEF, with use occurring 
   in STMT.  */
static inline void
link_imm_use_stmt (ssa_use_operand_t *linknode, tree def, tree stmt)
{
  if (stmt)
    link_imm_use (linknode, def);
  else
    link_imm_use (linknode, NULL);
  linknode->stmt = stmt;
}

/* Relink a new node in place of an old node in the list.  */
static inline void
relink_imm_use (ssa_use_operand_t *node, ssa_use_operand_t *old)
{
  /* The node one had better be in the same list.  */
  gcc_assert (*(old->use) == *(node->use));
  node->prev = old->prev;
  node->next = old->next;
  if (old->prev)
    {
      old->prev->next = node;
      old->next->prev = node;
      /* Remove the old node from the list.  */
      old->prev = NULL;
    }
}

/* Relink ssa_imm_use node LINKNODE into the chain for OLD, with use occurring 
   in STMT.  */
static inline void
relink_imm_use_stmt (ssa_use_operand_t *linknode, ssa_use_operand_t *old, tree stmt)
{
  if (stmt)
    relink_imm_use (linknode, old);
  else
    link_imm_use (linknode, NULL);
  linknode->stmt = stmt;
}


/* Return true is IMM has reached the end of the immediate use list.  */
static inline bool
end_readonly_imm_use_p (imm_use_iterator *imm)
{
  return (imm->imm_use == imm->end_p);
}

/* Initialize iterator IMM to process the list for VAR.  */
static inline use_operand_p
first_readonly_imm_use (imm_use_iterator *imm, tree var)
{
  gcc_assert (TREE_CODE (var) == SSA_NAME);

  imm->end_p = &(SSA_NAME_IMM_USE_NODE (var));
  imm->imm_use = imm->end_p->next;
#ifdef ENABLE_CHECKING
  imm->iter_node.next = imm->imm_use->next;
#endif
  if (end_readonly_imm_use_p (imm))
    return NULL_USE_OPERAND_P;
  return imm->imm_use;
}

/* Bump IMM to the next use in the list.  */
static inline use_operand_p
next_readonly_imm_use (imm_use_iterator *imm)
{
  use_operand_p old = imm->imm_use;

#ifdef ENABLE_CHECKING
  /* If this assertion fails, it indicates the 'next' pointer has changed 
     since we the last bump.  This indicates that the list is being modified
     via stmt changes, or SET_USE, or somesuch thing, and you need to be
     using the SAFE version of the iterator.  */
  gcc_assert (imm->iter_node.next == old->next);
  imm->iter_node.next = old->next->next;
#endif

  imm->imm_use = old->next;
  if (end_readonly_imm_use_p (imm))
    return old;
  return imm->imm_use;
}

/* Return true if VAR has no uses.  */
static inline bool
has_zero_uses (tree var)
{
  ssa_use_operand_t *ptr;
  ptr = &(SSA_NAME_IMM_USE_NODE (var));
  /* A single use means there is no items in the list.  */
  return (ptr == ptr->next);
}

/* Return true if VAR has a single use.  */
static inline bool
has_single_use (tree var)
{
  ssa_use_operand_t *ptr;
  ptr = &(SSA_NAME_IMM_USE_NODE (var));
  /* A single use means there is one item in the list.  */
  return (ptr != ptr->next && ptr == ptr->next->next);
}

/* If VAR has only a single immediate use, return true, and set USE_P and STMT
   to the use pointer and stmt of occurrence.  */
static inline bool
single_imm_use (tree var, use_operand_p *use_p, tree *stmt)
{
  ssa_use_operand_t *ptr;

  ptr = &(SSA_NAME_IMM_USE_NODE (var));
  if (ptr != ptr->next && ptr == ptr->next->next)
    {
      *use_p = ptr->next;
      *stmt = ptr->next->stmt;
      return true;
    }
  *use_p = NULL_USE_OPERAND_P;
  *stmt = NULL_TREE;
  return false;
}

/* Return the number of immediate uses of VAR.  */
static inline unsigned int
num_imm_uses (tree var)
{
  ssa_use_operand_t *ptr, *start;
  unsigned int num;

  start = &(SSA_NAME_IMM_USE_NODE (var));
  num = 0;
  for (ptr = start->next; ptr != start; ptr = ptr->next)
     num++;

  return num;
}


/* Return the tree pointer to by USE.  */ 
static inline tree
get_use_from_ptr (use_operand_p use)
{ 
  return *(use->use);
} 

/* Return the tree pointer to by DEF.  */
static inline tree
get_def_from_ptr (def_operand_p def)
{
  return *def;
}

/* Return a def_operand_p pointer for the result of PHI.  */
static inline def_operand_p
get_phi_result_ptr (tree phi)
{
  return &(PHI_RESULT_TREE (phi));
}

/* Return a use_operand_p pointer for argument I of phinode PHI.  */
static inline use_operand_p
get_phi_arg_def_ptr (tree phi, int i)
{
  return &(PHI_ARG_IMM_USE_NODE (phi,i));
}


/* Return the bitmap of addresses taken by STMT, or NULL if it takes
   no addresses.  */
static inline bitmap
addresses_taken (tree stmt)
{
  stmt_ann_t ann = stmt_ann (stmt);
  return ann ? ann->addresses_taken : NULL;
}

/* Return the PHI nodes for basic block BB, or NULL if there are no
   PHI nodes.  */
static inline tree
phi_nodes (basic_block bb)
{
  return bb->phi_nodes;
}

/* Set list of phi nodes of a basic block BB to L.  */

static inline void
set_phi_nodes (basic_block bb, tree l)
{
  tree phi;

  bb->phi_nodes = l;
  for (phi = l; phi; phi = PHI_CHAIN (phi))
    set_bb_for_stmt (phi, bb);
}

/* Return the phi argument which contains the specified use.  */

static inline int
phi_arg_index_from_use (use_operand_p use)
{
  struct phi_arg_d *element, *root;
  int index;
  tree phi;

  /* Since the use is the first thing in a PHI argument element, we can
     calculate its index based on casting it to an argument, and performing
     pointer arithmetic.  */

  phi = USE_STMT (use);
  gcc_assert (TREE_CODE (phi) == PHI_NODE);

  element = (struct phi_arg_d *)use;
  root = &(PHI_ARG_ELT (phi, 0));
  index = element - root;

#ifdef ENABLE_CHECKING
  /* Make sure the calculation doesn't have any leftover bytes.  If it does, 
     then imm_use is likely not the first element in phi_arg_d.  */
  gcc_assert (
	  (((char *)element - (char *)root) % sizeof (struct phi_arg_d)) == 0);
  gcc_assert (index >= 0 && index < PHI_ARG_CAPACITY (phi));
#endif
 
 return index;
}

/* Mark VAR as used, so that it'll be preserved during rtl expansion.  */

static inline void
set_is_used (tree var)
{
  var_ann_t ann = get_var_ann (var);
  ann->used = 1;
}


/*  -----------------------------------------------------------------------  */

/* Return true if T is an executable statement.  */
static inline bool
is_exec_stmt (tree t)
{
  return (t && !IS_EMPTY_STMT (t) && t != error_mark_node);
}


/* Return true if this stmt can be the target of a control transfer stmt such
   as a goto.  */
static inline bool
is_label_stmt (tree t)
{
  if (t)
    switch (TREE_CODE (t))
      {
	case LABEL_DECL:
	case LABEL_EXPR:
	case CASE_LABEL_EXPR:
	  return true;
	default:
	  return false;
      }
  return false;
}

/* PHI nodes should contain only ssa_names and invariants.  A test
   for ssa_name is definitely simpler; don't let invalid contents
   slip in in the meantime.  */

static inline bool
phi_ssa_name_p (tree t)
{
  if (TREE_CODE (t) == SSA_NAME)
    return true;
#ifdef ENABLE_CHECKING
  gcc_assert (is_gimple_min_invariant (t));
#endif
  return false;
}

/*  -----------------------------------------------------------------------  */

/* Return a block_stmt_iterator that points to beginning of basic
   block BB.  */
static inline block_stmt_iterator
bsi_start (basic_block bb)
{
  block_stmt_iterator bsi;
  if (bb->stmt_list)
    bsi.tsi = tsi_start (bb->stmt_list);
  else
    {
      gcc_assert (bb->index < NUM_FIXED_BLOCKS);
      bsi.tsi.ptr = NULL;
      bsi.tsi.container = NULL;
    }
  bsi.bb = bb;
  return bsi;
}

/* Return a block statement iterator that points to the first non-label
   statement in block BB.  */

static inline block_stmt_iterator
bsi_after_labels (basic_block bb)
{
  block_stmt_iterator bsi = bsi_start (bb);

  while (!bsi_end_p (bsi) && TREE_CODE (bsi_stmt (bsi)) == LABEL_EXPR)
    bsi_next (&bsi);

  return bsi;
}

/* Return a block statement iterator that points to the end of basic
   block BB.  */
static inline block_stmt_iterator
bsi_last (basic_block bb)
{
  block_stmt_iterator bsi;
  if (bb->stmt_list)
    bsi.tsi = tsi_last (bb->stmt_list);
  else
    {
      gcc_assert (bb->index < NUM_FIXED_BLOCKS);
      bsi.tsi.ptr = NULL;
      bsi.tsi.container = NULL;
    }
  bsi.bb = bb;
  return bsi;
}

/* Return true if block statement iterator I has reached the end of
   the basic block.  */
static inline bool
bsi_end_p (block_stmt_iterator i)
{
  return tsi_end_p (i.tsi);
}

/* Modify block statement iterator I so that it is at the next
   statement in the basic block.  */
static inline void
bsi_next (block_stmt_iterator *i)
{
  tsi_next (&i->tsi);
}

/* Modify block statement iterator I so that it is at the previous
   statement in the basic block.  */
static inline void
bsi_prev (block_stmt_iterator *i)
{
  tsi_prev (&i->tsi);
}

/* Return the statement that block statement iterator I is currently
   at.  */
static inline tree
bsi_stmt (block_stmt_iterator i)
{
  return tsi_stmt (i.tsi);
}

/* Return a pointer to the statement that block statement iterator I
   is currently at.  */
static inline tree *
bsi_stmt_ptr (block_stmt_iterator i)
{
  return tsi_stmt_ptr (i.tsi);
}

/* Returns the loop of the statement STMT.  */

static inline struct loop *
loop_containing_stmt (tree stmt)
{
  basic_block bb = bb_for_stmt (stmt);
  if (!bb)
    return NULL;

  return bb->loop_father;
}

/* Return true if VAR is a clobbered by function calls.  */
static inline bool
is_call_clobbered (tree var)
{
  if (!MTAG_P (var))
    return DECL_CALL_CLOBBERED (var);
  else
    return bitmap_bit_p (call_clobbered_vars, DECL_UID (var)); 
}

/* Mark variable VAR as being clobbered by function calls.  */
static inline void
mark_call_clobbered (tree var, unsigned int escape_type)
{
  var_ann (var)->escape_mask |= escape_type;
  if (!MTAG_P (var))
    DECL_CALL_CLOBBERED (var) = true;
  bitmap_set_bit (call_clobbered_vars, DECL_UID (var));
}

/* Clear the call-clobbered attribute from variable VAR.  */
static inline void
clear_call_clobbered (tree var)
{
  var_ann_t ann = var_ann (var);
  ann->escape_mask = 0;
  if (MTAG_P (var) && TREE_CODE (var) != STRUCT_FIELD_TAG)
    MTAG_GLOBAL (var) = 0;
  if (!MTAG_P (var))
    DECL_CALL_CLOBBERED (var) = false;
  bitmap_clear_bit (call_clobbered_vars, DECL_UID (var));
}

/* Mark variable VAR as being non-addressable.  */
static inline void
mark_non_addressable (tree var)
{
  if (!MTAG_P (var))
    DECL_CALL_CLOBBERED (var) = false;
  bitmap_clear_bit (call_clobbered_vars, DECL_UID (var));
  TREE_ADDRESSABLE (var) = 0;
}

/* Return the common annotation for T.  Return NULL if the annotation
   doesn't already exist.  */
static inline tree_ann_common_t
tree_common_ann (tree t)
{
  return &t->common.ann->common;
}

/* Return a common annotation for T.  Create the constant annotation if it
   doesn't exist.  */
static inline tree_ann_common_t
get_tree_common_ann (tree t)
{
  tree_ann_common_t ann = tree_common_ann (t);
  return (ann) ? ann : create_tree_common_ann (t);
}

/*  -----------------------------------------------------------------------  */

/* The following set of routines are used to iterator over various type of
   SSA operands.  */

/* Return true if PTR is finished iterating.  */
static inline bool
op_iter_done (ssa_op_iter *ptr)
{
  return ptr->done;
}

/* Get the next iterator use value for PTR.  */
static inline use_operand_p
op_iter_next_use (ssa_op_iter *ptr)
{
  use_operand_p use_p;
#ifdef ENABLE_CHECKING
  gcc_assert (ptr->iter_type == ssa_op_iter_use);
#endif
  if (ptr->uses)
    {
      use_p = USE_OP_PTR (ptr->uses);
      ptr->uses = ptr->uses->next;
      return use_p;
    }
  if (ptr->vuses)
    {
      use_p = VUSE_OP_PTR (ptr->vuses);
      ptr->vuses = ptr->vuses->next;
      return use_p;
    }
  if (ptr->mayuses)
    {
      use_p = MAYDEF_OP_PTR (ptr->mayuses);
      ptr->mayuses = ptr->mayuses->next;
      return use_p;
    }
  if (ptr->mustkills)
    {
      use_p = MUSTDEF_KILL_PTR (ptr->mustkills);
      ptr->mustkills = ptr->mustkills->next;
      return use_p;
    }
  if (ptr->phi_i < ptr->num_phi)
    {
      return PHI_ARG_DEF_PTR (ptr->phi_stmt, (ptr->phi_i)++);
    }
  ptr->done = true;
  return NULL_USE_OPERAND_P;
}

/* Get the next iterator def value for PTR.  */
static inline def_operand_p
op_iter_next_def (ssa_op_iter *ptr)
{
  def_operand_p def_p;
#ifdef ENABLE_CHECKING
  gcc_assert (ptr->iter_type == ssa_op_iter_def);
#endif
  if (ptr->defs)
    {
      def_p = DEF_OP_PTR (ptr->defs);
      ptr->defs = ptr->defs->next;
      return def_p;
    }
  if (ptr->mustdefs)
    {
      def_p = MUSTDEF_RESULT_PTR (ptr->mustdefs);
      ptr->mustdefs = ptr->mustdefs->next;
      return def_p;
    }
  if (ptr->maydefs)
    {
      def_p = MAYDEF_RESULT_PTR (ptr->maydefs);
      ptr->maydefs = ptr->maydefs->next;
      return def_p;
    }
  ptr->done = true;
  return NULL_DEF_OPERAND_P;
}

/* Get the next iterator tree value for PTR.  */
static inline tree
op_iter_next_tree (ssa_op_iter *ptr)
{
  tree val;
#ifdef ENABLE_CHECKING
  gcc_assert (ptr->iter_type == ssa_op_iter_tree);
#endif
  if (ptr->uses)
    {
      val = USE_OP (ptr->uses);
      ptr->uses = ptr->uses->next;
      return val;
    }
  if (ptr->vuses)
    {
      val = VUSE_OP (ptr->vuses);
      ptr->vuses = ptr->vuses->next;
      return val;
    }
  if (ptr->mayuses)
    {
      val = MAYDEF_OP (ptr->mayuses);
      ptr->mayuses = ptr->mayuses->next;
      return val;
    }
  if (ptr->mustkills)
    {
      val = MUSTDEF_KILL (ptr->mustkills);
      ptr->mustkills = ptr->mustkills->next;
      return val;
    }
  if (ptr->defs)
    {
      val = DEF_OP (ptr->defs);
      ptr->defs = ptr->defs->next;
      return val;
    }
  if (ptr->mustdefs)
    {
      val = MUSTDEF_RESULT (ptr->mustdefs);
      ptr->mustdefs = ptr->mustdefs->next;
      return val;
    }
  if (ptr->maydefs)
    {
      val = MAYDEF_RESULT (ptr->maydefs);
      ptr->maydefs = ptr->maydefs->next;
      return val;
    }

  ptr->done = true;
  return NULL_TREE;

}


/* This functions clears the iterator PTR, and marks it done.  This is normally
   used to prevent warnings in the compile about might be uninitialized
   components.  */

static inline void
clear_and_done_ssa_iter (ssa_op_iter *ptr)
{
  ptr->defs = NULL;
  ptr->uses = NULL;
  ptr->vuses = NULL;
  ptr->maydefs = NULL;
  ptr->mayuses = NULL;
  ptr->mustdefs = NULL;
  ptr->mustkills = NULL;
  ptr->iter_type = ssa_op_iter_none;
  ptr->phi_i = 0;
  ptr->num_phi = 0;
  ptr->phi_stmt = NULL_TREE;
  ptr->done = true;
}

/* Initialize the iterator PTR to the virtual defs in STMT.  */
static inline void
op_iter_init (ssa_op_iter *ptr, tree stmt, int flags)
{
#ifdef ENABLE_CHECKING
  gcc_assert (stmt_ann (stmt));
#endif

  ptr->defs = (flags & SSA_OP_DEF) ? DEF_OPS (stmt) : NULL;
  ptr->uses = (flags & SSA_OP_USE) ? USE_OPS (stmt) : NULL;
  ptr->vuses = (flags & SSA_OP_VUSE) ? VUSE_OPS (stmt) : NULL;
  ptr->maydefs = (flags & SSA_OP_VMAYDEF) ? MAYDEF_OPS (stmt) : NULL;
  ptr->mayuses = (flags & SSA_OP_VMAYUSE) ? MAYDEF_OPS (stmt) : NULL;
  ptr->mustdefs = (flags & SSA_OP_VMUSTDEF) ? MUSTDEF_OPS (stmt) : NULL;
  ptr->mustkills = (flags & SSA_OP_VMUSTKILL) ? MUSTDEF_OPS (stmt) : NULL;
  ptr->done = false;

  ptr->phi_i = 0;
  ptr->num_phi = 0;
  ptr->phi_stmt = NULL_TREE;
}

/* Initialize iterator PTR to the use operands in STMT based on FLAGS. Return
   the first use.  */
static inline use_operand_p
op_iter_init_use (ssa_op_iter *ptr, tree stmt, int flags)
{
  gcc_assert ((flags & SSA_OP_ALL_DEFS) == 0);
  op_iter_init (ptr, stmt, flags);
  ptr->iter_type = ssa_op_iter_use;
  return op_iter_next_use (ptr);
}

/* Initialize iterator PTR to the def operands in STMT based on FLAGS. Return
   the first def.  */
static inline def_operand_p
op_iter_init_def (ssa_op_iter *ptr, tree stmt, int flags)
{
  gcc_assert ((flags & (SSA_OP_ALL_USES | SSA_OP_VIRTUAL_KILLS)) == 0);
  op_iter_init (ptr, stmt, flags);
  ptr->iter_type = ssa_op_iter_def;
  return op_iter_next_def (ptr);
}

/* Initialize iterator PTR to the operands in STMT based on FLAGS. Return
   the first operand as a tree.  */
static inline tree
op_iter_init_tree (ssa_op_iter *ptr, tree stmt, int flags)
{
  op_iter_init (ptr, stmt, flags);
  ptr->iter_type = ssa_op_iter_tree;
  return op_iter_next_tree (ptr);
}

/* Get the next iterator mustdef value for PTR, returning the mustdef values in
   KILL and DEF.  */
static inline void
op_iter_next_maymustdef (use_operand_p *use, def_operand_p *def, 
			 ssa_op_iter *ptr)
{
#ifdef ENABLE_CHECKING
  gcc_assert (ptr->iter_type == ssa_op_iter_maymustdef);
#endif
  if (ptr->mayuses)
    {
      *def = MAYDEF_RESULT_PTR (ptr->mayuses);
      *use = MAYDEF_OP_PTR (ptr->mayuses);
      ptr->mayuses = ptr->mayuses->next;
      return;
    }

  if (ptr->mustkills)
    {
      *def = MUSTDEF_RESULT_PTR (ptr->mustkills);
      *use = MUSTDEF_KILL_PTR (ptr->mustkills);
      ptr->mustkills = ptr->mustkills->next;
      return;
    }

  *def = NULL_DEF_OPERAND_P;
  *use = NULL_USE_OPERAND_P;
  ptr->done = true;
  return;
}


/* Initialize iterator PTR to the operands in STMT.  Return the first operands
   in USE and DEF.  */
static inline void
op_iter_init_maydef (ssa_op_iter *ptr, tree stmt, use_operand_p *use, 
		     def_operand_p *def)
{
  gcc_assert (TREE_CODE (stmt) != PHI_NODE);

  op_iter_init (ptr, stmt, SSA_OP_VMAYUSE);
  ptr->iter_type = ssa_op_iter_maymustdef;
  op_iter_next_maymustdef (use, def, ptr);
}


/* Initialize iterator PTR to the operands in STMT.  Return the first operands
   in KILL and DEF.  */
static inline void
op_iter_init_mustdef (ssa_op_iter *ptr, tree stmt, use_operand_p *kill, 
		     def_operand_p *def)
{
  gcc_assert (TREE_CODE (stmt) != PHI_NODE);

  op_iter_init (ptr, stmt, SSA_OP_VMUSTKILL);
  ptr->iter_type = ssa_op_iter_maymustdef;
  op_iter_next_maymustdef (kill, def, ptr);
}

/* Initialize iterator PTR to the operands in STMT.  Return the first operands
   in KILL and DEF.  */
static inline void
op_iter_init_must_and_may_def (ssa_op_iter *ptr, tree stmt,
			       use_operand_p *kill, def_operand_p *def)
{
  gcc_assert (TREE_CODE (stmt) != PHI_NODE);

  op_iter_init (ptr, stmt, SSA_OP_VMUSTKILL|SSA_OP_VMAYUSE);
  ptr->iter_type = ssa_op_iter_maymustdef;
  op_iter_next_maymustdef (kill, def, ptr);
}


/* If there is a single operand in STMT matching FLAGS, return it.  Otherwise
   return NULL.  */
static inline tree
single_ssa_tree_operand (tree stmt, int flags)
{
  tree var;
  ssa_op_iter iter;

  var = op_iter_init_tree (&iter, stmt, flags);
  if (op_iter_done (&iter))
    return NULL_TREE;
  op_iter_next_tree (&iter);
  if (op_iter_done (&iter))
    return var;
  return NULL_TREE;
}


/* If there is a single operand in STMT matching FLAGS, return it.  Otherwise
   return NULL.  */
static inline use_operand_p
single_ssa_use_operand (tree stmt, int flags)
{
  use_operand_p var;
  ssa_op_iter iter;

  var = op_iter_init_use (&iter, stmt, flags);
  if (op_iter_done (&iter))
    return NULL_USE_OPERAND_P;
  op_iter_next_use (&iter);
  if (op_iter_done (&iter))
    return var;
  return NULL_USE_OPERAND_P;
}



/* If there is a single operand in STMT matching FLAGS, return it.  Otherwise
   return NULL.  */
static inline def_operand_p
single_ssa_def_operand (tree stmt, int flags)
{
  def_operand_p var;
  ssa_op_iter iter;

  var = op_iter_init_def (&iter, stmt, flags);
  if (op_iter_done (&iter))
    return NULL_DEF_OPERAND_P;
  op_iter_next_def (&iter);
  if (op_iter_done (&iter))
    return var;
  return NULL_DEF_OPERAND_P;
}


/* Return true if there are zero operands in STMT matching the type 
   given in FLAGS.  */
static inline bool
zero_ssa_operands (tree stmt, int flags)
{
  ssa_op_iter iter;

  op_iter_init_tree (&iter, stmt, flags);
  return op_iter_done (&iter);
}


/* Return the number of operands matching FLAGS in STMT.  */
static inline int
num_ssa_operands (tree stmt, int flags)
{
  ssa_op_iter iter;
  tree t;
  int num = 0;

  FOR_EACH_SSA_TREE_OPERAND (t, stmt, iter, flags)
    num++;
  return num;
}


/* Delink all immediate_use information for STMT.  */
static inline void
delink_stmt_imm_use (tree stmt)
{
   ssa_op_iter iter;
   use_operand_p use_p;

   if (ssa_operands_active ())
     FOR_EACH_SSA_USE_OPERAND (use_p, stmt, iter,
			       (SSA_OP_ALL_USES | SSA_OP_ALL_KILLS))
       delink_imm_use (use_p);
}


/* This routine will compare all the operands matching FLAGS in STMT1 to those
   in STMT2.  TRUE is returned if they are the same.  STMTs can be NULL.  */
static inline bool
compare_ssa_operands_equal (tree stmt1, tree stmt2, int flags)
{
  ssa_op_iter iter1, iter2;
  tree op1 = NULL_TREE;
  tree op2 = NULL_TREE;
  bool look1, look2;

  if (stmt1 == stmt2)
    return true;

  look1 = stmt1 && stmt_ann (stmt1);
  look2 = stmt2 && stmt_ann (stmt2);

  if (look1)
    {
      op1 = op_iter_init_tree (&iter1, stmt1, flags);
      if (!look2)
        return op_iter_done (&iter1);
    }
  else
    clear_and_done_ssa_iter (&iter1);

  if (look2)
    {
      op2 = op_iter_init_tree (&iter2, stmt2, flags);
      if (!look1)
        return op_iter_done (&iter2);
    }
  else
    clear_and_done_ssa_iter (&iter2);

  while (!op_iter_done (&iter1) && !op_iter_done (&iter2))
    {
      if (op1 != op2)
	return false;
      op1 = op_iter_next_tree (&iter1);
      op2 = op_iter_next_tree (&iter2);
    }

  return (op_iter_done (&iter1) && op_iter_done (&iter2));
}


/* If there is a single DEF in the PHI node which matches FLAG, return it.
   Otherwise return NULL_DEF_OPERAND_P.  */
static inline tree
single_phi_def (tree stmt, int flags)
{
  tree def = PHI_RESULT (stmt);
  if ((flags & SSA_OP_DEF) && is_gimple_reg (def)) 
    return def;
  if ((flags & SSA_OP_VIRTUAL_DEFS) && !is_gimple_reg (def))
    return def;
  return NULL_TREE;
}

/* Initialize the iterator PTR for uses matching FLAGS in PHI.  FLAGS should
   be either SSA_OP_USES or SSA_OP_VIRTUAL_USES.  */
static inline use_operand_p
op_iter_init_phiuse (ssa_op_iter *ptr, tree phi, int flags)
{
  tree phi_def = PHI_RESULT (phi);
  int comp;

  clear_and_done_ssa_iter (ptr);
  ptr->done = false;

  gcc_assert ((flags & (SSA_OP_USE | SSA_OP_VIRTUAL_USES)) != 0);

  comp = (is_gimple_reg (phi_def) ? SSA_OP_USE : SSA_OP_VIRTUAL_USES);
    
  /* If the PHI node doesn't the operand type we care about, we're done.  */
  if ((flags & comp) == 0)
    {
      ptr->done = true;
      return NULL_USE_OPERAND_P;
    }

  ptr->phi_stmt = phi;
  ptr->num_phi = PHI_NUM_ARGS (phi);
  ptr->iter_type = ssa_op_iter_use;
  return op_iter_next_use (ptr);
}


/* Start an iterator for a PHI definition.  */

static inline def_operand_p
op_iter_init_phidef (ssa_op_iter *ptr, tree phi, int flags)
{
  tree phi_def = PHI_RESULT (phi);
  int comp;

  clear_and_done_ssa_iter (ptr);
  ptr->done = false;

  gcc_assert ((flags & (SSA_OP_DEF | SSA_OP_VIRTUAL_DEFS)) != 0);

  comp = (is_gimple_reg (phi_def) ? SSA_OP_DEF : SSA_OP_VIRTUAL_DEFS);
    
  /* If the PHI node doesn't the operand type we care about, we're done.  */
  if ((flags & comp) == 0)
    {
      ptr->done = true;
      return NULL_USE_OPERAND_P;
    }

  ptr->iter_type = ssa_op_iter_def;
  /* The first call to op_iter_next_def will terminate the iterator since
     all the fields are NULL.  Simply return the result here as the first and
     therefore only result.  */
  return PHI_RESULT_PTR (phi);
}

/* Return true is IMM has reached the end of the immediate use stmt list.  */

static inline bool
end_imm_use_stmt_p (imm_use_iterator *imm)
{
  return (imm->imm_use == imm->end_p);
}

/* Finished the traverse of an immediate use stmt list IMM by removing the
   placeholder node from the list.  */

static inline void
end_imm_use_stmt_traverse (imm_use_iterator *imm)
{
  delink_imm_use (&(imm->iter_node));
}

/* Immediate use traversal of uses within a stmt require that all the
   uses on a stmt be sequentially listed.  This routine is used to build up
   this sequential list by adding USE_P to the end of the current list 
   currently delimited by HEAD and LAST_P.  The new LAST_P value is 
   returned.  */

static inline use_operand_p
move_use_after_head (use_operand_p use_p, use_operand_p head, 
		      use_operand_p last_p)
{
  gcc_assert (USE_FROM_PTR (use_p) == USE_FROM_PTR (head));
  /* Skip head when we find it.  */
  if (use_p != head)
    {
      /* If use_p is already linked in after last_p, continue.  */
      if (last_p->next == use_p)
	last_p = use_p;
      else
	{
	  /* Delink from current location, and link in at last_p.  */
	  delink_imm_use (use_p);
	  link_imm_use_to_list (use_p, last_p);
	  last_p = use_p;
	}
    }
  return last_p;
}


/* This routine will relink all uses with the same stmt as HEAD into the list
   immediately following HEAD for iterator IMM.  */

static inline void
link_use_stmts_after (use_operand_p head, imm_use_iterator *imm)
{
  use_operand_p use_p;
  use_operand_p last_p = head;
  tree head_stmt = USE_STMT (head);
  tree use = USE_FROM_PTR (head);
  ssa_op_iter op_iter;
  int flag;

  /* Only look at virtual or real uses, depending on the type of HEAD.  */
  flag = (is_gimple_reg (use) ? SSA_OP_USE : SSA_OP_VIRTUAL_USES);

  if (TREE_CODE (head_stmt) == PHI_NODE)
    {
      FOR_EACH_PHI_ARG (use_p, head_stmt, op_iter, flag)
	if (USE_FROM_PTR (use_p) == use)
	  last_p = move_use_after_head (use_p, head, last_p);
    }
  else
    {
      FOR_EACH_SSA_USE_OPERAND (use_p, head_stmt, op_iter, flag)
	if (USE_FROM_PTR (use_p) == use)
	  last_p = move_use_after_head (use_p, head, last_p);
    }
  /* LInk iter node in after last_p.  */
  if (imm->iter_node.prev != NULL)
    delink_imm_use (&imm->iter_node);
  link_imm_use_to_list (&(imm->iter_node), last_p);
}

/* Initialize IMM to traverse over uses of VAR.  Return the first statement.  */
static inline tree
first_imm_use_stmt (imm_use_iterator *imm, tree var)
{
  gcc_assert (TREE_CODE (var) == SSA_NAME);
  
  imm->end_p = &(SSA_NAME_IMM_USE_NODE (var));
  imm->imm_use = imm->end_p->next;
  imm->next_imm_name = NULL_USE_OPERAND_P;

  /* iter_node is used as a marker within the immediate use list to indicate
     where the end of the current stmt's uses are.  Initialize it to NULL
     stmt and use, which indicates a marker node.  */
  imm->iter_node.prev = NULL_USE_OPERAND_P;
  imm->iter_node.next = NULL_USE_OPERAND_P;
  imm->iter_node.stmt = NULL_TREE;
  imm->iter_node.use = NULL_USE_OPERAND_P;

  if (end_imm_use_stmt_p (imm))
    return NULL_TREE;

  link_use_stmts_after (imm->imm_use, imm);

  return USE_STMT (imm->imm_use);
}

/* Bump IMM to the next stmt which has a use of var.  */

static inline tree
next_imm_use_stmt (imm_use_iterator *imm)
{
  imm->imm_use = imm->iter_node.next;
  if (end_imm_use_stmt_p (imm))
    {
      if (imm->iter_node.prev != NULL)
	delink_imm_use (&imm->iter_node);
      return NULL_TREE;
    }

  link_use_stmts_after (imm->imm_use, imm);
  return USE_STMT (imm->imm_use);

}

/* This routine will return the first use on the stmt IMM currently refers
   to.  */

static inline use_operand_p
first_imm_use_on_stmt (imm_use_iterator *imm)
{
  imm->next_imm_name = imm->imm_use->next;
  return imm->imm_use;
}

/*  Return TRUE if the last use on the stmt IMM refers to has been visited.  */

static inline bool
end_imm_use_on_stmt_p (imm_use_iterator *imm)
{
  return (imm->imm_use == &(imm->iter_node));
}

/* Bump to the next use on the stmt IMM refers to, return NULL if done.  */

static inline use_operand_p
next_imm_use_on_stmt (imm_use_iterator *imm)
{
  imm->imm_use = imm->next_imm_name;
  if (end_imm_use_on_stmt_p (imm))
    return NULL_USE_OPERAND_P;
  else
    {
      imm->next_imm_name = imm->imm_use->next;
      return imm->imm_use;
    }
}

/* Return true if VAR cannot be modified by the program.  */

static inline bool
unmodifiable_var_p (tree var)
{
  if (TREE_CODE (var) == SSA_NAME)
    var = SSA_NAME_VAR (var);

  if (MTAG_P (var))
    return TREE_READONLY (var) && (TREE_STATIC (var) || MTAG_GLOBAL (var));

  return TREE_READONLY (var) && (TREE_STATIC (var) || DECL_EXTERNAL (var));
}

/* Return true if REF, an ARRAY_REF, has an INDIRECT_REF somewhere in it.  */

static inline bool
array_ref_contains_indirect_ref (tree ref)
{
  gcc_assert (TREE_CODE (ref) == ARRAY_REF);

  do {
    ref = TREE_OPERAND (ref, 0);
  } while (handled_component_p (ref));

  return TREE_CODE (ref) == INDIRECT_REF;
}

/* Return true if REF, a handled component reference, has an ARRAY_REF
   somewhere in it.  */

static inline bool
ref_contains_array_ref (tree ref)
{
  gcc_assert (handled_component_p (ref));

  do {
    if (TREE_CODE (ref) == ARRAY_REF)
      return true;
    ref = TREE_OPERAND (ref, 0);
  } while (handled_component_p (ref));

  return false;
}

/* Given a variable VAR, lookup and return a pointer to the list of
   subvariables for it.  */

static inline subvar_t *
lookup_subvars_for_var (tree var)
{
  var_ann_t ann = var_ann (var);
  gcc_assert (ann);
  return &ann->subvars;
}

/* Given a variable VAR, return a linked list of subvariables for VAR, or
   NULL, if there are no subvariables.  */

static inline subvar_t
get_subvars_for_var (tree var)
{
  subvar_t subvars;

  gcc_assert (SSA_VAR_P (var));  
  
  if (TREE_CODE (var) == SSA_NAME)
    subvars = *(lookup_subvars_for_var (SSA_NAME_VAR (var)));
  else
    subvars = *(lookup_subvars_for_var (var));
  return subvars;
}

/* Return the subvariable of VAR at offset OFFSET.  */

static inline tree
get_subvar_at (tree var, unsigned HOST_WIDE_INT offset)
{
  subvar_t sv;

  for (sv = get_subvars_for_var (var); sv; sv = sv->next)
    if (SFT_OFFSET (sv->var) == offset)
      return sv->var;

  return NULL_TREE;
}

/* Return true if V is a tree that we can have subvars for.
   Normally, this is any aggregate type.  Also complex
   types which are not gimple registers can have subvars.  */

static inline bool
var_can_have_subvars (tree v)
{
  /* Volatile variables should never have subvars.  */
  if (TREE_THIS_VOLATILE (v))
    return false;

  /* Non decls or memory tags can never have subvars.  */
  if (!DECL_P (v) || MTAG_P (v))
    return false;

  /* Aggregates can have subvars.  */
  if (AGGREGATE_TYPE_P (TREE_TYPE (v)))
    return true;

  /* Complex types variables which are not also a gimple register can
    have subvars. */
  if (TREE_CODE (TREE_TYPE (v)) == COMPLEX_TYPE
      && !DECL_COMPLEX_GIMPLE_REG_P (v))
    return true;

  return false;
}

  
/* Return true if OFFSET and SIZE define a range that overlaps with some
   portion of the range of SV, a subvar.  If there was an exact overlap,
   *EXACT will be set to true upon return. */

static inline bool
overlap_subvar (unsigned HOST_WIDE_INT offset, unsigned HOST_WIDE_INT size,
		tree sv,  bool *exact)
{
  /* There are three possible cases of overlap.
     1. We can have an exact overlap, like so:   
     |offset, offset + size             |
     |sv->offset, sv->offset + sv->size |
     
     2. We can have offset starting after sv->offset, like so:
     
           |offset, offset + size              |
     |sv->offset, sv->offset + sv->size  |

     3. We can have offset starting before sv->offset, like so:
     
     |offset, offset + size    |
       |sv->offset, sv->offset + sv->size|
  */

  if (exact)
    *exact = false;
  if (offset == SFT_OFFSET (sv) && size == SFT_SIZE (sv))
    {
      if (exact)
	*exact = true;
      return true;
    }
  else if (offset >= SFT_OFFSET (sv) 
	   && offset < (SFT_OFFSET (sv) + SFT_SIZE (sv)))
    {
      return true;
    }
  else if (offset < SFT_OFFSET (sv) 
	   && (size > SFT_OFFSET (sv) - offset))
    {
      return true;
    }
  return false;

}

#endif /* _TREE_FLOW_INLINE_H  */
