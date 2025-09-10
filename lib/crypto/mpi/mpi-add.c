/* mpi-add.c  -  MPI functions
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

#include <linux/export.h>

#include "mpi-internal.h"

int mpi_add(MPI w, MPI u, MPI v)
{
	mpi_ptr_t wp, up, vp;
	mpi_size_t usize, vsize, wsize;
	int usign, vsign, wsign;
	int err;

	if (u->nlimbs < v->nlimbs) { /* Swap U and V. */
		usize = v->nlimbs;
		usign = v->sign;
		vsize = u->nlimbs;
		vsign = u->sign;
		wsize = usize + 1;
		err = RESIZE_IF_NEEDED(w, wsize);
		if (err)
			return err;
		/* These must be after realloc (u or v may be the same as w).  */
		up = v->d;
		vp = u->d;
	} else {
		usize = u->nlimbs;
		usign = u->sign;
		vsize = v->nlimbs;
		vsign = v->sign;
		wsize = usize + 1;
		err = RESIZE_IF_NEEDED(w, wsize);
		if (err)
			return err;
		/* These must be after realloc (u or v may be the same as w).  */
		up = u->d;
		vp = v->d;
	}
	wp = w->d;
	wsign = 0;

	if (!vsize) {  /* simple */
		MPN_COPY(wp, up, usize);
		wsize = usize;
		wsign = usign;
	} else if (usign != vsign) { /* different sign */
		/* This test is right since USIZE >= VSIZE */
		if (usize != vsize) {
			mpihelp_sub(wp, up, usize, vp, vsize);
			wsize = usize;
			MPN_NORMALIZE(wp, wsize);
			wsign = usign;
		} else if (mpihelp_cmp(up, vp, usize) < 0) {
			mpihelp_sub_n(wp, vp, up, usize);
			wsize = usize;
			MPN_NORMALIZE(wp, wsize);
			if (!usign)
				wsign = 1;
		} else {
			mpihelp_sub_n(wp, up, vp, usize);
			wsize = usize;
			MPN_NORMALIZE(wp, wsize);
			if (usign)
				wsign = 1;
		}
	} else { /* U and V have same sign. Add them. */
		mpi_limb_t cy = mpihelp_add(wp, up, usize, vp, vsize);
		wp[usize] = cy;
		wsize = usize + cy;
		if (usign)
			wsign = 1;
	}

	w->nlimbs = wsize;
	w->sign = wsign;
	return 0;
}
EXPORT_SYMBOL_GPL(mpi_add);

int mpi_sub(MPI w, MPI u, MPI v)
{
	int err;
	MPI vv;

	vv = mpi_copy(v);
	if (!vv)
		return -ENOMEM;

	vv->sign = !vv->sign;
	err = mpi_add(w, u, vv);
	mpi_free(vv);

	return err;
}
EXPORT_SYMBOL_GPL(mpi_sub);

int mpi_addm(MPI w, MPI u, MPI v, MPI m)
{
	return mpi_add(w, u, v) ?:
	       mpi_mod(w, w, m);
}
EXPORT_SYMBOL_GPL(mpi_addm);

int mpi_subm(MPI w, MPI u, MPI v, MPI m)
{
	return mpi_sub(w, u, v) ?:
	       mpi_mod(w, w, m);
}
EXPORT_SYMBOL_GPL(mpi_subm);
