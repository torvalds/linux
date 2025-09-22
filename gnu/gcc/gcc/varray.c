/* Virtual array support.
   Copyright (C) 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2006
   Free Software Foundation, Inc.
   Contributed by Cygnus Solutions.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GCC is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
   License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING.  If not, write to the Free
   the Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston,
   MA 02110-1301, USA.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "toplev.h"
#include "varray.h"
#include "ggc.h"
#include "hashtab.h"

#define VARRAY_HDR_SIZE (sizeof (struct varray_head_tag) - sizeof (varray_data))

#ifdef GATHER_STATISTICS

/* Store information about each particular varray.  */
struct varray_descriptor
{
  const char *name;
  int allocated;
  int created;
  int resized;
  int copied;
};

/* Hashtable mapping varray names to descriptors.  */
static htab_t varray_hash;

/* Hashtable helpers.  */
static hashval_t
hash_descriptor (const void *p)
{
  const struct varray_descriptor *d = p;
  return htab_hash_pointer (d->name);
}
static int
eq_descriptor (const void *p1, const void *p2)
{
  const struct varray_descriptor *d = p1;
  return d->name == p2;
}

/* For given name, return descriptor, create new if needed.  */
static struct varray_descriptor *
varray_descriptor (const char *name)
{
  struct varray_descriptor **slot;

  if (!varray_hash)
    varray_hash = htab_create (10, hash_descriptor, eq_descriptor, NULL);

  slot = (struct varray_descriptor **)
    htab_find_slot_with_hash (varray_hash, name,
		    	      htab_hash_pointer (name),
			      1);
  if (*slot)
    return *slot;
  *slot = xcalloc (sizeof (**slot), 1);
  (*slot)->name = name;
  return *slot;
}
#endif

/* Do not add any more non-GC items here.  Please either remove or GC
   those items that are not GCed.  */

static const struct {
  unsigned char size;
  bool uses_ggc;
} element[NUM_VARRAY_DATA] = {
  { sizeof (char), 1 },
  { sizeof (unsigned char), 1 },
  { sizeof (short), 1 },
  { sizeof (unsigned short), 1 },
  { sizeof (int), 1 },
  { sizeof (unsigned int), 1 },
  { sizeof (long), 1 },
  { sizeof (unsigned long), 1 },
  { sizeof (HOST_WIDE_INT), 1 },
  { sizeof (unsigned HOST_WIDE_INT), 1 },
  { sizeof (void *), 1 },
  { sizeof (void *), 0 },
  { sizeof (char *), 1 },
  { sizeof (struct rtx_def *), 1 },
  { sizeof (struct rtvec_def *), 1 },
  { sizeof (union tree_node *), 1 },
  { sizeof (struct bitmap_head_def *), 1 },
  { sizeof (struct reg_info_def *), 0 },
  { sizeof (struct basic_block_def *), 1 },
  { sizeof (struct elt_list *), 1 },
  { sizeof (struct edge_def *), 1 },
  { sizeof (tree *), 1 },
};

/* Allocate a virtual array with NUM_ELEMENT elements, each of which is
   ELEMENT_SIZE bytes long, named NAME.  Array elements are zeroed.  */
varray_type
varray_init (size_t num_elements, enum varray_data_enum element_kind,
	     const char *name)
{
  size_t data_size = num_elements * element[element_kind].size;
  varray_type ptr;
#ifdef GATHER_STATISTICS
  struct varray_descriptor *desc = varray_descriptor (name);

  desc->created++;
  desc->allocated += data_size + VARRAY_HDR_SIZE;
#endif
  if (element[element_kind].uses_ggc)
    ptr = ggc_alloc_cleared (VARRAY_HDR_SIZE + data_size);
  else
    ptr = xcalloc (VARRAY_HDR_SIZE + data_size, 1);

  ptr->num_elements = num_elements;
  ptr->elements_used = 0;
  ptr->type = element_kind;
  ptr->name = name;
  return ptr;
}

/* Grow/shrink the virtual array VA to N elements.  Zero any new elements
   allocated.  */
varray_type
varray_grow (varray_type va, size_t n)
{
  size_t old_elements = va->num_elements;
  if (n != old_elements)
    {
      size_t elem_size = element[va->type].size;
      size_t old_data_size = old_elements * elem_size;
      size_t data_size = n * elem_size;
#ifdef GATHER_STATISTICS
      struct varray_descriptor *desc = varray_descriptor (va->name);
      varray_type oldva = va;

      if (data_size > old_data_size)
        desc->allocated += data_size - old_data_size;
      desc->resized ++;
#endif


      if (element[va->type].uses_ggc)
	va = ggc_realloc (va, VARRAY_HDR_SIZE + data_size);
      else
	va = xrealloc (va, VARRAY_HDR_SIZE + data_size);
      va->num_elements = n;
      if (n > old_elements)
	memset (&va->data.vdt_c[old_data_size], 0, data_size - old_data_size);
#ifdef GATHER_STATISTICS
      if (oldva != va)
        desc->copied++;
#endif
    }

  return va;
}

/* Reset a varray to its original state.  */
void
varray_clear (varray_type va)
{
  size_t data_size = element[va->type].size * va->num_elements;

  memset (va->data.vdt_c, 0, data_size);
  va->elements_used = 0;
}

/* Check the bounds of a varray access.  */

#if defined ENABLE_CHECKING && (GCC_VERSION >= 2007)

void
varray_check_failed (varray_type va, size_t n, const char *file, int line,
		     const char *function)
{
  internal_error ("virtual array %s[%lu]: element %lu out of bounds "
		  "in %s, at %s:%d",
		  va->name, (unsigned long) va->num_elements, (unsigned long) n,
		  function, trim_filename (file), line);
}

void
varray_underflow (varray_type va, const char *file, int line,
		  const char *function)
{
  internal_error ("underflowed virtual array %s in %s, at %s:%d",
		  va->name, function, trim_filename (file), line);
}

#endif


/* Output per-varray statistics.  */
#ifdef GATHER_STATISTICS

/* Used to accumulate statistics about varray sizes.  */
struct output_info
{
  int count;
  int size;
};

/* Called via htab_traverse.  Output varray descriptor pointed out by SLOT
   and update statistics.  */
static int
print_statistics (void **slot, void *b)
{
  struct varray_descriptor *d = (struct varray_descriptor *) *slot;
  struct output_info *i = (struct output_info *) b;

  if (d->allocated)
    {
      fprintf (stderr, "%-21s %6d %10d %7d %7d\n", d->name,
	       d->created, d->allocated, d->resized, d->copied);
      i->size += d->allocated;
      i->count += d->created;
    }
  return 1;
}
#endif

/* Output per-varray memory usage statistics.  */
void
dump_varray_statistics (void)
{
#ifdef GATHER_STATISTICS
  struct output_info info;

  fprintf (stderr, "\nVARRAY Kind            Count      Bytes  Resized copied\n");
  fprintf (stderr, "-------------------------------------------------------\n");
  info.count = 0;
  info.size = 0;
  htab_traverse (varray_hash, print_statistics, &info);
  fprintf (stderr, "-------------------------------------------------------\n");
  fprintf (stderr, "%-20s %7d %10d\n",
	   "Total", info.count, info.size);
  fprintf (stderr, "-------------------------------------------------------\n");
#endif
}
