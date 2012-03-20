/* mpi-internal.h  -  Internal to the Multi Precision Integers
 *	Copyright (C) 1994, 1996 Free Software Foundation, Inc.
 *	Copyright (C) 1998, 2000 Free Software Foundation, Inc.
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
 *
 * Note: This code is heavily based on the GNU MP Library.
 *	 Actually it's the same code with only minor changes in the
 *	 way the data is stored; this is to support the abstraction
 *	 of an optional secure memory allocation which may be used
 *	 to avoid revealing of sensitive data due to paging etc.
 *	 The GNU MP Library itself is published under the LGPL;
 *	 however I decided to publish this code under the plain GPL.
 */

#ifndef G10_MPI_INTERNAL_H
#define G10_MPI_INTERNAL_H

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/mpi.h>
#include <linux/errno.h>

#define log_debug printk
#define log_bug printk

#define assert(x) \
	do { \
		if (!x) \
			log_bug("failed assertion\n"); \
	} while (0);

/* If KARATSUBA_THRESHOLD is not already defined, define it to a
 * value which is good on most machines.  */

/* tested 4, 16, 32 and 64, where 16 gave the best performance when
 * checking a 768 and a 1024 bit ElGamal signature.
 * (wk 22.12.97) */
#ifndef KARATSUBA_THRESHOLD
#define KARATSUBA_THRESHOLD 16
#endif

/* The code can't handle KARATSUBA_THRESHOLD smaller than 2.  */
#if KARATSUBA_THRESHOLD < 2
#undef KARATSUBA_THRESHOLD
#define KARATSUBA_THRESHOLD 2
#endif

typedef mpi_limb_t *mpi_ptr_t;	/* pointer to a limb */
typedef int mpi_size_t;		/* (must be a signed type) */

#define ABS(x) (x >= 0 ? x : -x)
#define MIN(l, o) ((l) < (o) ? (l) : (o))
#define MAX(h, i) ((h) > (i) ? (h) : (i))

static inline int RESIZE_IF_NEEDED(MPI a, unsigned b)
{
	if (a->alloced < b)
		return mpi_resize(a, b);
	return 0;
}

/* Copy N limbs from S to D.  */
#define MPN_COPY(d, s, n) \
	do {					\
		mpi_size_t _i;			\
		for (_i = 0; _i < (n); _i++)	\
			(d)[_i] = (s)[_i];	\
	} while (0)

#define MPN_COPY_INCR(d, s, n) \
	do {					\
		mpi_size_t _i;			\
		for (_i = 0; _i < (n); _i++)	\
			(d)[_i] = (d)[_i];	\
	} while (0)

#define MPN_COPY_DECR(d, s, n) \
	do {					\
		mpi_size_t _i;			\
		for (_i = (n)-1; _i >= 0; _i--) \
			(d)[_i] = (s)[_i];	\
	} while (0)

/* Zero N limbs at D */
#define MPN_ZERO(d, n) \
	do {					\
		int  _i;			\
		for (_i = 0; _i < (n); _i++)	\
			(d)[_i] = 0;		\
	} while (0)

#define MPN_NORMALIZE(d, n)  \
	do {					\
		while ((n) > 0) {		\
			if ((d)[(n)-1])		\
				break;		\
			(n)--;			\
		}				\
	} while (0)

#define MPN_NORMALIZE_NOT_ZERO(d, n) \
	do {				\
		for (;;) {		\
			if ((d)[(n)-1])	\
				break;	\
			(n)--;		\
		}			\
	} while (0)

#define MPN_MUL_N_RECURSE(prodp, up, vp, size, tspace) \
	do {							\
		if ((size) < KARATSUBA_THRESHOLD)		\
			mul_n_basecase(prodp, up, vp, size);	\
		else						\
			mul_n(prodp, up, vp, size, tspace);	\
	} while (0);

/* Divide the two-limb number in (NH,,NL) by D, with DI being the largest
 * limb not larger than (2**(2*BITS_PER_MP_LIMB))/D - (2**BITS_PER_MP_LIMB).
 * If this would yield overflow, DI should be the largest possible number
 * (i.e., only ones).  For correct operation, the most significant bit of D
 * has to be set.  Put the quotient in Q and the remainder in R.
 */
#define UDIV_QRNND_PREINV(q, r, nh, nl, d, di) \
	do {								\
		mpi_limb_t _q, _ql, _r;					\
		mpi_limb_t _xh, _xl;					\
		umul_ppmm(_q, _ql, (nh), (di));				\
		_q += (nh);	/* DI is 2**BITS_PER_MPI_LIMB too small */ \
		umul_ppmm(_xh, _xl, _q, (d));				\
		sub_ddmmss(_xh, _r, (nh), (nl), _xh, _xl);		\
		if (_xh) {						\
			sub_ddmmss(_xh, _r, _xh, _r, 0, (d));		\
			_q++;						\
			if (_xh) {					\
				sub_ddmmss(_xh, _r, _xh, _r, 0, (d));	\
				_q++;					\
			}						\
		}							\
		if (_r >= (d)) {					\
			_r -= (d);					\
			_q++;						\
		}							\
		(r) = _r;						\
		(q) = _q;						\
	} while (0)

/*-- mpiutil.c --*/
mpi_ptr_t mpi_alloc_limb_space(unsigned nlimbs);
void mpi_free_limb_space(mpi_ptr_t a);
void mpi_assign_limb_space(MPI a, mpi_ptr_t ap, unsigned nlimbs);

