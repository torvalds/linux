/* Sorting algorithms.
   Copyright (C) 2000 Free Software Foundation, Inc.
   Contributed by Mark Mitchell <mark@codesourcery.com>.

This file is part of GNU CC.
   
GNU CC is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GNU CC is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU CC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street - Fifth Floor,
Boston, MA 02110-1301, USA.  */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "libiberty.h"
#include "sort.h"
#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif
#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifndef UCHAR_MAX
#define UCHAR_MAX ((unsigned char)(-1))
#endif

/* POINTERS and WORK are both arrays of N pointers.  When this
   function returns POINTERS will be sorted in ascending order.  */

void sort_pointers (size_t n, void **pointers, void **work)
{
  /* The type of a single digit.  This can be any unsigned integral
     type.  When changing this, DIGIT_MAX should be changed as 
     well.  */
  typedef unsigned char digit_t;

  /* The maximum value a single digit can have.  */
#define DIGIT_MAX (UCHAR_MAX + 1)

  /* The Ith entry is the number of elements in *POINTERSP that have I
     in the digit on which we are currently sorting.  */
  unsigned int count[DIGIT_MAX];
  /* Nonzero if we are running on a big-endian machine.  */
  int big_endian_p;
  size_t i;
  size_t j;

  /* The algorithm used here is radix sort which takes time linear in
     the number of elements in the array.  */

  /* The algorithm here depends on being able to swap the two arrays
     an even number of times.  */
  if ((sizeof (void *) / sizeof (digit_t)) % 2 != 0)
    abort ();

  /* Figure out the endianness of the machine.  */
  for (i = 0, j = 0; i < sizeof (size_t); ++i)
    {
      j *= (UCHAR_MAX + 1);
      j += i;
    }
  big_endian_p = (((char *)&j)[0] == 0);

  /* Move through the pointer values from least significant to most
     significant digits.  */
  for (i = 0; i < sizeof (void *) / sizeof (digit_t); ++i)
    {
      digit_t *digit;
      digit_t *bias;
      digit_t *top;
      unsigned int *countp;
      void **pointerp;

      /* The offset from the start of the pointer will depend on the
	 endianness of the machine.  */
      if (big_endian_p)
	j = sizeof (void *) / sizeof (digit_t) - i;
      else
	j = i;
	
      /* Now, perform a stable sort on this digit.  We use counting
	 sort.  */
      memset (count, 0, DIGIT_MAX * sizeof (unsigned int));

      /* Compute the address of the appropriate digit in the first and
	 one-past-the-end elements of the array.  On a little-endian
	 machine, the least-significant digit is closest to the front.  */
      bias = ((digit_t *) pointers) + j;
      top = ((digit_t *) (pointers + n)) + j;

      /* Count how many there are of each value.  At the end of this
	 loop, COUNT[K] will contain the number of pointers whose Ith
	 digit is K.  */
      for (digit = bias; 
	   digit < top; 
	   digit += sizeof (void *) / sizeof (digit_t))
	++count[*digit];

      /* Now, make COUNT[K] contain the number of pointers whose Ith
	 digit is less than or equal to K.  */
      for (countp = count + 1; countp < count + DIGIT_MAX; ++countp)
	*countp += countp[-1];

      /* Now, drop the pointers into their correct locations.  */
      for (pointerp = pointers + n - 1; pointerp >= pointers; --pointerp)
	work[--count[((digit_t *) pointerp)[j]]] = *pointerp;

      /* Swap WORK and POINTERS so that POINTERS contains the sorted
	 array.  */
      pointerp = pointers;
      pointers = work;
      work = pointerp;
    }
}

/* Everything below here is a unit test for the routines in this
   file.  */

#ifdef UNIT_TEST

#include <stdio.h>

void *xmalloc (size_t n)
{
  return malloc (n);
}

int main (int argc, char **argv)
{
  int k;
  int result;
  size_t i;
  void **pointers;
  void **work;

  if (argc > 1)
    k = atoi (argv[1]);
  else
    k = 10;

  pointers = XNEWVEC (void*, k);
  work = XNEWVEC (void*, k);

  for (i = 0; i < k; ++i)
    {
      pointers[i] = (void *) random ();
      printf ("%x\n", pointers[i]);
    }

  sort_pointers (k, pointers, work);

  printf ("\nSorted\n\n");

  result = 0;

  for (i = 0; i < k; ++i)
    {
      printf ("%x\n", pointers[i]);
      if (i > 0 && (char*) pointers[i] < (char*) pointers[i - 1])
	result = 1;
    }

  free (pointers);
  free (work);

  return result;
}

#endif
