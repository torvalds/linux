/* Vector API for GNU compiler.
   Copyright (C) 2004, 2005, 2006 Free Software Foundation, Inc.
   Contributed by Nathan Sidwell <nathan@codesourcery.com>

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

/* This file is compiled twice: once for the generator programs
   once for the compiler.  */
#ifdef GENERATOR_FILE
#include "bconfig.h"
#else
#include "config.h"
#endif

#include "system.h"
#include "ggc.h"
#include "vec.h"
#include "coretypes.h"
#include "tree.h"
#include "toplev.h"

struct vec_prefix 
{
  unsigned num;
  unsigned alloc;
  void *vec[1];
};

/* Calculate the new ALLOC value, making sure that RESERVE slots are
   free.  If EXACT grow exactly, otherwise grow exponentially.  */

static inline unsigned
calculate_allocation (const struct vec_prefix *pfx, int reserve, bool exact)
{
  unsigned alloc = 0;
  unsigned num = 0;

  gcc_assert (reserve >= 0);

  if (pfx)
    {
      alloc = pfx->alloc;
      num = pfx->num;
    }
  else if (!reserve)
    /* If there's no prefix, and we've not requested anything, then we
       will create a NULL vector.  */
    return 0;
  
  /* We must have run out of room.  */
  gcc_assert (alloc - num < (unsigned) reserve);
  
  if (exact)
    /* Exact size.  */
    alloc = num + reserve;
  else
    {
      /* Exponential growth. */
      if (!alloc)
	alloc = 4;
      else if (alloc < 16)
	/* Double when small.  */
	alloc = alloc * 2;
      else
	/* Grow slower when large.  */
	alloc = (alloc * 3 / 2);
      
      /* If this is still too small, set it to the right size. */
      if (alloc < num + reserve)
	alloc = num + reserve;
    }
  return alloc;
}

/* Ensure there are at least RESERVE free slots in VEC.  If EXACT grow
   exactly, else grow exponentially.  As a special case, if VEC is
   NULL and RESERVE is 0, no vector will be created.  The vector's
   trailing array is at VEC_OFFSET offset and consists of ELT_SIZE
   sized elements.  */

static void *
vec_gc_o_reserve_1 (void *vec, int reserve, size_t vec_offset, size_t elt_size,
		    bool exact MEM_STAT_DECL)
{
  struct vec_prefix *pfx = vec;
  unsigned alloc = alloc = calculate_allocation (pfx, reserve, exact);
  
  if (!alloc)
    return NULL;
  
  vec = ggc_realloc_stat (vec, vec_offset + alloc * elt_size PASS_MEM_STAT);
  ((struct vec_prefix *)vec)->alloc = alloc;
  if (!pfx)
    ((struct vec_prefix *)vec)->num = 0;
  
  return vec;
}

/* Ensure there are at least RESERVE free slots in VEC, growing
   exponentially.  If RESERVE < 0 grow exactly, else grow
   exponentially.  As a special case, if VEC is NULL, and RESERVE is
   0, no vector will be created. */

void *
vec_gc_p_reserve (void *vec, int reserve MEM_STAT_DECL)
{
  return vec_gc_o_reserve_1 (vec, reserve,
			     offsetof (struct vec_prefix, vec),
			     sizeof (void *), false
			     PASS_MEM_STAT);
}

/* Ensure there are at least RESERVE free slots in VEC, growing
   exactly.  If RESERVE < 0 grow exactly, else grow exponentially.  As
   a special case, if VEC is NULL, and RESERVE is 0, no vector will be
   created. */

void *
vec_gc_p_reserve_exact (void *vec, int reserve MEM_STAT_DECL)
{
  return vec_gc_o_reserve_1 (vec, reserve,
			     offsetof (struct vec_prefix, vec),
			     sizeof (void *), true
			     PASS_MEM_STAT);
}

/* As for vec_gc_p_reserve, but for object vectors.  The vector's
   trailing array is at VEC_OFFSET offset and consists of ELT_SIZE
   sized elements.  */

void *
vec_gc_o_reserve (void *vec, int reserve, size_t vec_offset, size_t elt_size
		  MEM_STAT_DECL)
{
  return vec_gc_o_reserve_1 (vec, reserve, vec_offset, elt_size, false
			     PASS_MEM_STAT);
}

/* As for vec_gc_p_reserve_exact, but for object vectors.  The
   vector's trailing array is at VEC_OFFSET offset and consists of
   ELT_SIZE sized elements.  */

void *
vec_gc_o_reserve_exact (void *vec, int reserve, size_t vec_offset,
			size_t elt_size MEM_STAT_DECL)
{
  return vec_gc_o_reserve_1 (vec, reserve, vec_offset, elt_size, true
			     PASS_MEM_STAT);
}

/* As for vec_gc_o_reserve_1, but for heap allocated vectors.  */

static void *
vec_heap_o_reserve_1 (void *vec, int reserve, size_t vec_offset,
		      size_t elt_size, bool exact MEM_STAT_DECL)
{
  struct vec_prefix *pfx = vec;
  unsigned alloc = calculate_allocation (pfx, reserve, exact);

  if (!alloc)
    return NULL;
  
  vec = xrealloc (vec, vec_offset + alloc * elt_size);
  ((struct vec_prefix *)vec)->alloc = alloc;
  if (!pfx)
    ((struct vec_prefix *)vec)->num = 0;
  
  return vec;
}

/* As for vec_gc_p_reserve, but for heap allocated vectors.  */

void *
vec_heap_p_reserve (void *vec, int reserve MEM_STAT_DECL)
{
  return vec_heap_o_reserve_1 (vec, reserve,
			       offsetof (struct vec_prefix, vec),
			       sizeof (void *), false
			       PASS_MEM_STAT);
}

/* As for vec_gc_p_reserve_exact, but for heap allocated vectors.  */

void *
vec_heap_p_reserve_exact (void *vec, int reserve MEM_STAT_DECL)
{
  return vec_heap_o_reserve_1 (vec, reserve,
			       offsetof (struct vec_prefix, vec),
			       sizeof (void *), true
			       PASS_MEM_STAT);
}

/* As for vec_gc_o_reserve, but for heap allocated vectors.  */

void *
vec_heap_o_reserve (void *vec, int reserve, size_t vec_offset, size_t elt_size
		    MEM_STAT_DECL)
{
  return vec_heap_o_reserve_1 (vec, reserve, vec_offset, elt_size, false
			       PASS_MEM_STAT);
}

/* As for vec_gc_o_reserve_exact, but for heap allocated vectors.  */

void *
vec_heap_o_reserve_exact (void *vec, int reserve, size_t vec_offset,
			  size_t elt_size MEM_STAT_DECL)
{
  return vec_heap_o_reserve_1 (vec, reserve, vec_offset, elt_size, true
			       PASS_MEM_STAT);
}

#if ENABLE_CHECKING
/* Issue a vector domain error, and then fall over.  */

void
vec_assert_fail (const char *op, const char *struct_name,
		 const char *file, unsigned int line, const char *function)
{
  internal_error ("vector %s %s domain error, in %s at %s:%u",
		  struct_name, op, function, trim_filename (file), line);
}
#endif
