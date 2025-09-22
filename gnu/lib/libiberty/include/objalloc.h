/* objalloc.h -- routines to allocate memory for objects
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

#ifndef OBJALLOC_H
#define OBJALLOC_H

#include "ansidecl.h"

/* These routines allocate space for an object.  The assumption is
   that the object will want to allocate space as it goes along, but
   will never want to free any particular block.  There is a function
   to free a block, which also frees all more recently allocated
   blocks.  There is also a function to free all the allocated space.

   This is essentially a specialization of obstacks.  The main
   difference is that a block may not be allocated a bit at a time.
   Another difference is that these routines are always built on top
   of malloc, and always pass an malloc failure back to the caller,
   unlike more recent versions of obstacks.  */

/* This is what an objalloc structure looks like.  Callers should not
   refer to these fields, nor should they allocate these structure
   themselves.  Instead, they should only create them via
   objalloc_init, and only access them via the functions and macros
   listed below.  The structure is only defined here so that we can
   access it via macros.  */

struct objalloc
{
  char *current_ptr;
  unsigned int current_space;
  void *chunks;
};

/* Work out the required alignment.  */

struct objalloc_align { char x; double d; };

#if defined (__STDC__) && __STDC__
#ifndef offsetof
#include <stddef.h>
#endif
#endif
#ifndef offsetof
#define offsetof(TYPE, MEMBER) ((unsigned long) &((TYPE *)0)->MEMBER)
#endif
#define OBJALLOC_ALIGN offsetof (struct objalloc_align, d)

/* Create an objalloc structure.  Returns NULL if malloc fails.  */

extern struct objalloc *objalloc_create (void);

/* Allocate space from an objalloc structure.  Returns NULL if malloc
   fails.  */

extern void *_objalloc_alloc (struct objalloc *, unsigned long);

/* The macro version of objalloc_alloc.  We only define this if using
   gcc, because otherwise we would have to evaluate the arguments
   multiple times, or use a temporary field as obstack.h does.  */

#if defined (__GNUC__) && defined (__STDC__) && __STDC__

/* NextStep 2.0 cc is really gcc 1.93 but it defines __GNUC__ = 2 and
   does not implement __extension__.  But that compiler doesn't define
   __GNUC_MINOR__.  */
#if __GNUC__ < 2 || (__NeXT__ && !__GNUC_MINOR__)
#define __extension__
#endif

#define objalloc_alloc(o, l)						\
  __extension__								\
  ({ struct objalloc *__o = (o);					\
     unsigned long __len = (l);						\
     if (__len == 0)							\
       __len = 1;							\
     __len = (__len + OBJALLOC_ALIGN - 1) &~ (OBJALLOC_ALIGN - 1);	\
     (__len != 0 && __len <= __o->current_space			\
      ? (__o->current_ptr += __len,					\
	 __o->current_space -= __len,					\
	 (void *) (__o->current_ptr - __len))				\
      : _objalloc_alloc (__o, __len)); })

#else /* ! __GNUC__ */

#define objalloc_alloc(o, l) _objalloc_alloc ((o), (l))

#endif /* ! __GNUC__ */

/* Free an entire objalloc structure.  */

extern void objalloc_free (struct objalloc *);

/* Free a block allocated by objalloc_alloc.  This also frees all more
   recently allocated blocks.  */

extern void objalloc_free_block (struct objalloc *, void *);

#endif /* OBJALLOC_H */
