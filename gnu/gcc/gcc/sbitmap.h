/* Simple bitmaps.
   Copyright (C) 1999, 2000, 2002, 2003, 2004 Free Software Foundation, Inc.

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

#ifndef GCC_SBITMAP_H
#define GCC_SBITMAP_H

/* It's not clear yet whether using bitmap.[ch] will be a win.
   It should be straightforward to convert so for now we keep things simple
   while more important issues are dealt with.  */

#define SBITMAP_ELT_BITS ((unsigned) HOST_BITS_PER_WIDEST_FAST_INT)
#define SBITMAP_ELT_TYPE unsigned HOST_WIDEST_FAST_INT

typedef struct simple_bitmap_def
{
  unsigned int n_bits;		/* Number of bits.  */
  unsigned int size;		/* Size in elements.  */
  unsigned int bytes;		/* Size in bytes.  */
  SBITMAP_ELT_TYPE elms[1];	/* The elements.  */
} *sbitmap;

typedef SBITMAP_ELT_TYPE *sbitmap_ptr;

/* Return the set size needed for N elements.  */
#define SBITMAP_SET_SIZE(N) (((N) + SBITMAP_ELT_BITS - 1) / SBITMAP_ELT_BITS)

/* Set bit number bitno in the bitmap.  */
#define SET_BIT(BITMAP, BITNO)					\
  ((BITMAP)->elms [(BITNO) / SBITMAP_ELT_BITS]			\
   |= (SBITMAP_ELT_TYPE) 1 << (BITNO) % SBITMAP_ELT_BITS)

/* Test if bit number bitno in the bitmap is set.  */
#define TEST_BIT(BITMAP, BITNO) \
((BITMAP)->elms [(BITNO) / SBITMAP_ELT_BITS] >> (BITNO) % SBITMAP_ELT_BITS & 1)

/* Reset bit number bitno in the bitmap.  */
#define RESET_BIT(BITMAP, BITNO)				\
  ((BITMAP)->elms [(BITNO) / SBITMAP_ELT_BITS]			\
   &= ~((SBITMAP_ELT_TYPE) 1 << (BITNO) % SBITMAP_ELT_BITS))

/* The iterator for sbitmap.  */
typedef struct {
  /* The pointer to the first word of the bitmap.  */
  SBITMAP_ELT_TYPE *ptr;

  /* The size of the bitmap.  */
  unsigned int size;

  /* The current word index.  */
  unsigned int word_num;

  /* The current bit index (not modulo SBITMAP_ELT_BITS).  */
  unsigned int bit_num;

  /* The words currently visited.  */
  SBITMAP_ELT_TYPE word;
} sbitmap_iterator;

/* Initialize the iterator I with sbitmap BMP and the initial index
   MIN.  */

static inline void
sbitmap_iter_init (sbitmap_iterator *i, sbitmap bmp, unsigned int min)
{
  i->word_num = min / (unsigned int) SBITMAP_ELT_BITS;
  i->bit_num = min;
  i->size = bmp->size;
  i->ptr = bmp->elms;

  if (i->word_num >= i->size)
    i->word = 0;
  else
    i->word = (i->ptr[i->word_num]
	       >> (i->bit_num % (unsigned int) SBITMAP_ELT_BITS));
}

/* Return true if we have more bits to visit, in which case *N is set
   to the index of the bit to be visited.  Otherwise, return
   false.  */

static inline bool
sbitmap_iter_cond (sbitmap_iterator *i, unsigned int *n)
{
  /* Skip words that are zeros.  */
  for (; i->word == 0; i->word = i->ptr[i->word_num])
    {
      i->word_num++;

      /* If we have reached the end, break.  */
      if (i->word_num >= i->size)
	return false;

      i->bit_num = i->word_num * SBITMAP_ELT_BITS;
    }

  /* Skip bits that are zero.  */
  for (; (i->word & 1) == 0; i->word >>= 1)
    i->bit_num++;

  *n = i->bit_num;

  return true;
}

/* Advance to the next bit.  */

static inline void
sbitmap_iter_next (sbitmap_iterator *i)
{
  i->word >>= 1;
  i->bit_num++;
}

/* Loop over all elements of SBITMAP, starting with MIN.  In each
   iteration, N is set to the index of the bit being visited.  ITER is
   an instance of sbitmap_iterator used to iterate the bitmap.  */

#define EXECUTE_IF_SET_IN_SBITMAP(SBITMAP, MIN, N, ITER)	\
  for (sbitmap_iter_init (&(ITER), (SBITMAP), (MIN));		\
       sbitmap_iter_cond (&(ITER), &(N));			\
       sbitmap_iter_next (&(ITER)))

