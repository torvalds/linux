/* Et-forest data structure implementation.
   Copyright (C) 2002, 2003, 2004, 2005 Free Software Foundation, Inc.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.  */

/* This package implements ET forest data structure. Each tree in
   the structure maintains a tree structure and offers logarithmic time
   for tree operations (insertion and removal of nodes and edges) and
   poly-logarithmic time for nearest common ancestor.

   ET tree stores its structure as a sequence of symbols obtained
   by dfs(root)

   dfs (node)
   {
     s = node;
     for each child c of node do
       s = concat (s, c, node);
     return s;
   }

   For example for tree

            1
          / | \
         2  3  4
       / |
      4  5

   the sequence is 1 2 4 2 5 3 1 3 1 4 1.

   The sequence is stored in a slightly modified splay tree.
   In order to support various types of node values, a hashtable
   is used to convert node values to the internal representation.  */

#ifndef _ET_TREE_H
#define _ET_TREE_H

#include <ansidecl.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* The node representing the node in an et tree.  */
struct et_node
{
  void *data;			/* The data represented by the node.  */

  int dfs_num_in, dfs_num_out;	/* Number of the node in the dfs ordering.  */

  struct et_node *father;	/* Father of the node.  */
  struct et_node *son;		/* The first of the sons of the node.  */
  struct et_node *left;
  struct et_node *right;	/* The brothers of the node.  */

  struct et_occ *rightmost_occ;	/* The rightmost occurrence.  */
  struct et_occ *parent_occ;	/* The occurrence of the parent node.  */
};

struct et_node *et_new_tree (void *data);
void et_free_tree (struct et_node *);
void et_free_tree_force (struct et_node *);
void et_free_pools (void);
void et_set_father (struct et_node *, struct et_node *);
void et_split (struct et_node *);
struct et_node *et_nca (struct et_node *, struct et_node *);
bool et_below (struct et_node *, struct et_node *);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _ET_TREE_H */
