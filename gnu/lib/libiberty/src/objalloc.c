/* objalloc.c -- routines to allocate memory for objects
   Copyright 1997-2012 Free Software Foundation, Inc.
   Written by Ian Lance Taylor, Cygnus Solutions.

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
Foundation, 51 Franklin Street - Fifth Floor,
Boston, MA 02110-1301, USA.  */

#include "config.h"
#include "ansidecl.h"

#include "objalloc.h"

/* Get a definition for NULL.  */
#include <stdio.h>

#if VMS
#include <stdlib.h>
#include <unixlib.h>
#else

/* Get a definition for size_t.  */
#include <stddef.h>

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#else
/* For systems with larger pointers than ints, this must be declared.  */
extern PTR malloc (size_t);
extern void free (PTR);
#endif

#endif

/* These routines allocate space for an object.  Freeing allocated
   space may or may not free all more recently allocated space.

   We handle large and small allocation requests differently.  If we
   don't have enough space in the current block, and the allocation
   request is for more than 512 bytes, we simply pass it through to
   malloc.  */

/* The objalloc structure is defined in objalloc.h.  */

/* This structure appears at the start of each chunk.  */

struct objalloc_chunk
{
  /* Next chunk.  */
  struct objalloc_chunk *next;
  /* If this chunk contains large objects, this is the value of
     current_ptr when this chunk was allocated.  If this chunk
     contains small objects, this is NULL.  */
  char *current_ptr;
};

/* The aligned size of objalloc_chunk.  */

#define CHUNK_HEADER_SIZE					\
  ((sizeof (struct objalloc_chunk) + OBJALLOC_ALIGN - 1)	\
   &~ (OBJALLOC_ALIGN - 1))

/* We ask for this much memory each time we create a chunk which is to
   hold small objects.  */

#define CHUNK_SIZE (4096 - 32)

/* A request for this amount or more is just passed through to malloc.  */

#define BIG_REQUEST (512)

/* Create an objalloc structure.  */

struct objalloc *
objalloc_create (void)
{
  struct objalloc *ret;
  struct objalloc_chunk *chunk;

  ret = (struct objalloc *) malloc (sizeof *ret);
  if (ret == NULL)
    return NULL;

  ret->chunks = (PTR) malloc (CHUNK_SIZE);
  if (ret->chunks == NULL)
    {
      free (ret);
      return NULL;
    }

  chunk = (struct objalloc_chunk *) ret->chunks;
  chunk->next = NULL;
  chunk->current_ptr = NULL;

  ret->current_ptr = (char *) chunk + CHUNK_HEADER_SIZE;
  ret->current_space = CHUNK_SIZE - CHUNK_HEADER_SIZE;

  return ret;
}

/* Allocate space from an objalloc structure.  */

PTR
_objalloc_alloc (struct objalloc *o, unsigned long original_len)
{
  unsigned long len = original_len;

  /* We avoid confusion from zero sized objects by always allocating
     at least 1 byte.  */
  if (len == 0)
    len = 1;

  len = (len + OBJALLOC_ALIGN - 1) &~ (OBJALLOC_ALIGN - 1);

  /* CVE-2012-3509: Check for overflow in the alignment operation above
   * and then malloc argument below. */
  if (len + CHUNK_HEADER_SIZE < original_len)
    return NULL;

  if (len <= o->current_space)
    {
      o->current_ptr += len;
      o->current_space -= len;
      return (PTR) (o->current_ptr - len);
    }

  if (len >= BIG_REQUEST)
    {
      char *ret;
      struct objalloc_chunk *chunk;

      ret = (char *) malloc (CHUNK_HEADER_SIZE + len);
      if (ret == NULL)
	return NULL;

      chunk = (struct objalloc_chunk *) ret;
      chunk->next = (struct objalloc_chunk *) o->chunks;
      chunk->current_ptr = o->current_ptr;

      o->chunks = (PTR) chunk;

      return (PTR) (ret + CHUNK_HEADER_SIZE);
    }
  else
    {
      struct objalloc_chunk *chunk;

      chunk = (struct objalloc_chunk *) malloc (CHUNK_SIZE);
      if (chunk == NULL)
	return NULL;
      chunk->next = (struct objalloc_chunk *) o->chunks;
      chunk->current_ptr = NULL;

      o->current_ptr = (char *) chunk + CHUNK_HEADER_SIZE;
      o->current_space = CHUNK_SIZE - CHUNK_HEADER_SIZE;

      o->chunks = (PTR) chunk;

      return objalloc_alloc (o, len);
    }
}

/* Free an entire objalloc structure.  */

void
objalloc_free (struct objalloc *o)
{
  struct objalloc_chunk *l;

  l = (struct objalloc_chunk *) o->chunks;
  while (l != NULL)
    {
      struct objalloc_chunk *next;

      next = l->next;
      free (l);
      l = next;
    }

  free (o);
}

/* Free a block from an objalloc structure.  This also frees all more
   recently allocated blocks.  */

void
objalloc_free_block (struct objalloc *o, PTR block)
{
  struct objalloc_chunk *p, *small;
  char *b = (char *) block;

  /* First set P to the chunk which contains the block we are freeing,
     and set Q to the last small object chunk we see before P.  */
  small = NULL;
  for (p = (struct objalloc_chunk *) o->chunks; p != NULL; p = p->next)
    {
      if (p->current_ptr == NULL)
	{
	  if (b > (char *) p && b < (char *) p + CHUNK_SIZE)
	    break;
	  small = p;
	}
      else
	{
	  if (b == (char *) p + CHUNK_HEADER_SIZE)
	    break;
	}
    }

  /* If we can't find the chunk, the caller has made a mistake.  */
  if (p == NULL)
    abort ();

  if (p->current_ptr == NULL)
    {
      struct objalloc_chunk *q;
      struct objalloc_chunk *first;

      /* The block is in a chunk containing small objects.  We can
	 free every chunk through SMALL, because they have certainly
	 been allocated more recently.  After SMALL, we will not see
	 any chunks containing small objects; we can free any big
	 chunk if the current_ptr is greater than or equal to B.  We
	 can then reset the new current_ptr to B.  */

      first = NULL;
      q = (struct objalloc_chunk *) o->chunks;
      while (q != p)
	{
	  struct objalloc_chunk *next;

	  next = q->next;
	  if (small != NULL)
	    {
	      if (small == q)
		small = NULL;
	      free (q);
	    }
	  else if (q->current_ptr > b)
	    free (q);
	  else if (first == NULL)
	    first = q;

	  q = next;
	}

      if (first == NULL)
	first = p;
      o->chunks = (PTR) first;

      /* Now start allocating from this small block again.  */
      o->current_ptr = b;
      o->current_space = ((char *) p + CHUNK_SIZE) - b;
    }
  else
    {
      struct objalloc_chunk *q;
      char *current_ptr;

      /* This block is in a large chunk by itself.  We can free
         everything on the list up to and including this block.  We
         then start allocating from the next chunk containing small
         objects, setting current_ptr from the value stored with the
         large chunk we are freeing.  */

      current_ptr = p->current_ptr;
      p = p->next;

      q = (struct objalloc_chunk *) o->chunks;
      while (q != p)
	{
	  struct objalloc_chunk *next;

	  next = q->next;
	  free (q);
	  q = next;
	}

      o->chunks = (PTR) p;

      while (p->current_ptr != NULL)
	p = p->next;

      o->current_ptr = current_ptr;
      o->current_space = ((char *) p + CHUNK_SIZE) - current_ptr;
    }
}
