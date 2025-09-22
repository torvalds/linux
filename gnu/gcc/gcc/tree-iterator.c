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

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tree.h"
#include "tree-gimple.h"
#include "tree-iterator.h"
#include "ggc.h"


/* This is a cache of STATEMENT_LIST nodes.  We create and destroy them
   fairly often during gimplification.  */

static GTY ((deletable (""))) tree stmt_list_cache;

tree
alloc_stmt_list (void)
{
  tree list = stmt_list_cache;
  if (list)
    {
      stmt_list_cache = TREE_CHAIN (list);
      gcc_assert (stmt_list_cache != list);
      memset (list, 0, sizeof(struct tree_common));
      TREE_SET_CODE (list, STATEMENT_LIST);
    }
  else
    list = make_node (STATEMENT_LIST);
  TREE_TYPE (list) = void_type_node;
  return list;
}

void
free_stmt_list (tree t)
{
  gcc_assert (!STATEMENT_LIST_HEAD (t));
  gcc_assert (!STATEMENT_LIST_TAIL (t));
  /* If this triggers, it's a sign that the same list is being freed
     twice.  */
  gcc_assert (t != stmt_list_cache || stmt_list_cache == NULL);
  TREE_CHAIN (t) = stmt_list_cache;
  stmt_list_cache = t;
}

/* Links a statement, or a chain of statements, before the current stmt.  */

void
tsi_link_before (tree_stmt_iterator *i, tree t, enum tsi_iterator_update mode)
{
  struct tree_statement_list_node *head, *tail, *cur;

  /* Die on looping.  */
  gcc_assert (t != i->container);

  if (TREE_CODE (t) == STATEMENT_LIST)
    {
      head = STATEMENT_LIST_HEAD (t);
      tail = STATEMENT_LIST_TAIL (t);
      STATEMENT_LIST_HEAD (t) = NULL;
      STATEMENT_LIST_TAIL (t) = NULL;

      free_stmt_list (t);

      /* Empty statement lists need no work.  */
      if (!head || !tail)
	{
	  gcc_assert (head == tail);
	  return;
	}
    }
  else
    {
      head = ggc_alloc (sizeof (*head));
      head->prev = NULL;
      head->next = NULL;
      head->stmt = t;
      tail = head;
    }

  TREE_SIDE_EFFECTS (i->container) = 1;

  cur = i->ptr;

  /* Link it into the list.  */
  if (cur)
    {
      head->prev = cur->prev;
      if (head->prev)
	head->prev->next = head;
      else
	STATEMENT_LIST_HEAD (i->container) = head;
      tail->next = cur;
      cur->prev = tail;
    }
  else
    {
      head->prev = STATEMENT_LIST_TAIL (i->container);
      if (head->prev)
       head->prev->next = head;
      else
       STATEMENT_LIST_HEAD (i->container) = head;
      STATEMENT_LIST_TAIL (i->container) = tail;
    }

  /* Update the iterator, if requested.  */
  switch (mode)
    {
    case TSI_NEW_STMT:
    case TSI_CONTINUE_LINKING:
    case TSI_CHAIN_START:
      i->ptr = head;
      break;
    case TSI_CHAIN_END:
      i->ptr = tail;
      break;
    case TSI_SAME_STMT:
      break;
    }
}

/* Links a statement, or a chain of statements, after the current stmt.  */

void
tsi_link_after (tree_stmt_iterator *i, tree t, enum tsi_iterator_update mode)
{
  struct tree_statement_list_node *head, *tail, *cur;

  /* Die on looping.  */
  gcc_assert (t != i->container);

  if (TREE_CODE (t) == STATEMENT_LIST)
    {
      head = STATEMENT_LIST_HEAD (t);
      tail = STATEMENT_LIST_TAIL (t);
      STATEMENT_LIST_HEAD (t) = NULL;
      STATEMENT_LIST_TAIL (t) = NULL;

      free_stmt_list (t);

      /* Empty statement lists need no work.  */
      if (!head || !tail)
	{
	  gcc_assert (head == tail);
	  return;
	}
    }
  else
    {
      head = ggc_alloc (sizeof (*head));
      head->prev = NULL;
      head->next = NULL;
      head->stmt = t;
      tail = head;
    }

  TREE_SIDE_EFFECTS (i->container) = 1;

  cur = i->ptr;

  /* Link it into the list.  */
  if (cur)
    {
      tail->next = cur->next;
      if (tail->next)
	tail->next->prev = tail;
      else
	STATEMENT_LIST_TAIL (i->container) = tail;
      head->prev = cur;
      cur->next = head;
    }
  else
    {
      gcc_assert (!STATEMENT_LIST_TAIL (i->container));
      STATEMENT_LIST_HEAD (i->container) = head;
      STATEMENT_LIST_TAIL (i->container) = tail;
    }

  /* Update the iterator, if requested.  */
  switch (mode)
    {
    case TSI_NEW_STMT:
    case TSI_CHAIN_START:
      i->ptr = head;
      break;
    case TSI_CONTINUE_LINKING:
    case TSI_CHAIN_END:
      i->ptr = tail;
      break;
    case TSI_SAME_STMT:
      gcc_assert (cur);
      break;
    }
}

