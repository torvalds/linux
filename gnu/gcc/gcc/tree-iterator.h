/* Iterator routines for manipulating GENERIC and GIMPLE tree statements.
   Copyright (C) 2003, 2004 Free Software Foundation, Inc.
   Contributed by Andrew MacLeod  <amacleod@redhat.com>

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


/* This file is dependent upon the implementation of tree's. It provides an
   abstract interface to the tree objects such that if all tree creation and
   manipulations are done through this interface, we can easily change the
   implementation of tree's, and not impact other code.  */

#ifndef GCC_TREE_ITERATOR_H
#define GCC_TREE_ITERATOR_H 1

/* Iterator object for GENERIC or GIMPLE TREE statements.  */

typedef struct {
  struct tree_statement_list_node *ptr;
  tree container;
} tree_stmt_iterator;

static inline tree_stmt_iterator
tsi_start (tree t)
{
  tree_stmt_iterator i;

  i.ptr = STATEMENT_LIST_HEAD (t);
  i.container = t;

  return i;
}

static inline tree_stmt_iterator
tsi_last (tree t)
{
  tree_stmt_iterator i;

  i.ptr = STATEMENT_LIST_TAIL (t);
  i.container = t;

  return i;
}

static inline bool
tsi_end_p (tree_stmt_iterator i)
{
  return i.ptr == NULL;
}

static inline bool
tsi_one_before_end_p (tree_stmt_iterator i)
{
  return i.ptr != NULL && i.ptr->next == NULL;
}

static inline void
tsi_next (tree_stmt_iterator *i)
{
  i->ptr = i->ptr->next;
}

static inline void
tsi_prev (tree_stmt_iterator *i)
{
  i->ptr = i->ptr->prev;
}

static inline tree *
tsi_stmt_ptr (tree_stmt_iterator i)
{
  return &i.ptr->stmt;
}

static inline tree
tsi_stmt (tree_stmt_iterator i)
{
  return i.ptr->stmt;
}

enum tsi_iterator_update
{
  TSI_NEW_STMT,		/* Only valid when single statement is added, move
			   iterator to it.  */
  TSI_SAME_STMT,	/* Leave the iterator at the same statement.  */
  TSI_CHAIN_START,	/* Only valid when chain of statements is added, move
			   iterator to the first statement in the chain.  */
  TSI_CHAIN_END,	/* Only valid when chain of statements is added, move
			   iterator to the last statement in the chain.  */
  TSI_CONTINUE_LINKING	/* Move iterator to whatever position is suitable for
			   linking other statements/chains of statements in
			   the same direction.  */
};

extern void tsi_link_before (tree_stmt_iterator *, tree,
			     enum tsi_iterator_update);
extern void tsi_link_after (tree_stmt_iterator *, tree,
			    enum tsi_iterator_update);

void tsi_delink (tree_stmt_iterator *);

tree tsi_split_statement_list_after (const tree_stmt_iterator *);
tree tsi_split_statement_list_before (tree_stmt_iterator *);

void append_to_statement_list (tree, tree *);
void append_to_statement_list_force (tree, tree *);

#endif /* GCC_TREE_ITERATOR_H  */