#define EXECUTE_IF_SET_IN_SBITMAP_REV(SBITMAP, N, CODE)			\
do {									\
  unsigned int word_num_;						\
  unsigned int bit_num_;						\
  unsigned int size_ = (SBITMAP)->size;					\
  SBITMAP_ELT_TYPE *ptr_ = (SBITMAP)->elms;				\
									\
  for (word_num_ = size_; word_num_ > 0; word_num_--)			\
    {									\
      SBITMAP_ELT_TYPE word_ = ptr_[word_num_ - 1];			\
									\
      if (word_ != 0)							\
	for (bit_num_ = SBITMAP_ELT_BITS; bit_num_ > 0; bit_num_--)	\
	  {								\
	    SBITMAP_ELT_TYPE _mask = (SBITMAP_ELT_TYPE)1 << (bit_num_ - 1);\
									\
	    if ((word_ & _mask) != 0)					\
	      {								\
		word_ &= ~ _mask;					\
		(N) = (word_num_ - 1) * SBITMAP_ELT_BITS + bit_num_ - 1;\
		CODE;							\
		if (word_ == 0)						\
		  break;						\
	      }								\
	  }								\
    }									\
} while (0)

#define sbitmap_free(MAP)		free(MAP)
#define sbitmap_vector_free(VEC)	free(VEC)

struct int_list;

extern void dump_sbitmap (FILE *, sbitmap);
extern void dump_sbitmap_file (FILE *, sbitmap);
extern void dump_sbitmap_vector (FILE *, const char *, const char *, sbitmap *,
				 int);
extern sbitmap sbitmap_alloc (unsigned int);
extern sbitmap *sbitmap_vector_alloc (unsigned int, unsigned int);
extern sbitmap sbitmap_resize (sbitmap, unsigned int, int);
extern void sbitmap_copy (sbitmap, sbitmap);
extern int sbitmap_equal (sbitmap, sbitmap);
extern void sbitmap_zero (sbitmap);
extern void sbitmap_ones (sbitmap);
extern void sbitmap_vector_zero (sbitmap *, unsigned int);
extern void sbitmap_vector_ones (sbitmap *, unsigned int);

extern void sbitmap_union_of_diff (sbitmap, sbitmap, sbitmap, sbitmap);
extern bool sbitmap_union_of_diff_cg (sbitmap, sbitmap, sbitmap, sbitmap);
extern void sbitmap_difference (sbitmap, sbitmap, sbitmap);
extern void sbitmap_not (sbitmap, sbitmap);
extern void sbitmap_a_or_b_and_c (sbitmap, sbitmap, sbitmap, sbitmap);
extern bool sbitmap_a_or_b_and_c_cg (sbitmap, sbitmap, sbitmap, sbitmap);
extern void sbitmap_a_and_b_or_c (sbitmap, sbitmap, sbitmap, sbitmap);
extern bool sbitmap_a_and_b_or_c_cg (sbitmap, sbitmap, sbitmap, sbitmap);
extern bool sbitmap_any_common_bits (sbitmap, sbitmap);
extern void sbitmap_a_and_b (sbitmap, sbitmap, sbitmap);
extern bool sbitmap_a_and_b_cg (sbitmap, sbitmap, sbitmap);
extern void sbitmap_a_or_b (sbitmap, sbitmap, sbitmap);
extern bool sbitmap_a_or_b_cg (sbitmap, sbitmap, sbitmap);
extern void sbitmap_a_xor_b (sbitmap, sbitmap, sbitmap);
extern bool sbitmap_a_xor_b_cg (sbitmap, sbitmap, sbitmap);
extern bool sbitmap_a_subset_b_p (sbitmap, sbitmap);

extern int sbitmap_first_set_bit (sbitmap);
extern int sbitmap_last_set_bit (sbitmap);

extern void sbitmap_intersect_of_predsucc (sbitmap, sbitmap *, int,
					   struct int_list **);
#define sbitmap_intersect_of_predecessors  sbitmap_intersect_of_predsucc
#define sbitmap_intersect_of_successors    sbitmap_intersect_of_predsucc

extern void sbitmap_union_of_predsucc (sbitmap, sbitmap *, int,
				       struct int_list **);
#define sbitmap_union_of_predecessors  sbitmap_union_of_predsucc
#define sbitmap_union_of_successors    sbitmap_union_of_predsucc

/* Intersection and Union of preds/succs using the new flow graph
   structure instead of the pred/succ arrays.  */

extern void sbitmap_intersection_of_succs (sbitmap, sbitmap *, int);
extern void sbitmap_intersection_of_preds (sbitmap, sbitmap *, int);
extern void sbitmap_union_of_succs (sbitmap, sbitmap *, int);
extern void sbitmap_union_of_preds (sbitmap, sbitmap *, int);

extern void debug_sbitmap (sbitmap);
extern sbitmap sbitmap_realloc (sbitmap, unsigned int);
#endif /* ! GCC_SBITMAP_H */
