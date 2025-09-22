/* ternary.c - Ternary Search Trees
   Copyright (C) 2001 Free Software Foundation, Inc.

   Contributed by Daniel Berlin (dan@cgsoftware.com)

   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 2, or (at your option) any
   later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301,
   USA.  */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#include <stdio.h>

#include "libiberty.h"
#include "ternary.h"

/* Non-recursive so we don't waste stack space/time on large
   insertions. */

PTR
ternary_insert (ternary_tree *root, const char *s, PTR data, int replace)
{
  int diff;
  ternary_tree curr, *pcurr;

  /* Start at the root. */
  pcurr = root;
  /* Loop until we find the right position */
  while ((curr = *pcurr))
    {
      /* Calculate the difference */
      diff = *s - curr->splitchar;
      /* Handle current char equal to node splitchar */
      if (diff == 0)
	{
	  /* Handle the case of a string we already have */
	  if (*s++ == 0)
	    {
	      if (replace)
		curr->eqkid = (ternary_tree) data;
	      return (PTR) curr->eqkid;
	    }
	  pcurr = &(curr->eqkid);
	}
      /* Handle current char less than node splitchar */
      else if (diff < 0)
	{
	  pcurr = &(curr->lokid);
	}
      /* Handle current char greater than node splitchar */
      else
	{
	  pcurr = &(curr->hikid);
	}
    }
  /* It's not a duplicate string, and we should insert what's left of
     the string, into the tree rooted at curr */
  for (;;)
    {
      /* Allocate the memory for the node, and fill it in */
      *pcurr = XNEW (ternary_node);
      curr = *pcurr;
      curr->splitchar = *s;
      curr->lokid = curr->hikid = curr->eqkid = 0;

      /* Place nodes until we hit the end of the string.
         When we hit it, place the data in the right place, and
         return.
       */
      if (*s++ == 0)
	{
	  curr->eqkid = (ternary_tree) data;
	  return data;
	}
      pcurr = &(curr->eqkid);
    }
}

/* Free the ternary search tree rooted at p. */
void
ternary_cleanup (ternary_tree p)
{
  if (p)
    {
      ternary_cleanup (p->lokid);
      if (p->splitchar)
	ternary_cleanup (p->eqkid);
      ternary_cleanup (p->hikid);
      free (p);
    }
}

/* Non-recursive find of a string in the ternary tree */
PTR
ternary_search (const ternary_node *p, const char *s)
{
  const ternary_node *curr;
  int diff, spchar;
  spchar = *s;
  curr = p;
  /* Loop while we haven't hit a NULL node or returned */
  while (curr)
    {
      /* Calculate the difference */
      diff = spchar - curr->splitchar;
      /* Handle the equal case */
      if (diff == 0)
	{
	  if (spchar == 0)
	    return (PTR) curr->eqkid;
	  spchar = *++s;
	  curr = curr->eqkid;
	}
      /* Handle the less than case */
      else if (diff < 0)
	curr = curr->lokid;
      /* All that's left is greater than */
      else
	curr = curr->hikid;
    }
  return NULL;
}

/* For those who care, the recursive version of the search. Useful if
   you want a starting point for pmsearch or nearsearch. */
static PTR
ternary_recursivesearch (const ternary_node *p, const char *s)
{
  if (!p)
    return 0;
  if (*s < p->splitchar)
    return ternary_recursivesearch (p->lokid, s);
  else if (*s > p->splitchar)
    return ternary_recursivesearch (p->hikid, s);
  else
    {
      if (*s == 0)
	return (PTR) p->eqkid;
      return ternary_recursivesearch (p->eqkid, ++s);
    }
}
