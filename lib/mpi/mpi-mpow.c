/* mpi-mpow.c  -  MPI functions
 * Copyright (C) 1998, 1999, 2000 Free Software Foundation, Inc.
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

static int build_index(const MPI *exparray, int k, int i, int t)
{
	int j, bitno;
	int index = 0;

	bitno = t - i;
	for (j = k - 1; j >= 0; j--) {
		index <<= 1;
		if (mpi_test_bit(exparray[j], bitno))
			index |= 1;
	}
	return index;
}

/****************
 * RES = (BASE[0] ^ EXP[0]) *  (BASE[1] ^ EXP[1]) * ... * mod M
 */
int mpi_mulpowm(MPI res, MPI *basearray, MPI *exparray, MPI m)
{
	int rc = -ENOMEM;
	int k;			/* number of elements */
	int t;			/* bit size of largest exponent */
	int i, j, idx;
	MPI *G = NULL;		/* table with precomputed values of size 2^k */
	MPI tmp = NULL;

	for (k = 0; basearray[k]; k++)
		;
	if (!k) {
		pr_emerg("mpi_mulpowm: assert(k) failed\n");
		BUG();
	}
	for (t = 0, i = 0; (tmp = exparray[i]); i++) {
		j = mpi_get_nbits(tmp);
		if (j > t)
			t = j;
	}
	if (i != k) {
		pr_emerg("mpi_mulpowm: assert(i==k) failed\n");
		BUG();
	}
	if (!t) {
		pr_emerg("mpi_mulpowm: assert(t) failed\n");
		BUG();
	}
	if (k >= 10) {
		pr_emerg("mpi_mulpowm: assert(k<10) failed\n");
		BUG();
	}

	G = kzalloc((1 << k) * sizeof *G, GFP_KERNEL);
	if (!G)
		goto err_out;

	/* and calculate */
	tmp = mpi_alloc(mpi_get_nlimbs(m) + 1);
	if (!tmp)
		goto nomem;
	if (mpi_set_ui(res, 1) < 0)
		goto nomem;
	for (i = 1; i <= t; i++) {
		if (mpi_mulm(tmp, res, res, m) < 0)
			goto nomem;
		idx = build_index(exparray, k, i, t);
		if (!(idx >= 0 && idx < (1 << k))) {
			pr_emerg("mpi_mulpowm: assert(idx >= 0 && idx < (1<<k)) failed\n");
			BUG();
		}
		if (!G[idx]) {
			if (!idx) {
				G[0] = mpi_alloc_set_ui(1);
				if (!G[0])
					goto nomem;
			} else {
				for (j = 0; j < k; j++) {
					if ((idx & (1 << j))) {
						if (!G[idx]) {
							if (mpi_copy
							    (&G[idx],
							     basearray[j]) < 0)
								goto nomem;
						} else {
							if (mpi_mulm
							    (G[idx], G[idx],
							     basearray[j],
							     m) < 0)
								goto nomem;
						}
					}
				}
				if (!G[idx]) {
					G[idx] = mpi_alloc(0);
					if (!G[idx])
						goto nomem;
				}
			}
		}
		if (mpi_mulm(res, tmp, G[idx], m) < 0)
			goto nomem;
	}

	rc = 0;
nomem:
	/* cleanup */
	mpi_free(tmp);
	for (i = 0; i < (1 << k); i++)
		mpi_free(G[i]);
	kfree(G);
err_out:
	return rc;
}
