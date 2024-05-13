// SPDX-License-Identifier: GPL-2.0-or-later
/* mpihelp-sub.c  -  MPI helper functions
 *	Copyright (C) 1994, 1996 Free Software Foundation, Inc.
 *	Copyright (C) 1998, 1999, 2000, 2001 Free Software Foundation, Inc.
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

/****************
 * Compare OP1_PTR/OP1_SIZE with OP2_PTR/OP2_SIZE.
 * There are no restrictions on the relative sizes of
 * the two arguments.
 * Return 1 if OP1 > OP2, 0 if they are equal, and -1 if OP1 < OP2.
 */
int mpihelp_cmp(mpi_ptr_t op1_ptr, mpi_ptr_t op2_ptr, mpi_size_t size)
{
	mpi_size_t i;
	mpi_limb_t op1_word, op2_word;

	for (i = size - 1; i >= 0; i--) {
		op1_word = op1_ptr[i];
		op2_word = op2_ptr[i];
		if (op1_word != op2_word)
			goto diff;
	}
	return 0;

diff:
	/* This can *not* be simplified to
	 *   op2_word - op2_word
	 * since that expression might give signed overflow.  */
	return (op1_word > op2_word) ? 1 : -1;
}
