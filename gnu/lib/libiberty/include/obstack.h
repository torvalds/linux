/* obstack.h - object stack macros
   Copyright 1988, 1989, 1990, 1991, 1992, 1993, 1994, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004, 2005
   Free Software Foundation, Inc.


   NOTE: The canonical source of this file is maintained with the GNU C Library.
   Bugs can be reported to bug-glibc@gnu.org.

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

/* Summary:

All the apparent functions defined here are macros. The idea
is that you would use these pre-tested macros to solve a
very specific set of problems, and they would run fast.
Caution: no side-effects in arguments please!! They may be
evaluated MANY times!!

These macros operate a stack of objects.  Each object starts life
small, and may grow to maturity.  (Consider building a word syllable
by syllable.)  An object can move while it is growing.  Once it has
been "finished" it never changes address again.  So the "top of the
stack" is typically an immature growing object, while the rest of the
stack is of mature, fixed size and fixed address objects.

These routines grab large chunks of memory, using a function you
supply, called `obstack_chunk_alloc'.  On occasion, they free chunks,
by calling `obstack_chunk_free'.  You must define them and declare
them before using any obstack macros.

Each independent stack is represented by a `struct obstack'.
Each of the obstack macros expects a pointer to such a structure
as the first argument.

One motivation for this package is the problem of growing char strings
in symbol tables.  Unless you are "fascist pig with a read-only mind"
--Gosper's immortal quote from HAKMEM item 154, out of context--you
would not like to put any arbitrary upper limit on the length of your
symbols.

In practice this often means you will build many short symbols and a
few long symbols.  At the time you are reading a symbol you don't know
how long it is.  One traditional method is to read a symbol into a
buffer, realloc()ating the buffer every time you try to read a symbol
that is longer than the buffer.  This is beaut, but you still will
want to copy the symbol from the buffer to a more permanent
symbol-table entry say about half the time.

With obstacks, you can work differently.  Use one obstack for all symbol
names.  As you read a symbol, grow the name in the obstack gradually.
When the name is complete, finalize it.  Then, if the symbol exists already,
free the newly read name.

The way we do this is to take a large chunk, allocating memory from
low addresses.  When you want to build a symbol in the chunk you just
add chars above the current "high water mark" in the chunk.  When you
have finished adding chars, because you got to the end of the symbol,
you know how long the chars are, and you can create a new object.
Mostly the chars will not burst over the highest address of the chunk,
because you would typically expect a chunk to be (say) 100 times as
long as an average object.

In case that isn't clear, when we have enough chars to make up
the object, THEY ARE ALREADY CONTIGUOUS IN THE CHUNK (guaranteed)
so we just point to it where it lies.  No moving of chars is
needed and this is the second win: potentially long strings need
never be explicitly shuffled. Once an object is formed, it does not
change its address during its lifetime.

When the chars burst over a chunk boundary, we allocate a larger
chunk, and then copy the partly formed object from the end of the old
chunk to the beginning of the new larger chunk.  We then carry on
accreting characters to the end of the object as we normally would.

A special macro is provided to add a single char at a time to a
growing object.  This allows the use of register variables, which
break the ordinary 'growth' macro.

Summary:
	We allocate large chunks.
	We carve out one object at a time from the current chunk.
	Once carved, an object never moves.
	We are free to append data of any size to the currently
	  growing object.
	Exactly one object is growing in an obstack at any one time.
	You can run one obstack per control block.
	You may have as many control blocks as you dare.
	Because of the way we do it, you can `unwind' an obstack
	  back to a previous state. (You may remove objects much
	  as you would with a stack.)
*/


/* Don't do the contents of this file more than once.  */

#ifndef _OBSTACK_H
#define _OBSTACK_H 1

