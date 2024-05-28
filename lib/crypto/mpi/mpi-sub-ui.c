// SPDX-License-Identifier: GPL-2.0-or-later
/* mpi-sub-ui.c - Subtract an unsigned integer from an MPI.
 *
 * Copyright 1991, 1993, 1994, 1996, 1999-2002, 2004, 2012, 2013, 2015
 * Free Software Foundation, Inc.
 *
 * This file was based on the GNU MP Library source file:
 * https://gmplib.org/repo/gmp-6.2/file/510b83519d1c/mpz/aors_ui.h
 *
 * The GNU MP Library is free software; you can redistribute it and/or modify
 * it under the terms of either:
 *
 *   * the GNU Lesser General Public License as published by the Free
 *     Software Foundation; either version 3 of the License, or (at your
 *     option) any later version.
 *
 * or
 *
 *   * the GNU General Public License as published by the Free Software
 *     Foundation; either version 2 of the License, or (at your option) any
 *     later version.
 *
 * or both in parallel, as here.
 *
 * The GNU MP Library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received copies of the GNU General Public License and the
 * GNU Lesser General Public License along with the GNU MP Library.  If not,
 * see https://www.gnu.org/licenses/.
 */

#include "mpi-internal.h"

int mpi_sub_ui(MPI w, MPI u, unsigned long vval)
{
	if (u->nlimbs == 0) {
		if (mpi_resize(w, 1) < 0)
			return -ENOMEM;
		w->d[0] = vval;
		w->nlimbs = (vval != 0);
		w->sign = (vval != 0);
		return 0;
	}

	/* If not space for W (and possible carry), increase space. */
	if (mpi_resize(w, u->nlimbs + 1))
		return -ENOMEM;

	if (u->sign) {
		mpi_limb_t cy;

		cy = mpihelp_add_1(w->d, u->d, u->nlimbs, (mpi_limb_t) vval);
		w->d[u->nlimbs] = cy;
		w->nlimbs = u->nlimbs + cy;
		w->sign = 1;
	} else {
		/* The signs are different.  Need exact comparison to determine
		 * which operand to subtract from which.
		 */
		if (u->nlimbs == 1 && u->d[0] < vval) {
			w->d[0] = vval - u->d[0];
			w->nlimbs = 1;
			w->sign = 1;
		} else {
			mpihelp_sub_1(w->d, u->d, u->nlimbs, (mpi_limb_t) vval);
			/* Size can decrease with at most one limb. */
			w->nlimbs = (u->nlimbs - (w->d[u->nlimbs - 1] == 0));
			w->sign = 0;
		}
	}

	mpi_normalize(w);
	return 0;
}
EXPORT_SYMBOL_GPL(mpi_sub_ui);