/* Remove a stmt from the tree list.  The iterator is updated to point to
   the next stmt.  */

void
tsi_delink (tree_stmt_iterator *i)
{
  struct tree_statement_list_node *cur, *next, *prev;

  cur = i->ptr;
  next = cur->next;
  prev = cur->prev;

  if (prev)
    prev->next = next;
  else
    STATEMENT_LIST_HEAD (i->container) = next;
  if (next)
    next->prev = prev;
  else
    STATEMENT_LIST_TAIL (i->container) = prev;

  if (!next && !prev)
    TREE_SIDE_EFFECTS (i->container) = 0;

  i->ptr = next;
}

/* Move all statements in the statement list after I to a new
   statement list.  I itself is unchanged.  */

tree
tsi_split_statement_list_after (const tree_stmt_iterator *i)
{
  struct tree_statement_list_node *cur, *next;
  tree old_sl, new_sl;

  cur = i->ptr;
  /* How can we possibly split after the end, or before the beginning?  */
  gcc_assert (cur);
  next = cur->next;

  old_sl = i->container;
  new_sl = alloc_stmt_list ();
  TREE_SIDE_EFFECTS (new_sl) = 1;

  STATEMENT_LIST_HEAD (new_sl) = next;
  STATEMENT_LIST_TAIL (new_sl) = STATEMENT_LIST_TAIL (old_sl);
  STATEMENT_LIST_TAIL (old_sl) = cur;
  cur->next = NULL;
  next->prev = NULL;

  return new_sl;
}

/* Move all statements in the statement list before I to a new
   statement list.  I is set to the head of the new list.  */

tree
tsi_split_statement_list_before (tree_stmt_iterator *i)
{
  struct tree_statement_list_node *cur, *prev;
  tree old_sl, new_sl;

  cur = i->ptr;
  /* How can we possibly split after the end, or before the beginning?  */
  gcc_assert (cur);
  prev = cur->prev;

  old_sl = i->container;
  new_sl = alloc_stmt_list ();
  TREE_SIDE_EFFECTS (new_sl) = 1;
  i->container = new_sl;

  STATEMENT_LIST_HEAD (new_sl) = cur;
  STATEMENT_LIST_TAIL (new_sl) = STATEMENT_LIST_TAIL (old_sl);
  STATEMENT_LIST_TAIL (old_sl) = prev;
  cur->prev = NULL;
  if (prev)
    prev->next = NULL;

  return new_sl;
}

/* Return the first expression in a sequence of COMPOUND_EXPRs,
   or in a STATEMENT_LIST.  */

tree
expr_first (tree expr)
{
  if (expr == NULL_TREE)
    return expr;

  if (TREE_CODE (expr) == STATEMENT_LIST)
    {
      struct tree_statement_list_node *n = STATEMENT_LIST_HEAD (expr);
      return n ? n->stmt : NULL_TREE;
    }

  while (TREE_CODE (expr) == COMPOUND_EXPR)
    expr = TREE_OPERAND (expr, 0);
  return expr;
}

/* Return the last expression in a sequence of COMPOUND_EXPRs,
   or in a STATEMENT_LIST.  */

tree
expr_last (tree expr)
{
  if (expr == NULL_TREE)
    return expr;

  if (TREE_CODE (expr) == STATEMENT_LIST)
    {
      struct tree_statement_list_node *n = STATEMENT_LIST_TAIL (expr);
      return n ? n->stmt : NULL_TREE;
    }

  while (TREE_CODE (expr) == COMPOUND_EXPR)
    expr = TREE_OPERAND (expr, 1);
  return expr;
}

/* If EXPR is a single statement return it.  If EXPR is a
   STATEMENT_LIST containing exactly one statement S, return S.
   Otherwise, return NULL.  */

tree 
expr_only (tree expr)
{
  if (expr == NULL_TREE)
    return NULL_TREE;

  if (TREE_CODE (expr) == STATEMENT_LIST)
    {
      struct tree_statement_list_node *n = STATEMENT_LIST_TAIL (expr);
      if (n && STATEMENT_LIST_HEAD (expr) == n)
	return n->stmt;
      else
	return NULL_TREE;
    }

  if (TREE_CODE (expr) == COMPOUND_EXPR)
    return NULL_TREE;

  return expr;
}

#include "gt-tree-iterator.h"