#ifdef __cplusplus
extern "C" {
#endif

/* We use subtraction of (char *) 0 instead of casting to int
   because on word-addressable machines a simple cast to int
   may ignore the byte-within-word field of the pointer.  */

#ifndef __PTR_TO_INT
# define __PTR_TO_INT(P) ((P) - (char *) 0)
#endif

#ifndef __INT_TO_PTR
# define __INT_TO_PTR(P) ((P) + (char *) 0)
#endif

/* We need the type of the resulting object.  If __PTRDIFF_TYPE__ is
   defined, as with GNU C, use that; that way we don't pollute the
   namespace with <stddef.h>'s symbols.  Otherwise, if <stddef.h> is
   available, include it and use ptrdiff_t.  In traditional C, long is
   the best that we can do.  */

#ifdef __PTRDIFF_TYPE__
# define PTR_INT_TYPE __PTRDIFF_TYPE__
#else
# ifdef HAVE_STDDEF_H
#  include <stddef.h>
#  define PTR_INT_TYPE ptrdiff_t
# else
#  define PTR_INT_TYPE long
# endif
#endif

#if defined _LIBC || defined HAVE_STRING_H
# include <string.h>
# define _obstack_memcpy(To, From, N) memcpy ((To), (From), (N))
#else
# ifdef memcpy
#  define _obstack_memcpy(To, From, N) memcpy ((To), (char *)(From), (N))
# else
#  define _obstack_memcpy(To, From, N) bcopy ((char *)(From), (To), (N))
# endif
#endif

struct _obstack_chunk		/* Lives at front of each chunk. */
{
  char  *limit;			/* 1 past end of this chunk */
  struct _obstack_chunk *prev;	/* address of prior chunk or NULL */
  char	contents[4];		/* objects begin here */
};

struct obstack		/* control current object in current chunk */
{
  long	chunk_size;		/* preferred size to allocate chunks in */
  struct _obstack_chunk *chunk;	/* address of current struct obstack_chunk */
  char	*object_base;		/* address of object we are building */
  char	*next_free;		/* where to add next char to current object */
  char	*chunk_limit;		/* address of char after current chunk */
  PTR_INT_TYPE temp;		/* Temporary for some macros.  */
  int   alignment_mask;		/* Mask of alignment for each object. */
  /* These prototypes vary based on `use_extra_arg', and we use
     casts to the prototypeless function type in all assignments,
     but having prototypes here quiets -Wstrict-prototypes.  */
  struct _obstack_chunk *(*chunkfun) (void *, long);
  void (*freefun) (void *, struct _obstack_chunk *);
  void *extra_arg;		/* first arg for chunk alloc/dealloc funcs */
  unsigned use_extra_arg:1;	/* chunk alloc/dealloc funcs take extra arg */
  unsigned maybe_empty_object:1;/* There is a possibility that the current
				   chunk contains a zero-length object.  This
				   prevents freeing the chunk if we allocate
				   a bigger chunk to replace it. */
  unsigned alloc_failed:1;	/* No longer used, as we now call the failed
				   handler on error, but retained for binary
				   compatibility.  */
};

/* Declare the external functions we use; they are in obstack.c.  */

extern void _obstack_newchunk (struct obstack *, int);
extern void _obstack_free (struct obstack *, void *);
extern int _obstack_begin (struct obstack *, int, int,
			    void *(*) (long), void (*) (void *));
extern int _obstack_begin_1 (struct obstack *, int, int,
			     void *(*) (void *, long),
			     void (*) (void *, void *), void *);
extern int _obstack_memory_used (struct obstack *);

/* Do the function-declarations after the structs
   but before defining the macros.  */

void obstack_init (struct obstack *obstack);

void * obstack_alloc (struct obstack *obstack, int size);

void * obstack_copy (struct obstack *obstack, void *address, int size);
void * obstack_copy0 (struct obstack *obstack, void *address, int size);

void obstack_free (struct obstack *obstack, void *block);

void obstack_blank (struct obstack *obstack, int size);

void obstack_grow (struct obstack *obstack, void *data, int size);
void obstack_grow0 (struct obstack *obstack, void *data, int size);

void obstack_1grow (struct obstack *obstack, int data_char);
void obstack_ptr_grow (struct obstack *obstack, void *data);
void obstack_int_grow (struct obstack *obstack, int data);

void * obstack_finish (struct obstack *obstack);

int obstack_object_size (struct obstack *obstack);

int obstack_room (struct obstack *obstack);
void obstack_make_room (struct obstack *obstack, int size);
void obstack_1grow_fast (struct obstack *obstack, int data_char);
void obstack_ptr_grow_fast (struct obstack *obstack, void *data);
void obstack_int_grow_fast (struct obstack *obstack, int data);
void obstack_blank_fast (struct obstack *obstack, int size);

void * obstack_base (struct obstack *obstack);
void * obstack_next_free (struct obstack *obstack);
int obstack_alignment_mask (struct obstack *obstack);
int obstack_chunk_size (struct obstack *obstack);
int obstack_memory_used (struct obstack *obstack);

/* Error handler called when `obstack_chunk_alloc' failed to allocate
   more memory.  This can be set to a user defined function.  The
   default action is to print a message and abort.  */
extern void (*obstack_alloc_failed_handler) (void);

/* Exit value used when `print_and_abort' is used.  */
extern int obstack_exit_failure;

/* Pointer to beginning of object being allocated or to be allocated next.
   Note that this might not be the final address of the object
   because a new chunk might be needed to hold the final size.  */

#define obstack_base(h) ((h)->object_base)

/* Size for allocating ordinary chunks.  */

#define obstack_chunk_size(h) ((h)->chunk_size)

/* Pointer to next byte not yet allocated in current chunk.  */

#define obstack_next_free(h)	((h)->next_free)

/* Mask specifying low bits that should be clear in address of an object.  */

#define obstack_alignment_mask(h) ((h)->alignment_mask)

/* To prevent prototype warnings provide complete argument list in
   standard C version.  */
# define obstack_init(h) \
  _obstack_begin ((h), 0, 0, \
		  (void *(*) (long)) obstack_chunk_alloc, (void (*) (void *)) obstack_chunk_free)

# define obstack_begin(h, size) \
  _obstack_begin ((h), (size), 0, \
		  (void *(*) (long)) obstack_chunk_alloc, (void (*) (void *)) obstack_chunk_free)

# define obstack_specify_allocation(h, size, alignment, chunkfun, freefun) \
  _obstack_begin ((h), (size), (alignment), \
		    (void *(*) (long)) (chunkfun), (void (*) (void *)) (freefun))

# define obstack_specify_allocation_with_arg(h, size, alignment, chunkfun, freefun, arg) \
  _obstack_begin_1 ((h), (size), (alignment), \
		    (void *(*) (void *, long)) (chunkfun), \
		    (void (*) (void *, void *)) (freefun), (arg))

# define obstack_chunkfun(h, newchunkfun) \
  ((h) -> chunkfun = (struct _obstack_chunk *(*)(void *, long)) (newchunkfun))

# define obstack_freefun(h, newfreefun) \
  ((h) -> freefun = (void (*)(void *, struct _obstack_chunk *)) (newfreefun))

#define obstack_1grow_fast(h,achar) (*((h)->next_free)++ = (achar))

#define obstack_blank_fast(h,n) ((h)->next_free += (n))

#define obstack_memory_used(h) _obstack_memory_used (h)

#if defined __GNUC__ && defined __STDC__ && __STDC__
/* NextStep 2.0 cc is really gcc 1.93 but it defines __GNUC__ = 2 and
   does not implement __extension__.  But that compiler doesn't define
   __GNUC_MINOR__.  */
# if __GNUC__ < 2 || (__NeXT__ && !__GNUC_MINOR__)
#  define __extension__
# endif

/* For GNU C, if not -traditional,
   we can define these macros to compute all args only once
   without using a global variable.
   Also, we can avoid using the `temp' slot, to make faster code.  */

# define obstack_object_size(OBSTACK)					\
  __extension__								\
  ({ struct obstack *__o = (OBSTACK);					\
     (unsigned) (__o->next_free - __o->object_base); })

# define obstack_room(OBSTACK)						\
  __extension__								\
  ({ struct obstack *__o = (OBSTACK);					\
     (unsigned) (__o->chunk_limit - __o->next_free); })

# define obstack_make_room(OBSTACK,length)				\
__extension__								\
({ struct obstack *__o = (OBSTACK);					\
   int __len = (length);						\
   if (__o->chunk_limit - __o->next_free < __len)			\
     _obstack_newchunk (__o, __len);					\
   (void) 0; })

# define obstack_empty_p(OBSTACK)					\
  __extension__								\
  ({ struct obstack *__o = (OBSTACK);					\
     (__o->chunk->prev == 0 && __o->next_free - __o->chunk->contents == 0); })

# define obstack_grow(OBSTACK,where,length)				\
__extension__								\
({ struct obstack *__o = (OBSTACK);					\
   int __len = (length);						\
   if (__o->next_free + __len > __o->chunk_limit)			\
     _obstack_newchunk (__o, __len);					\
   _obstack_memcpy (__o->next_free, (where), __len);			\
   __o->next_free += __len;						\
   (void) 0; })

# define obstack_grow0(OBSTACK,where,length)				\
__extension__								\
({ struct obstack *__o = (OBSTACK);					\
   int __len = (length);						\
   if (__o->next_free + __len + 1 > __o->chunk_limit)			\
     _obstack_newchunk (__o, __len + 1);				\
   _obstack_memcpy (__o->next_free, (where), __len);			\
   __o->next_free += __len;						\
   *(__o->next_free)++ = 0;						\
   (void) 0; })

# define obstack_1grow(OBSTACK,datum)					\
__extension__								\
({ struct obstack *__o = (OBSTACK);					\
   if (__o->next_free + 1 > __o->chunk_limit)				\
     _obstack_newchunk (__o, 1);					\
   obstack_1grow_fast (__o, datum);					\
   (void) 0; })

/* These assume that the obstack alignment is good enough for pointers or ints,
   and that the data added so far to the current object
   shares that much alignment.  */

# define obstack_ptr_grow(OBSTACK,datum)				\
__extension__								\
({ struct obstack *__o = (OBSTACK);					\
   if (__o->next_free + sizeof (void *) > __o->chunk_limit)		\
     _obstack_newchunk (__o, sizeof (void *));				\
   obstack_ptr_grow_fast (__o, datum); })

# define obstack_int_grow(OBSTACK,datum)				\
__extension__								\
({ struct obstack *__o = (OBSTACK);					\
   if (__o->next_free + sizeof (int) > __o->chunk_limit)		\
     _obstack_newchunk (__o, sizeof (int));				\
   obstack_int_grow_fast (__o, datum); })

# define obstack_ptr_grow_fast(OBSTACK,aptr)				\
__extension__								\
({ struct obstack *__o1 = (OBSTACK);					\
   *(const void **) __o1->next_free = (aptr);				\
   __o1->next_free += sizeof (const void *);				\
   (void) 0; })

# define obstack_int_grow_fast(OBSTACK,aint)				\
__extension__								\
({ struct obstack *__o1 = (OBSTACK);					\
   *(int *) __o1->next_free = (aint);					\
   __o1->next_free += sizeof (int);					\
   (void) 0; })

# define obstack_blank(OBSTACK,length)					\
__extension__								\
({ struct obstack *__o = (OBSTACK);					\
   int __len = (length);						\
   if (__o->chunk_limit - __o->next_free < __len)			\
     _obstack_newchunk (__o, __len);					\
   obstack_blank_fast (__o, __len);					\
   (void) 0; })

# define obstack_alloc(OBSTACK,length)					\
__extension__								\
({ struct obstack *__h = (OBSTACK);					\
   obstack_blank (__h, (length));					\
   obstack_finish (__h); })

# define obstack_copy(OBSTACK,where,length)				\
__extension__								\
({ struct obstack *__h = (OBSTACK);					\
   obstack_grow (__h, (where), (length));				\
   obstack_finish (__h); })

# define obstack_copy0(OBSTACK,where,length)				\
__extension__								\
({ struct obstack *__h = (OBSTACK);					\
   obstack_grow0 (__h, (where), (length));				\
   obstack_finish (__h); })

/* The local variable is named __o1 to avoid a name conflict
   when obstack_blank is called.  */
# define obstack_finish(OBSTACK)  					\
__extension__								\
({ struct obstack *__o1 = (OBSTACK);					\
   void *value;								\
   value = (void *) __o1->object_base;					\
   if (__o1->next_free == value)					\
     __o1->maybe_empty_object = 1;					\
   __o1->next_free							\
     = __INT_TO_PTR ((__PTR_TO_INT (__o1->next_free)+__o1->alignment_mask)\
		     & ~ (__o1->alignment_mask));			\
   if (__o1->next_free - (char *)__o1->chunk				\
       > __o1->chunk_limit - (char *)__o1->chunk)			\
     __o1->next_free = __o1->chunk_limit;				\
   __o1->object_base = __o1->next_free;					\
   value; })

# define obstack_free(OBSTACK, OBJ)					\
__extension__								\
({ struct obstack *__o = (OBSTACK);					\
   void *__obj = (void *) (OBJ);					\
   if (__obj > (void *)__o->chunk && __obj < (void *)__o->chunk_limit)  \
     __o->next_free = __o->object_base = (char *) __obj;		\
   else (obstack_free) (__o, __obj); })

#else /* not __GNUC__ or not __STDC__ */

# define obstack_object_size(h) \
 (unsigned) ((h)->next_free - (h)->object_base)

# define obstack_room(h)		\
 (unsigned) ((h)->chunk_limit - (h)->next_free)

# define obstack_empty_p(h) \
 ((h)->chunk->prev == 0 && (h)->next_free - (h)->chunk->contents == 0)

/* Note that the call to _obstack_newchunk is enclosed in (..., 0)
   so that we can avoid having void expressions
   in the arms of the conditional expression.
   Casting the third operand to void was tried before,
   but some compilers won't accept it.  */

# define obstack_make_room(h,length)					\
( (h)->temp = (length),							\
  (((h)->next_free + (h)->temp > (h)->chunk_limit)			\
   ? (_obstack_newchunk ((h), (h)->temp), 0) : 0))

# define obstack_grow(h,where,length)					\
( (h)->temp = (length),							\
  (((h)->next_free + (h)->temp > (h)->chunk_limit)			\
   ? (_obstack_newchunk ((h), (h)->temp), 0) : 0),			\
  _obstack_memcpy ((h)->next_free, (where), (h)->temp),			\
  (h)->next_free += (h)->temp)

# define obstack_grow0(h,where,length)					\
( (h)->temp = (length),							\
  (((h)->next_free + (h)->temp + 1 > (h)->chunk_limit)			\
   ? (_obstack_newchunk ((h), (h)->temp + 1), 0) : 0),			\
  _obstack_memcpy ((h)->next_free, (where), (h)->temp),			\
  (h)->next_free += (h)->temp,						\
  *((h)->next_free)++ = 0)

# define obstack_1grow(h,datum)						\
( (((h)->next_free + 1 > (h)->chunk_limit)				\
   ? (_obstack_newchunk ((h), 1), 0) : 0),				\
  obstack_1grow_fast (h, datum))

# define obstack_ptr_grow(h,datum)					\
( (((h)->next_free + sizeof (char *) > (h)->chunk_limit)		\
   ? (_obstack_newchunk ((h), sizeof (char *)), 0) : 0),		\
  obstack_ptr_grow_fast (h, datum))

# define obstack_int_grow(h,datum)					\
( (((h)->next_free + sizeof (int) > (h)->chunk_limit)			\
   ? (_obstack_newchunk ((h), sizeof (int)), 0) : 0),			\
  obstack_int_grow_fast (h, datum))

# define obstack_ptr_grow_fast(h,aptr)					\
  (((const void **) ((h)->next_free += sizeof (void *)))[-1] = (aptr))

# define obstack_int_grow_fast(h,aint)					\
  (((int *) ((h)->next_free += sizeof (int)))[-1] = (aptr))

# define obstack_blank(h,length)					\
( (h)->temp = (length),							\
  (((h)->chunk_limit - (h)->next_free < (h)->temp)			\
   ? (_obstack_newchunk ((h), (h)->temp), 0) : 0),			\
  obstack_blank_fast (h, (h)->temp))

# define obstack_alloc(h,length)					\
 (obstack_blank ((h), (length)), obstack_finish ((h)))

# define obstack_copy(h,where,length)					\
 (obstack_grow ((h), (where), (length)), obstack_finish ((h)))

# define obstack_copy0(h,where,length)					\
 (obstack_grow0 ((h), (where), (length)), obstack_finish ((h)))

# define obstack_finish(h)  						\
( ((h)->next_free == (h)->object_base					\
   ? (((h)->maybe_empty_object = 1), 0)					\
   : 0),								\
  (h)->temp = __PTR_TO_INT ((h)->object_base),				\
  (h)->next_free							\
    = __INT_TO_PTR ((__PTR_TO_INT ((h)->next_free)+(h)->alignment_mask)	\
		    & ~ ((h)->alignment_mask)),				\
  (((h)->next_free - (char *) (h)->chunk				\
    > (h)->chunk_limit - (char *) (h)->chunk)				\
   ? ((h)->next_free = (h)->chunk_limit) : 0),				\
  (h)->object_base = (h)->next_free,					\
  __INT_TO_PTR ((h)->temp))

# define obstack_free(h,obj)						\
( (h)->temp = (char *) (obj) - (char *) (h)->chunk,			\
  (((h)->temp > 0 && (h)->temp < (h)->chunk_limit - (char *) (h)->chunk)\
   ? (int) ((h)->next_free = (h)->object_base				\
	    = (h)->temp + (char *) (h)->chunk)				\
   : (((obstack_free) ((h), (h)->temp + (char *) (h)->chunk), 0), 0)))

#endif /* not __GNUC__ or not __STDC__ */

#ifdef __cplusplus
}	/* C++ */
#endif

#endif /* obstack.h */
