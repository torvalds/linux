/* List implementation of a partition of consecutive integers.
   Copyright (C) 2000, 2001 Free Software Foundation, Inc.
   Contributed by CodeSourcery, LLC.

   This file is part of GNU CC.

   GNU CC is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GNU CC is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GNU CC; see the file COPYING.  If not, write to
   the Free Software Foundation, 51 Franklin Street - Fifth Floor,
   Boston, MA 02110-1301, USA.  */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include "libiberty.h"
#include "partition.h"

static int elem_compare (const void *, const void *);

/* Creates a partition of NUM_ELEMENTS elements.  Initially each
   element is in a class by itself.  */

partition
partition_new (int num_elements)
{
  int e;
  
  partition part = (partition) 
    xmalloc (sizeof (struct partition_def) + 
	     (num_elements - 1) * sizeof (struct partition_elem));
  part->num_elements = num_elements;
  for (e = 0; e < num_elements; ++e) 
    {
      part->elements[e].class_element = e;
      part->elements[e].next = &(part->elements[e]);
      part->elements[e].class_count = 1;
    }

  return part;
}

/* Freeds a partition.  */

void
partition_delete (partition part)
{
  free (part);
}

/* Unites the classes containing ELEM1 and ELEM2 into a single class
   of partition PART.  If ELEM1 and ELEM2 are already in the same
   class, does nothing.  Returns the canonical element of the
   resulting union class.  */

int
partition_union (partition part, int elem1, int elem2)
{
  struct partition_elem *elements = part->elements;
  struct partition_elem *e1;
  struct partition_elem *e2;
  struct partition_elem *p;
  struct partition_elem *old_next;
  /* The canonical element of the resulting union class.  */
  int class_element = elements[elem1].class_element;

  /* If they're already in the same class, do nothing.  */
  if (class_element == elements[elem2].class_element)
    return class_element;

  /* Make sure ELEM1 is in the larger class of the two.  If not, swap
     them.  This way we always scan the shorter list.  */
  if (elements[elem1].class_count < elements[elem2].class_count) 
    {
      int temp = elem1;
      elem1 = elem2;
      elem2 = temp;
      class_element = elements[elem1].class_element;
    }

  e1 = &(elements[elem1]);
  e2 = &(elements[elem2]);

  /* Keep a count of the number of elements in the list.  */
  elements[class_element].class_count 
    += elements[e2->class_element].class_count;

  /* Update the class fields in elem2's class list.  */
  e2->class_element = class_element;
  for (p = e2->next; p != e2; p = p->next)
    p->class_element = class_element;
  
  /* Splice ELEM2's class list into ELEM1's.  These are circular
     lists.  */
  old_next = e1->next;
  e1->next = e2->next;
  e2->next = old_next;

  return class_element;
}

/* Compare elements ELEM1 and ELEM2 from array of integers, given a
   pointer to each.  Used to qsort such an array.  */

static int 
elem_compare (const void *elem1, const void *elem2)
{
  int e1 = * (const int *) elem1;
  int e2 = * (const int *) elem2;
  if (e1 < e2)
    return -1;
  else if (e1 > e2)
    return 1;
  else
    return 0;
}

/* Prints PART to the file pointer FP.  The elements of each
   class are sorted.  */

void
partition_print (partition part, FILE *fp)
{
  char *done;
  int num_elements = part->num_elements;
  struct partition_elem *elements = part->elements;
  int *class_elements;
  int e;

  /* Flag the elements we've already printed.  */
  done = (char *) xmalloc (num_elements);
  memset (done, 0, num_elements);

  /* A buffer used to sort elements in a class.  */
  class_elements = (int *) xmalloc (num_elements * sizeof (int));

  fputc ('[', fp);
  for (e = 0; e < num_elements; ++e)
    /* If we haven't printed this element, print its entire class.  */
    if (! done[e]) 
      {
	int c = e;
	int count = elements[elements[e].class_element].class_count;
	int i;

      /* Collect the elements in this class.  */
	for (i = 0; i < count; ++i) {
	  class_elements[i] = c;
	  done[c] = 1;
	  c = elements[c].next - elements;
	}
	/* Sort them.  */
	qsort ((void *) class_elements, count, sizeof (int), elem_compare);
	/* Print them.  */
	fputc ('(', fp);
	for (i = 0; i < count; ++i) 
	  fprintf (fp, i == 0 ? "%d" : " %d", class_elements[i]);
	fputc (')', fp);
      }
  fputc (']', fp);

  free (class_elements);
  free (done);
}

