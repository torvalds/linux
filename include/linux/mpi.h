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
#define mpi_has_sign(a)       ((a)->sign)

/*-- mpiutil.c --*/
MPI mpi_alloc(unsigned nlimbs);
void mpi_clear(MPI a);
void mpi_free(MPI a);
int mpi_resize(MPI a, unsigned nlimbs);

static inline MPI mpi_new(unsigned int nbits)
{
	return mpi_alloc((nbits + BITS_PER_MPI_LIMB - 1) / BITS_PER_MPI_LIMB);
}

MPI mpi_copy(MPI a);
MPI mpi_alloc_like(MPI a);
void mpi_snatch(MPI w, MPI u);
MPI mpi_set(MPI w, MPI u);
MPI mpi_set_ui(MPI w, unsigned long u);
MPI mpi_alloc_set_ui(unsigned long u);
void mpi_swap_cond(MPI a, MPI b, unsigned long swap);

/* Constants used to return constant MPIs.  See mpi_init if you
 * want to add more constants.
 */
#define MPI_NUMBER_OF_CONSTANTS 6
enum gcry_mpi_constants {
	MPI_C_ZERO,
	MPI_C_ONE,
	MPI_C_TWO,
	MPI_C_THREE,
	MPI_C_FOUR,
	MPI_C_EIGHT
};

MPI mpi_const(enum gcry_mpi_constants no);

/*-- mpicoder.c --*/

/* Different formats of external big integer representation. */
enum gcry_mpi_format {
	GCRYMPI_FMT_NONE = 0,
	GCRYMPI_FMT_STD = 1,    /* Twos complement stored without length. */
	GCRYMPI_FMT_PGP = 2,    /* As used by OpenPGP (unsigned only). */
	GCRYMPI_FMT_SSH = 3,    /* As used by SSH (like STD but with length). */
	GCRYMPI_FMT_HEX = 4,    /* Hex format. */
	GCRYMPI_FMT_USG = 5,    /* Like STD but unsigned. */
	GCRYMPI_FMT_OPAQUE = 8  /* Opaque format (some functions only). */
};

MPI mpi_read_raw_data(const void *xbuffer, size_t nbytes);
MPI mpi_read_from_buffer(const void *buffer, unsigned *ret_nread);
int mpi_fromstr(MPI val, const char *str);
MPI mpi_scanval(const char *string);
MPI mpi_read_raw_from_sgl(struct scatterlist *sgl, unsigned int len);
void *mpi_get_buffer(MPI a, unsigned *nbytes, int *sign);
int mpi_read_buffer(MPI a, uint8_t *buf, unsigned buf_len, unsigned *nbytes,
		    int *sign);
int mpi_write_to_sgl(MPI a, struct scatterlist *sg, unsigned nbytes,
		     int *sign);
int mpi_print(enum gcry_mpi_format format, unsigned char *buffer,
			size_t buflen, size_t *nwritten, MPI a);

/*-- mpi-mod.c --*/
void mpi_mod(MPI rem, MPI dividend, MPI divisor);

/* Context used with Barrett reduction.  */
struct barrett_ctx_s;
typedef struct barrett_ctx_s *mpi_barrett_t;

mpi_barrett_t mpi_barrett_init(MPI m, int copy);
void mpi_barrett_free(mpi_barrett_t ctx);
void mpi_mod_barrett(MPI r, MPI x, mpi_barrett_t ctx);
void mpi_mul_barrett(MPI w, MPI u, MPI v, mpi_barrett_t ctx);

/*-- mpi-pow.c --*/
int mpi_powm(MPI res, MPI base, MPI exp, MPI mod);

/*-- mpi-cmp.c --*/
int mpi_cmp_ui(MPI u, ulong v);
int mpi_cmp(MPI u, MPI v);
int mpi_cmpabs(MPI u, MPI v);

/*-- mpi-sub-ui.c --*/
int mpi_sub_ui(MPI w, MPI u, unsigned long vval);

/*-- mpi-bit.c --*/
void mpi_normalize(MPI a);
unsigned mpi_get_nbits(MPI a);
int mpi_test_bit(MPI a, unsigned int n);
void mpi_set_bit(MPI a, unsigned int n);
void mpi_set_highbit(MPI a, unsigned int n);
void mpi_clear_highbit(MPI a, unsigned int n);
void mpi_clear_bit(MPI a, unsigned int n);
void mpi_rshift_limbs(MPI a, unsigned int count);
void mpi_rshift(MPI x, MPI a, unsigned int n);
void mpi_lshift_limbs(MPI a, unsigned int count);
void mpi_lshift(MPI x, MPI a, unsigned int n);

/*-- mpi-add.c --*/
void mpi_add_ui(MPI w, MPI u, unsigned long v);
void mpi_add(MPI w, MPI u, MPI v);
void mpi_sub(MPI w, MPI u, MPI v);
void mpi_addm(MPI w, MPI u, MPI v, MPI m);
void mpi_subm(MPI w, MPI u, MPI v, MPI m);

/*-- mpi-mul.c --*/
void mpi_mul(MPI w, MPI u, MPI v);
void mpi_mulm(MPI w, MPI u, MPI v, MPI m);

/*-- mpi-div.c --*/
void mpi_tdiv_r(MPI rem, MPI num, MPI den);
void mpi_fdiv_r(MPI rem, MPI dividend, MPI divisor);
void mpi_fdiv_q(MPI quot, MPI dividend, MPI divisor);

/*-- mpi-inv.c --*/
int mpi_invm(MPI x, MPI a, MPI n);

/* inline functions */

/**
 * mpi_get_size() - returns max size required to store the number
 *
 * @a:	A multi precision integer for which we want to allocate a bufer
 *
 * Return: size required to store the number
 */
static inline unsigned int mpi_get_size(MPI a)
{
	return a->nlimbs * BYTES_PER_MPI_LIMB;
}
#endif /*G10_MPI_H */
