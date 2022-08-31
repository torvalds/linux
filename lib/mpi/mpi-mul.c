/* mpi-mul.c  -  MPI functions
 * Copyright (C) 1994, 1996, 1998, 2001, 2002,
 *               2003 Free Software Foundation, Inc.
 *
 * This file is part of Libgcrypt.
 *
 * Note: This code is heavily based on the GNU MP Library.
 *	 Actually it's the same code with only minor changes in the
 *	 way the data is stored; this is to support the abstraction
 *	 of an optional secure memory allocation which may be used
 *	 to avoid revealing of sensitive data due to paging etc.
 */

#include "mpi-internal.h"

void mpi_mul(MPI w, MPI u, MPI v)
{
	mpi_size_t usize, vsize, wsize;
	mpi_ptr_t up, vp, wp;
	mpi_limb_t cy;
	int usign, vsign, sign_product;
	int assign_wp = 0;
	mpi_ptr_t tmp_limb = NULL;

	if (u->nlimbs < v->nlimbs) {
		/* Swap U and V. */
		usize = v->nlimbs;
		usign = v->sign;
		up    = v->d;
		vsize = u->nlimbs;
		vsign = u->sign;
		vp    = u->d;
	} else {
		usize = u->nlimbs;
		usign = u->sign;
		up    = u->d;
		vsize = v->nlimbs;
		vsign = v->sign;
		vp    = v->d;
	}
	sign_product = usign ^ vsign;
	wp = w->d;

	/* Ensure W has space enough to store the result.  */
	wsize = usize + vsize;
	if (w->alloced < wsize) {
		if (wp == up || wp == vp) {
			wp = mpi_alloc_limb_space(wsize);
			assign_wp = 1;
		} else {
			mpi_resize(w, wsize);
			wp = w->d;
		}
	} else { /* Make U and V not overlap with W.	*/
		if (wp == up) {
			/* W and U are identical.  Allocate temporary space for U. */
			up = tmp_limb = mpi_alloc_limb_space(usize);
			/* Is V identical too?  Keep it identical with U.  */
			if (wp == vp)
				vp = up;
			/* Copy to the temporary space.  */
			MPN_COPY(up, wp, usize);
		} else if (wp == vp) {
			/* W and V are identical.  Allocate temporary space for V. */
			vp = tmp_limb = mpi_alloc_limb_space(vsize);
			/* Copy to the temporary space.  */
			MPN_COPY(vp, wp, vsize);
		}
	}

	if (!vsize)
		wsize = 0;
	else {
		mpihelp_mul(wp, up, usize, vp, vsize, &cy);
		wsize -= cy ? 0:1;
	}

	if (assign_wp)
		mpi_assign_limb_space(w, wp, wsize);
	w->nlimbs = wsize;
	w->sign = sign_product;
	if (tmp_limb)
		mpi_free_limb_space(tmp_limb);
}
EXPORT_SYMBOL_GPL(mpi_mul);

void mpi_mulm(MPI w, MPI u, MPI v, MPI m)
{
	mpi_mul(w, u, v);
	mpi_tdiv_r(w, w, m);
}
EXPORT_SYMBOL_GPL(mpi_mulm);
