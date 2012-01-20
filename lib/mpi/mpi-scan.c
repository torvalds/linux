/* mpi-scan.c  -  MPI functions
 * Copyright (C) 1998, 1999, 2000, 2001 Free Software Foundation, Inc.
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

/****************
 * Scan through an mpi and return byte for byte. a -1 is returned to indicate
 * the end of the mpi. Scanning is done from the lsb to the msb, returned
 * values are in the range of 0 .. 255.
 *
 * FIXME: This code is VERY ugly!
 */
int mpi_getbyte(const MPI a, unsigned idx)
{
	int i, j;
	unsigned n;
	mpi_ptr_t ap;
	mpi_limb_t limb;

	ap = a->d;
	for (n = 0, i = 0; i < a->nlimbs; i++) {
		limb = ap[i];
		for (j = 0; j < BYTES_PER_MPI_LIMB; j++, n++)
			if (n == idx)
				return (limb >> j * 8) & 0xff;
	}
	return -1;
}

/****************
 * Put a value at position IDX into A. idx counts from lsb to msb
 */
void mpi_putbyte(MPI a, unsigned idx, int xc)
{
	int i, j;
	unsigned n;
	mpi_ptr_t ap;
	mpi_limb_t limb, c;

	c = xc & 0xff;
	ap = a->d;
	for (n = 0, i = 0; i < a->alloced; i++) {
		limb = ap[i];
		for (j = 0; j < BYTES_PER_MPI_LIMB; j++, n++)
			if (n == idx) {
#if BYTES_PER_MPI_LIMB == 4
				if (j == 0)
					limb = (limb & 0xffffff00) | c;
				else if (j == 1)
					limb = (limb & 0xffff00ff) | (c << 8);
				else if (j == 2)
					limb = (limb & 0xff00ffff) | (c << 16);
				else
					limb = (limb & 0x00ffffff) | (c << 24);
#elif BYTES_PER_MPI_LIMB == 8
				if (j == 0)
					limb = (limb & 0xffffffffffffff00) | c;
				else if (j == 1)
					limb =
					    (limb & 0xffffffffffff00ff) | (c <<
									   8);
				else if (j == 2)
					limb =
					    (limb & 0xffffffffff00ffff) | (c <<
									   16);
				else if (j == 3)
					limb =
					    (limb & 0xffffffff00ffffff) | (c <<
									   24);
				else if (j == 4)
					limb =
					    (limb & 0xffffff00ffffffff) | (c <<
									   32);
				else if (j == 5)
					limb =
					    (limb & 0xffff00ffffffffff) | (c <<
									   40);
				else if (j == 6)
					limb =
					    (limb & 0xff00ffffffffffff) | (c <<
									   48);
				else
					limb =
					    (limb & 0x00ffffffffffffff) | (c <<
									   56);
#else
#error please enhance this function, its ugly - i know.
#endif
				if (a->nlimbs <= i)
					a->nlimbs = i + 1;
				ap[i] = limb;
				return;
			}
	}
	log_bug("index out of range\n");
}

/****************
 * Count the number of zerobits at the low end of A
 */
unsigned mpi_trailing_zeros(const MPI a)
{
	unsigned n, count = 0;

	for (n = 0; n < a->nlimbs; n++) {
		if (a->d[n]) {
			unsigned nn;
			mpi_limb_t alimb = a->d[n];

			count_trailing_zeros(nn, alimb);
			count += nn;
			break;
		}
		count += BITS_PER_MPI_LIMB;
	}
	return count;

}
