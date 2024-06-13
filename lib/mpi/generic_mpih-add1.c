// SPDX-License-Identifier: GPL-2.0-or-later
/* mpihelp-add_1.c  -  MPI helper functions
 * Copyright (C) 1994, 1996, 1997, 1998,
 *               2000 Free Software Foundation, Inc.
 *
 * This file is part of GnuPG.
 *
 * Note: This code is heavily based on the GNU MP Library.
 *	 Actually it's the same code with only minor changes in the
 *	 way the data is stored; this is to support the abstraction
 *	 of an optional secure memory allocation which may be used
 *	 to avoid revealing of sensitive data due to paging etc.
 *	 The GNU MP Library itself is published under the LGPL;
 *	 however I decided to publish this code under the plain GPL.
 */

#include "mpi-internal.h"
#include "longlong.h"

mpi_limb_t
mpihelp_add_n(mpi_ptr_t res_ptr, mpi_ptr_t s1_ptr,
	      mpi_ptr_t s2_ptr, mpi_size_t size)
{
	mpi_limb_t x, y, cy;
	mpi_size_t j;

	/* The loop counter and index J goes from -SIZE to -1.  This way
	   the loop becomes faster.  */
	j = -size;

	/* Offset the base pointers to compensate for the negative indices. */
	s1_ptr -= j;
	s2_ptr -= j;
	res_ptr -= j;

	cy = 0;
	do {
		y = s2_ptr[j];
		x = s1_ptr[j];
		y += cy;	/* add previous carry to one addend */
		cy = y < cy;	/* get out carry from that addition */
		y += x;		/* add other addend */
		cy += y < x;	/* get out carry from that add, combine */
		res_ptr[j] = y;
	} while (++j);

	return cy;
}
