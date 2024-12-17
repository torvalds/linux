/* mpi-bit.c  -  MPI bit level functions
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
			n = count_leading_zeros(alimb);
		else
			n = BITS_PER_MPI_LIMB;
		n = BITS_PER_MPI_LIMB - n + (a->nlimbs - 1) * BITS_PER_MPI_LIMB;
	} else
		n = 0;
	return n;
}
EXPORT_SYMBOL_GPL(mpi_get_nbits);

/****************
 * Test whether bit N is set.
 */
int mpi_test_bit(MPI a, unsigned int n)
{
	unsigned int limbno, bitno;
	mpi_limb_t limb;

	limbno = n / BITS_PER_MPI_LIMB;
	bitno  = n % BITS_PER_MPI_LIMB;

	if (limbno >= a->nlimbs)
		return 0; /* too far left: this is a 0 */
	limb = a->d[limbno];
	return (limb & (A_LIMB_1 << bitno)) ? 1 : 0;
}
EXPORT_SYMBOL_GPL(mpi_test_bit);

/****************
 * Set bit N of A.
 */
int mpi_set_bit(MPI a, unsigned int n)
{
	unsigned int i, limbno, bitno;
	int err;

	limbno = n / BITS_PER_MPI_LIMB;
	bitno  = n % BITS_PER_MPI_LIMB;

	if (limbno >= a->nlimbs) {
		for (i = a->nlimbs; i < a->alloced; i++)
			a->d[i] = 0;
		err = mpi_resize(a, limbno+1);
		if (err)
			return err;
		a->nlimbs = limbno+1;
	}
	a->d[limbno] |= (A_LIMB_1<<bitno);
	return 0;
}
EXPORT_SYMBOL_GPL(mpi_set_bit);

/*
 * Shift A by N bits to the right.
 */
int mpi_rshift(MPI x, MPI a, unsigned int n)
{
	mpi_size_t xsize;
	unsigned int i;
	unsigned int nlimbs = (n/BITS_PER_MPI_LIMB);
	unsigned int nbits = (n%BITS_PER_MPI_LIMB);
	int err;

	if (x == a) {
		/* In-place operation.  */
		if (nlimbs >= x->nlimbs) {
			x->nlimbs = 0;
			return 0;
		}

		if (nlimbs) {
			for (i = 0; i < x->nlimbs - nlimbs; i++)
				x->d[i] = x->d[i+nlimbs];
			x->d[i] = 0;
			x->nlimbs -= nlimbs;
		}
		if (x->nlimbs && nbits)
			mpihelp_rshift(x->d, x->d, x->nlimbs, nbits);
	} else if (nlimbs) {
		/* Copy and shift by more or equal bits than in a limb. */
		xsize = a->nlimbs;
		x->sign = a->sign;
		err = RESIZE_IF_NEEDED(x, xsize);
		if (err)
			return err;
		x->nlimbs = xsize;
		for (i = 0; i < a->nlimbs; i++)
			x->d[i] = a->d[i];
		x->nlimbs = i;

		if (nlimbs >= x->nlimbs) {
			x->nlimbs = 0;
			return 0;
		}

		for (i = 0; i < x->nlimbs - nlimbs; i++)
			x->d[i] = x->d[i+nlimbs];
		x->d[i] = 0;
		x->nlimbs -= nlimbs;

		if (x->nlimbs && nbits)
			mpihelp_rshift(x->d, x->d, x->nlimbs, nbits);
	} else {
		/* Copy and shift by less than bits in a limb.  */
		xsize = a->nlimbs;
		x->sign = a->sign;
		err = RESIZE_IF_NEEDED(x, xsize);
		if (err)
			return err;
		x->nlimbs = xsize;

		if (xsize) {
			if (nbits)
				mpihelp_rshift(x->d, a->d, x->nlimbs, nbits);
			else {
				/* The rshift helper function is not specified for
				 * NBITS==0, thus we do a plain copy here.
				 */
				for (i = 0; i < x->nlimbs; i++)
					x->d[i] = a->d[i];
			}
		}
	}
	MPN_NORMALIZE(x->d, x->nlimbs);

	return 0;
}
EXPORT_SYMBOL_GPL(mpi_rshift);