/*-- mpi-bit.c --*/
void mpi_rshift_limbs(MPI a, unsigned int count);
int mpi_lshift_limbs(MPI a, unsigned int count);

/*-- mpihelp-add.c --*/
mpi_limb_t mpihelp_add_1(mpi_ptr_t res_ptr, mpi_ptr_t s1_ptr,
			 mpi_size_t s1_size, mpi_limb_t s2_limb);
mpi_limb_t mpihelp_add_n(mpi_ptr_t res_ptr, mpi_ptr_t s1_ptr,
			 mpi_ptr_t s2_ptr, mpi_size_t size);
mpi_limb_t mpihelp_add(mpi_ptr_t res_ptr, mpi_ptr_t s1_ptr, mpi_size_t s1_size,
		       mpi_ptr_t s2_ptr, mpi_size_t s2_size);

/*-- mpihelp-sub.c --*/
mpi_limb_t mpihelp_sub_1(mpi_ptr_t res_ptr, mpi_ptr_t s1_ptr,
			 mpi_size_t s1_size, mpi_limb_t s2_limb);
mpi_limb_t mpihelp_sub_n(mpi_ptr_t res_ptr, mpi_ptr_t s1_ptr,
			 mpi_ptr_t s2_ptr, mpi_size_t size);
mpi_limb_t mpihelp_sub(mpi_ptr_t res_ptr, mpi_ptr_t s1_ptr, mpi_size_t s1_size,
		       mpi_ptr_t s2_ptr, mpi_size_t s2_size);

/*-- mpihelp-cmp.c --*/
int mpihelp_cmp(mpi_ptr_t op1_ptr, mpi_ptr_t op2_ptr, mpi_size_t size);

/*-- mpihelp-mul.c --*/

struct karatsuba_ctx {
	struct karatsuba_ctx *next;
	mpi_ptr_t tspace;
	mpi_size_t tspace_size;
	mpi_ptr_t tp;
	mpi_size_t tp_size;
};

void mpihelp_release_karatsuba_ctx(struct karatsuba_ctx *ctx);

mpi_limb_t mpihelp_addmul_1(mpi_ptr_t res_ptr, mpi_ptr_t s1_ptr,
			    mpi_size_t s1_size, mpi_limb_t s2_limb);
mpi_limb_t mpihelp_submul_1(mpi_ptr_t res_ptr, mpi_ptr_t s1_ptr,
			    mpi_size_t s1_size, mpi_limb_t s2_limb);
int mpihelp_mul_n(mpi_ptr_t prodp, mpi_ptr_t up, mpi_ptr_t vp, mpi_size_t size);
int mpihelp_mul(mpi_ptr_t prodp, mpi_ptr_t up, mpi_size_t usize,
		mpi_ptr_t vp, mpi_size_t vsize, mpi_limb_t *_result);
void mpih_sqr_n_basecase(mpi_ptr_t prodp, mpi_ptr_t up, mpi_size_t size);
void mpih_sqr_n(mpi_ptr_t prodp, mpi_ptr_t up, mpi_size_t size,
		mpi_ptr_t tspace);

int mpihelp_mul_karatsuba_case(mpi_ptr_t prodp,
			       mpi_ptr_t up, mpi_size_t usize,
			       mpi_ptr_t vp, mpi_size_t vsize,
			       struct karatsuba_ctx *ctx);

/*-- mpihelp-mul_1.c (or xxx/cpu/ *.S) --*/
mpi_limb_t mpihelp_mul_1(mpi_ptr_t res_ptr, mpi_ptr_t s1_ptr,
			 mpi_size_t s1_size, mpi_limb_t s2_limb);

/*-- mpihelp-div.c --*/
mpi_limb_t mpihelp_mod_1(mpi_ptr_t dividend_ptr, mpi_size_t dividend_size,
			 mpi_limb_t divisor_limb);
mpi_limb_t mpihelp_divrem(mpi_ptr_t qp, mpi_size_t qextra_limbs,
			  mpi_ptr_t np, mpi_size_t nsize,
			  mpi_ptr_t dp, mpi_size_t dsize);
mpi_limb_t mpihelp_divmod_1(mpi_ptr_t quot_ptr,
			    mpi_ptr_t dividend_ptr, mpi_size_t dividend_size,
			    mpi_limb_t divisor_limb);

/*-- mpihelp-shift.c --*/
mpi_limb_t mpihelp_lshift(mpi_ptr_t wp, mpi_ptr_t up, mpi_size_t usize,
			  unsigned cnt);
mpi_limb_t mpihelp_rshift(mpi_ptr_t wp, mpi_ptr_t up, mpi_size_t usize,
			  unsigned cnt);

/* Define stuff for longlong.h.  */
#define W_TYPE_SIZE BITS_PER_MPI_LIMB
typedef mpi_limb_t UWtype;
typedef unsigned int UHWtype;
#if defined(__GNUC__)
typedef unsigned int UQItype __attribute__ ((mode(QI)));
typedef int SItype __attribute__ ((mode(SI)));
typedef unsigned int USItype __attribute__ ((mode(SI)));
typedef int DItype __attribute__ ((mode(DI)));
typedef unsigned int UDItype __attribute__ ((mode(DI)));
#else
typedef unsigned char UQItype;
typedef long SItype;
typedef unsigned long USItype;
#endif

#ifdef __GNUC__
#include "mpi-inline.h"
#endif

#endif /*G10_MPI_INTERNAL_H */
