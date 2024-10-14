/* mpi-cmp.c  -  MPI functions
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

int mpi_cmp_ui(MPI u, unsigned long v)
{
	mpi_limb_t limb = v;

	mpi_normalize(u);
	if (u->nlimbs == 0) {
		if (v == 0)
			return 0;
		else
			return -1;
	}
	if (u->sign)
		return -1;
	if (u->nlimbs > 1)
		return 1;

	if (u->d[0] == limb)
		return 0;
	else if (u->d[0] > limb)
		return 1;
	else
		return -1;
}
EXPORT_SYMBOL_GPL(mpi_cmp_ui);

int mpi_cmp(MPI u, MPI v)
{
	mpi_size_t usize, vsize;
	int cmp;

	mpi_normalize(u);
	mpi_normalize(v);
	usize = u->nlimbs;
	vsize = v->nlimbs;
	if (!u->sign && v->sign)
		return 1;
	if (u->sign && !v->sign)
		return -1;
	if (usize != vsize && !u->sign && !v->sign)
		return usize - vsize;
	if (usize != vsize && u->sign && v->sign)
		return vsize - usize;
	if (!usize)
		return 0;
	cmp = mpihelp_cmp(u->d, v->d, usize);
	if (u->sign)
		return -cmp;
	return cmp;
}
EXPORT_SYMBOL_GPL(mpi_cmp);
