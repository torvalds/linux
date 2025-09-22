/* Tree inlining hooks and declarations.
   Copyright 2001, 2003, 2004, 2005 Free Software Foundation, Inc.
   Contributed by Alexandre Oliva  <aoliva@redhat.com>

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

#ifndef GCC_TREE_INLINE_H
#define GCC_TREE_INLINE_H

#include "varray.h"
#include "splay-tree.h"


/* Data required for function body duplication.  */

typedef struct copy_body_data
{
  /* FUNCTION_DECL for function being inlined, or in general the
     source function providing the original trees.  */
  tree src_fn;
  /* FUNCTION_DECL for function being inlined into, or in general
     the destination function receiving the new trees.  */
  tree dst_fn;
  /* Callgraph node of the source function.  */
  struct cgraph_node *src_node;
  /* Callgraph node of the destination function.  */
  struct cgraph_node *dst_node;
  /* struct function for function being inlined.  Usually this is the same
     as DECL_STRUCT_FUNCTION (src_fn), but can be different if saved_cfg
     and saved_eh are in use.  */
  struct function *src_cfun;

  /* The VAR_DECL for the return value.  */
  tree retvar;
  /* The map from local declarations in the inlined function to
     equivalents in the function into which it is being inlined.  */
  splay_tree decl_map;

  /* Create a new decl to replace DECL in the destination function.  */
  tree (*copy_decl) (tree, struct copy_body_data *);

  /* Current BLOCK.  */
  tree block;

  /* Exception region the inlined call lie in.  */
  int eh_region;
  /* Take region number in the function being copied, add this value and
     get eh region number of the duplicate in the function we inline into.  */
  int eh_region_offset;

  /* We use the same mechanism do all sorts of different things.  Rather
     than enumerating the different cases, we categorize the behavior
     in the various situations.  */

  /* Indicate the desired behavior wrt call graph edges.  We can either
     duplicate the edge (inlining, cloning), move the edge (versioning,
     parallelization), or move the edges of the clones (saving).  */
  enum copy_body_cge_which {
    CB_CGE_DUPLICATE,
    CB_CGE_MOVE,
    CB_CGE_MOVE_CLONES
  } transform_call_graph_edges;

  /* True if a new CFG should be created.  False for inlining, true for
     everything else.  */
  bool transform_new_cfg;

  /* True if RETURN_EXPRs should be transformed to just the contained
     MODIFY_EXPR.  The branch semantics of the return will be handled
     by manipulating the CFG rather than a statement.  */
  bool transform_return_to_modify;

  /* True if lang_hooks.decls.insert_block should be invoked when
     duplicating BLOCK nodes.  */
  bool transform_lang_insert_block;
} copy_body_data;

/* Function prototypes.  */

extern tree copy_body_r (tree *, int *, void *);
extern void insert_decl_map (copy_body_data *, tree, tree);

void optimize_inline_calls (tree);
bool tree_inlinable_function_p (tree);
tree copy_tree_r (tree *, int *, void *);
void clone_body (tree, tree, void *);
void save_body (tree, tree *, tree *);
int estimate_move_cost (tree type);
void push_cfun (struct function *new_cfun);
void pop_cfun (void);
int estimate_num_insns (tree expr);
bool tree_versionable_function_p (tree);
void tree_function_versioning (tree, tree, varray_type, bool);

extern tree remap_decl (tree decl, copy_body_data *id);
extern tree remap_type (tree type, copy_body_data *id);

/* 0 if we should not perform inlining.
   1 if we should expand functions calls inline at the tree level.
   2 if we should consider *all* functions to be inline
   candidates.  */

extern int flag_inline_trees;

#endif /* GCC_TREE_INLINE_H */
