/* mpiutil.ac  -  Utility functions for MPI
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

/* Constants allocated right away at startup.  */
static MPI constants[MPI_NUMBER_OF_CONSTANTS];

/* Initialize the MPI subsystem.  This is called early and allows to
 * do some initialization without taking care of threading issues.
 */
static int __init mpi_init(void)
{
	int idx;
	unsigned long value;

	for (idx = 0; idx < MPI_NUMBER_OF_CONSTANTS; idx++) {
		switch (idx) {
		case MPI_C_ZERO:
			value = 0;
			break;
		case MPI_C_ONE:
			value = 1;
			break;
		case MPI_C_TWO:
			value = 2;
			break;
		case MPI_C_THREE:
			value = 3;
			break;
		case MPI_C_FOUR:
			value = 4;
			break;
		case MPI_C_EIGHT:
			value = 8;
			break;
		default:
			pr_err("MPI: invalid mpi_const selector %d\n", idx);
			return -EFAULT;
		}
		constants[idx] = mpi_alloc_set_ui(value);
		constants[idx]->flags = (16|32);
	}

	return 0;
}
postcore_initcall(mpi_init);

/* Return a constant MPI descripbed by NO which is one of the
 * MPI_C_xxx macros.  There is no need to copy this returned value; it
 * may be used directly.
 */
MPI mpi_const(enum gcry_mpi_constants no)
{
	if ((int)no < 0 || no > MPI_NUMBER_OF_CONSTANTS)
		pr_err("MPI: invalid mpi_const selector %d\n", no);
	if (!constants[no])
		pr_err("MPI: MPI subsystem not initialized\n");
	return constants[no];
}
EXPORT_SYMBOL_GPL(mpi_const);

/****************
 * Note:  It was a bad idea to use the number of limbs to allocate
 *	  because on a alpha the limbs are large but we normally need
 *	  integers of n bits - So we should chnage this to bits (or bytes).
 *
 *	  But mpi_alloc is used in a lot of places :-)
 */
MPI mpi_alloc(unsigned nlimbs)
{
	MPI a;

	a = kmalloc(sizeof *a, GFP_KERNEL);
	if (!a)
		return a;

	if (nlimbs) {
		a->d = mpi_alloc_limb_space(nlimbs);
		if (!a->d) {
			kfree(a);
			return NULL;
		}
	} else {
		a->d = NULL;
	}

	a->alloced = nlimbs;
	a->nlimbs = 0;
	a->sign = 0;
	a->flags = 0;
	a->nbits = 0;
	return a;
}
EXPORT_SYMBOL_GPL(mpi_alloc);

mpi_ptr_t mpi_alloc_limb_space(unsigned nlimbs)
{
	size_t len = nlimbs * sizeof(mpi_limb_t);

	if (!len)
		return NULL;

	return kmalloc(len, GFP_KERNEL);
}

void mpi_free_limb_space(mpi_ptr_t a)
{
	if (!a)
		return;

	kfree_sensitive(a);
}

void mpi_assign_limb_space(MPI a, mpi_ptr_t ap, unsigned nlimbs)
{
	mpi_free_limb_space(a->d);
	a->d = ap;
	a->alloced = nlimbs;
}

/****************
 * Resize the array of A to NLIMBS. the additional space is cleared
 * (set to 0) [done by m_realloc()]
 */
int mpi_resize(MPI a, unsigned nlimbs)
{
	void *p;

	if (nlimbs <= a->alloced)
		return 0;	/* no need to do it */

	if (a->d) {
		p = kmalloc_array(nlimbs, sizeof(mpi_limb_t), GFP_KERNEL);
		if (!p)
			return -ENOMEM;
		memcpy(p, a->d, a->alloced * sizeof(mpi_limb_t));
		kfree_sensitive(a->d);
		a->d = p;
	} else {
		a->d = kcalloc(nlimbs, sizeof(mpi_limb_t), GFP_KERNEL);
		if (!a->d)
			return -ENOMEM;
	}
	a->alloced = nlimbs;
	return 0;
}

void mpi_clear(MPI a)
{
	if (!a)
		return;
	a->nlimbs = 0;
	a->flags = 0;
}
EXPORT_SYMBOL_GPL(mpi_clear);

