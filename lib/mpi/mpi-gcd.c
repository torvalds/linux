/* mpi-gcd.c  -  MPI functions
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

/****************
 * Find the greatest common divisor G of A and B.
 * Return: true if this 1, false in all other cases
 */
int mpi_gcd(MPI g, const MPI xa, const MPI xb)
{
	MPI a = NULL, b = NULL;

	if (mpi_copy(&a, xa) < 0)
		goto nomem;

	if (mpi_copy(&b, xb) < 0)
		goto nomem;

	/* TAOCP Vol II, 4.5.2, Algorithm A */
	a->sign = 0;
	b->sign = 0;
	while (mpi_cmp_ui(b, 0)) {
		if (mpi_fdiv_r(g, a, b) < 0)	/* g used as temorary variable */
			goto nomem;
		if (mpi_set(a, b) < 0)
			goto nomem;
		if (mpi_set(b, g) < 0)
			goto nomem;
	}
	if (mpi_set(g, a) < 0)
		goto nomem;

	mpi_free(a);
	mpi_free(b);
	return !mpi_cmp_ui(g, 1);

nomem:
	mpi_free(a);
	mpi_free(b);
	return -ENOMEM;
}
