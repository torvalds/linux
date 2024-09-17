/* SPDX-License-Identifier: GPL-2.0-or-later */
/* mpi.h  -  Multi Precision Integers
 *	Copyright (C) 1994, 1996, 1998, 1999,
 *                    2000, 2001 Free Software Foundation, Inc.
 *
 * This file is part of GNUPG.
 *
 * Note: This code is heavily based on the GNU MP Library.
 *	 Actually it's the same code with only minor changes in the
 *	 way the data is stored; this is to support the abstraction
 *	 of an optional secure memory allocation which may be used
 *	 to avoid revealing of sensitive data due to paging etc.
 *	 The GNU MP Library itself is published under the LGPL;
 *	 however I decided to publish this code under the plain GPL.
 */

#ifndef G10_MPI_H
#define G10_MPI_H

#include <linux/types.h>
#include <linux/scatterlist.h>

#define BYTES_PER_MPI_LIMB	(BITS_PER_LONG / 8)
#define BITS_PER_MPI_LIMB	BITS_PER_LONG

typedef unsigned long int mpi_limb_t;
typedef signed long int mpi_limb_signed_t;

struct gcry_mpi {
	int alloced;		/* array size (# of allocated limbs) */
	int nlimbs;		/* number of valid limbs */
	int nbits;		/* the real number of valid bits (info only) */
	int sign;		/* indicates a negative number */
	unsigned flags;		/* bit 0: array must be allocated in secure memory space */
	/* bit 1: not used */
	/* bit 2: the limb is a pointer to some m_alloced data */
	mpi_limb_t *d;		/* array with the limbs */
};

typedef struct gcry_mpi *MPI;

#define mpi_get_nlimbs(a)     ((a)->nlimbs)

/*-- mpiutil.c --*/
MPI mpi_alloc(unsigned nlimbs);
void mpi_free(MPI a);
int mpi_resize(MPI a, unsigned nlimbs);

MPI mpi_copy(MPI a);

/*-- mpicoder.c --*/
MPI mpi_read_raw_data(const void *xbuffer, size_t nbytes);
MPI mpi_read_from_buffer(const void *buffer, unsigned *ret_nread);
MPI mpi_read_raw_from_sgl(struct scatterlist *sgl, unsigned int len);
void *mpi_get_buffer(MPI a, unsigned *nbytes, int *sign);
int mpi_read_buffer(MPI a, uint8_t *buf, unsigned buf_len, unsigned *nbytes,
		    int *sign);
int mpi_write_to_sgl(MPI a, struct scatterlist *sg, unsigned nbytes,
		     int *sign);

/*-- mpi-mod.c --*/
int mpi_mod(MPI rem, MPI dividend, MPI divisor);

/*-- mpi-pow.c --*/
int mpi_powm(MPI res, MPI base, MPI exp, MPI mod);

/*-- mpi-cmp.c --*/
int mpi_cmp_ui(MPI u, ulong v);
int mpi_cmp(MPI u, MPI v);

/*-- mpi-sub-ui.c --*/
int mpi_sub_ui(MPI w, MPI u, unsigned long vval);

/*-- mpi-bit.c --*/
void mpi_normalize(MPI a);
unsigned mpi_get_nbits(MPI a);
int mpi_test_bit(MPI a, unsigned int n);
int mpi_set_bit(MPI a, unsigned int n);
int mpi_rshift(MPI x, MPI a, unsigned int n);

/*-- mpi-add.c --*/
int mpi_add(MPI w, MPI u, MPI v);
int mpi_sub(MPI w, MPI u, MPI v);
int mpi_addm(MPI w, MPI u, MPI v, MPI m);
int mpi_subm(MPI w, MPI u, MPI v, MPI m);

/*-- mpi-mul.c --*/
int mpi_mul(MPI w, MPI u, MPI v);
int mpi_mulm(MPI w, MPI u, MPI v, MPI m);

/*-- mpi-div.c --*/
int mpi_tdiv_r(MPI rem, MPI num, MPI den);
int mpi_fdiv_r(MPI rem, MPI dividend, MPI divisor);

/* inline functions */

/**
 * mpi_get_size() - returns max size required to store the number
 *
 * @a:	A multi precision integer for which we want to allocate a buffer
 *
 * Return: size required to store the number
 */
static inline unsigned int mpi_get_size(MPI a)
{
	return a->nlimbs * BYTES_PER_MPI_LIMB;
}
#endif /*G10_MPI_H */