void mpi_free(MPI a)
{
	if (!a)
		return;

	if (a->flags & 4)
		kfree_sensitive(a->d);
	else
		mpi_free_limb_space(a->d);

	if (a->flags & ~7)
		pr_info("invalid flag value in mpi\n");
	kfree(a);
}
EXPORT_SYMBOL_GPL(mpi_free);

/****************
 * Note: This copy function should not interpret the MPI
 *	 but copy it transparently.
 */
MPI mpi_copy(MPI a)
{
	int i;
	MPI b;

	if (a) {
		b = mpi_alloc(a->nlimbs);
		b->nlimbs = a->nlimbs;
		b->sign = a->sign;
		b->flags = a->flags;
		b->flags &= ~(16|32); /* Reset the immutable and constant flags. */
		for (i = 0; i < b->nlimbs; i++)
			b->d[i] = a->d[i];
	} else
		b = NULL;
	return b;
}

/****************
 * This function allocates an MPI which is optimized to hold
 * a value as large as the one given in the argument and allocates it
 * with the same flags as A.
 */
MPI mpi_alloc_like(MPI a)
{
	MPI b;

	if (a) {
		b = mpi_alloc(a->nlimbs);
		b->nlimbs = 0;
		b->sign = 0;
		b->flags = a->flags;
	} else
		b = NULL;

	return b;
}


/* Set U into W and release U.  If W is NULL only U will be released. */
void mpi_snatch(MPI w, MPI u)
{
	if (w) {
		mpi_assign_limb_space(w, u->d, u->alloced);
		w->nlimbs = u->nlimbs;
		w->sign   = u->sign;
		w->flags  = u->flags;
		u->alloced = 0;
		u->nlimbs = 0;
		u->d = NULL;
	}
	mpi_free(u);
}


MPI mpi_set(MPI w, MPI u)
{
	mpi_ptr_t wp, up;
	mpi_size_t usize = u->nlimbs;
	int usign = u->sign;

	if (!w)
		w = mpi_alloc(mpi_get_nlimbs(u));
	RESIZE_IF_NEEDED(w, usize);
	wp = w->d;
	up = u->d;
	MPN_COPY(wp, up, usize);
	w->nlimbs = usize;
	w->flags = u->flags;
	w->flags &= ~(16|32); /* Reset the immutable and constant flags.  */
	w->sign = usign;
	return w;
}
EXPORT_SYMBOL_GPL(mpi_set);

MPI mpi_set_ui(MPI w, unsigned long u)
{
	if (!w)
		w = mpi_alloc(1);
	/* FIXME: If U is 0 we have no need to resize and thus possible
	 * allocating the the limbs.
	 */
	RESIZE_IF_NEEDED(w, 1);
	w->d[0] = u;
	w->nlimbs = u ? 1 : 0;
	w->sign = 0;
	w->flags = 0;
	return w;
}
EXPORT_SYMBOL_GPL(mpi_set_ui);

MPI mpi_alloc_set_ui(unsigned long u)
{
	MPI w = mpi_alloc(1);
	w->d[0] = u;
	w->nlimbs = u ? 1 : 0;
	w->sign = 0;
	return w;
}

/****************
 * Swap the value of A and B, when SWAP is 1.
 * Leave the value when SWAP is 0.
 * This implementation should be constant-time regardless of SWAP.
 */
void mpi_swap_cond(MPI a, MPI b, unsigned long swap)
{
	mpi_size_t i;
	mpi_size_t nlimbs;
	mpi_limb_t mask = ((mpi_limb_t)0) - swap;
	mpi_limb_t x;

	if (a->alloced > b->alloced)
		nlimbs = b->alloced;
	else
		nlimbs = a->alloced;
	if (a->nlimbs > nlimbs || b->nlimbs > nlimbs)
		return;

	for (i = 0; i < nlimbs; i++) {
		x = mask & (a->d[i] ^ b->d[i]);
		a->d[i] = a->d[i] ^ x;
		b->d[i] = b->d[i] ^ x;
	}

	x = mask & (a->nlimbs ^ b->nlimbs);
	a->nlimbs = a->nlimbs ^ x;
	b->nlimbs = b->nlimbs ^ x;

	x = mask & (a->sign ^ b->sign);
	a->sign = a->sign ^ x;
	b->sign = b->sign ^ x;
}

MODULE_DESCRIPTION("Multiprecision maths library");
MODULE_LICENSE("GPL");
