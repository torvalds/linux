/* mpi-bit.c  -  MPI bit level fucntions
 * Copyright (C) 1998, 1999 Free Software Foundation, Inc.
 *
 * This file is part of GnuPG.
 *
 * GnuPG is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * GnuPG is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */

#include "mpi-internal.h"
#include "longlong.h"

#define A_LIMB_1 ((mpi_limb_t) 1)

/****************
 * Sometimes we have MSL (most significant limbs) which are 0;
 * this is for some reasons not good, so this function removes them.
 */
void mpi_normalize(MPI a)
{
	for (; a->nlimbs && !a->d[a->nlimbs - 1]; a->nlimbs--)
		;
}

/****************
 * Return the number of bits in A.
 */
unsigned mpi_get_nbits(MPI a)
{
	unsigned n;

	mpi_normalize(a);

	if (a->nlimbs) {
		mpi_limb_t alimb = a->d[a->nlimbs - 1];
		if (alimb)
			count_leading_zeros(n, alimb);
		else
			n = BITS_PER_MPI_LIMB;
		n = BITS_PER_MPI_LIMB - n + (a->nlimbs - 1) * BITS_PER_MPI_LIMB;
	} else
		n = 0;
	return n;
}
EXPORT_SYMBOL_GPL(mpi_get_nbits);
