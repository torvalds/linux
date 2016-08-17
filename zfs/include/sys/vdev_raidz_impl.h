/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (C) 2016 Gvozden Nešković. All rights reserved.
 */

#ifndef _VDEV_RAIDZ_H
#define	_VDEV_RAIDZ_H

#include <sys/types.h>
#include <sys/debug.h>
#include <sys/kstat.h>
#include <sys/abd.h>

#ifdef  __cplusplus
extern "C" {
#endif

#define	CODE_P		(0U)
#define	CODE_Q		(1U)
#define	CODE_R		(2U)

#define	PARITY_P	(1U)
#define	PARITY_PQ	(2U)
#define	PARITY_PQR	(3U)

#define	TARGET_X	(0U)
#define	TARGET_Y	(1U)
#define	TARGET_Z	(2U)

/*
 * Parity generation methods indexes
 */
enum raidz_math_gen_op {
	RAIDZ_GEN_P = 0,
	RAIDZ_GEN_PQ,
	RAIDZ_GEN_PQR,
	RAIDZ_GEN_NUM = 3
};
/*
 * Data reconstruction methods indexes
 */
enum raidz_rec_op {
	RAIDZ_REC_P = 0,
	RAIDZ_REC_Q,
	RAIDZ_REC_R,
	RAIDZ_REC_PQ,
	RAIDZ_REC_PR,
	RAIDZ_REC_QR,
	RAIDZ_REC_PQR,
	RAIDZ_REC_NUM = 7
};

extern const char *raidz_gen_name[RAIDZ_GEN_NUM];
extern const char *raidz_rec_name[RAIDZ_REC_NUM];

/*
 * Methods used to define raidz implementation
 *
 * @raidz_gen_f	Parity generation function
 *     @par1	pointer to raidz_map
 * @raidz_rec_f	Data reconstruction function
 *     @par1	pointer to raidz_map
 *     @par2	array of reconstruction targets
 * @will_work_f Function returns TRUE if impl. is supported on the system
 * @init_impl_f Function is called once on init
 * @fini_impl_f Function is called once on fini
 */
typedef void		(*raidz_gen_f)(void *);
typedef int		(*raidz_rec_f)(void *, const int *);
typedef boolean_t	(*will_work_f)(void);
typedef void		(*init_impl_f)(void);
typedef void		(*fini_impl_f)(void);

#define	RAIDZ_IMPL_NAME_MAX	(16)

typedef struct raidz_impl_ops {
	init_impl_f init;
	fini_impl_f fini;
	raidz_gen_f gen[RAIDZ_GEN_NUM];	/* Parity generate functions */
	raidz_rec_f rec[RAIDZ_REC_NUM];	/* Data reconstruction functions */
	will_work_f is_supported;	/* Support check function */
	char name[RAIDZ_IMPL_NAME_MAX];	/* Name of the implementation */
} raidz_impl_ops_t;

typedef struct raidz_col {
	uint64_t rc_devidx;		/* child device index for I/O */
	uint64_t rc_offset;		/* device offset */
	uint64_t rc_size;		/* I/O size */
	abd_t *rc_abd;			/* I/O data */
	void *rc_gdata;			/* used to store the "good" version */
	int rc_error;			/* I/O error for this device */
	uint8_t rc_tried;		/* Did we attempt this I/O column? */
	uint8_t rc_skipped;		/* Did we skip this I/O column? */
} raidz_col_t;

typedef struct raidz_map {
	uint64_t rm_cols;		/* Regular column count */
	uint64_t rm_scols;		/* Count including skipped columns */
	uint64_t rm_bigcols;		/* Number of oversized columns */
	uint64_t rm_asize;		/* Actual total I/O size */
	uint64_t rm_missingdata;	/* Count of missing data devices */
	uint64_t rm_missingparity;	/* Count of missing parity devices */
	uint64_t rm_firstdatacol;	/* First data column/parity count */
	uint64_t rm_nskip;		/* Skipped sectors for padding */
	uint64_t rm_skipstart;		/* Column index of padding start */
	abd_t *rm_abd_copy;		/* rm_asize-buffer of copied data */
	uintptr_t rm_reports;		/* # of referencing checksum reports */
	uint8_t	rm_freed;		/* map no longer has referencing ZIO */
	uint8_t	rm_ecksuminjected;	/* checksum error was injected */
	raidz_impl_ops_t *rm_ops;	/* RAIDZ math operations */
	raidz_col_t rm_col[1];		/* Flexible array of I/O columns */
} raidz_map_t;

#define	RAIDZ_ORIGINAL_IMPL	(INT_MAX)

extern const raidz_impl_ops_t vdev_raidz_scalar_impl;
#if defined(__x86_64) && defined(HAVE_SSE2)	/* only x86_64 for now */
extern const raidz_impl_ops_t vdev_raidz_sse2_impl;
#endif
#if defined(__x86_64) && defined(HAVE_SSSE3)	/* only x86_64 for now */
extern const raidz_impl_ops_t vdev_raidz_ssse3_impl;
#endif
#if defined(__x86_64) && defined(HAVE_AVX2)	/* only x86_64 for now */
extern const raidz_impl_ops_t vdev_raidz_avx2_impl;
#endif
#if defined(__x86_64) && defined(HAVE_AVX512F)	/* only x86_64 for now */
extern const raidz_impl_ops_t vdev_raidz_avx512f_impl;
#endif
#if defined(__x86_64) && defined(HAVE_AVX512BW)	/* only x86_64 for now */
extern const raidz_impl_ops_t vdev_raidz_avx512bw_impl;
#endif
#if defined(__aarch64__)
extern const raidz_impl_ops_t vdev_raidz_aarch64_neon_impl;
extern const raidz_impl_ops_t vdev_raidz_aarch64_neonx2_impl;
#endif

/*
 * Commonly used raidz_map helpers
 *
 * raidz_parity		Returns parity of the RAIDZ block
 * raidz_ncols		Returns number of columns the block spans
 * raidz_nbigcols	Returns number of big columns columns
 * raidz_col_p		Returns pointer to a column
 * raidz_col_size	Returns size of a column
 * raidz_big_size	Returns size of big columns
 * raidz_short_size	Returns size of short columns
 */
#define	raidz_parity(rm)	((rm)->rm_firstdatacol)
#define	raidz_ncols(rm)		((rm)->rm_cols)
#define	raidz_nbigcols(rm)	((rm)->rm_bigcols)
#define	raidz_col_p(rm, c)	((rm)->rm_col + (c))
#define	raidz_col_size(rm, c)	((rm)->rm_col[c].rc_size)
#define	raidz_big_size(rm)	(raidz_col_size(rm, CODE_P))
#define	raidz_short_size(rm)	(raidz_col_size(rm, raidz_ncols(rm)-1))

/*
 * Macro defines an RAIDZ parity generation method
 *
 * @code	parity the function produce
 * @impl	name of the implementation
 */
#define	_RAIDZ_GEN_WRAP(code, impl)					\
static void								\
impl ## _gen_ ## code(void *rmp)					\
{									\
	raidz_map_t *rm = (raidz_map_t *)rmp;				\
	raidz_generate_## code ## _impl(rm);				\
}

/*
 * Macro defines an RAIDZ data reconstruction method
 *
 * @code	parity the function produce
 * @impl	name of the implementation
 */
#define	_RAIDZ_REC_WRAP(code, impl)					\
static int								\
impl ## _rec_ ## code(void *rmp, const int *tgtidx)			\
{									\
	raidz_map_t *rm = (raidz_map_t *)rmp;				\
	return (raidz_reconstruct_## code ## _impl(rm, tgtidx));	\
}

/*
 * Define all gen methods for an implementation
 *
 * @impl	name of the implementation
 */
#define	DEFINE_GEN_METHODS(impl)					\
	_RAIDZ_GEN_WRAP(p, impl);					\
	_RAIDZ_GEN_WRAP(pq, impl);					\
	_RAIDZ_GEN_WRAP(pqr, impl)

/*
 * Define all rec functions for an implementation
 *
 * @impl	name of the implementation
 */
#define	DEFINE_REC_METHODS(impl)					\
	_RAIDZ_REC_WRAP(p, impl);					\
	_RAIDZ_REC_WRAP(q, impl);					\
	_RAIDZ_REC_WRAP(r, impl);					\
	_RAIDZ_REC_WRAP(pq, impl);					\
	_RAIDZ_REC_WRAP(pr, impl);					\
	_RAIDZ_REC_WRAP(qr, impl);					\
	_RAIDZ_REC_WRAP(pqr, impl)

#define	RAIDZ_GEN_METHODS(impl)						\
{									\
	[RAIDZ_GEN_P] = & impl ## _gen_p,				\
	[RAIDZ_GEN_PQ] = & impl ## _gen_pq,				\
	[RAIDZ_GEN_PQR] = & impl ## _gen_pqr				\
}

#define	RAIDZ_REC_METHODS(impl)						\
{									\
	[RAIDZ_REC_P] = & impl ## _rec_p,				\
	[RAIDZ_REC_Q] = & impl ## _rec_q,				\
	[RAIDZ_REC_R] = & impl ## _rec_r,				\
	[RAIDZ_REC_PQ] = & impl ## _rec_pq,				\
	[RAIDZ_REC_PR] = & impl ## _rec_pr,				\
	[RAIDZ_REC_QR] = & impl ## _rec_qr,				\
	[RAIDZ_REC_PQR] = & impl ## _rec_pqr				\
}


typedef struct raidz_impl_kstat {
	uint64_t gen[RAIDZ_GEN_NUM];	/* gen method speed B/s */
	uint64_t rec[RAIDZ_REC_NUM];	/* rec method speed B/s */
} raidz_impl_kstat_t;

/*
 * Enumerate various multiplication constants
 * used in reconstruction methods
 */
typedef enum raidz_mul_info {
	/* Reconstruct Q */
	MUL_Q_X		= 0,
	/* Reconstruct R */
	MUL_R_X		= 0,
	/* Reconstruct PQ */
	MUL_PQ_X	= 0,
	MUL_PQ_Y	= 1,
	/* Reconstruct PR */
	MUL_PR_X	= 0,
	MUL_PR_Y	= 1,
	/* Reconstruct QR */
	MUL_QR_XQ	= 0,
	MUL_QR_X	= 1,
	MUL_QR_YQ	= 2,
	MUL_QR_Y	= 3,
	/* Reconstruct PQR */
	MUL_PQR_XP	= 0,
	MUL_PQR_XQ	= 1,
	MUL_PQR_XR	= 2,
	MUL_PQR_YU	= 3,
	MUL_PQR_YP	= 4,
	MUL_PQR_YQ	= 5,

	MUL_CNT		= 6
} raidz_mul_info_t;

/*
 * Powers of 2 in the Galois field.
 */
extern const uint8_t vdev_raidz_pow2[256] __attribute__((aligned(256)));
/* Logs of 2 in the Galois field defined above. */
extern const uint8_t vdev_raidz_log2[256] __attribute__((aligned(256)));

/*
 * Multiply a given number by 2 raised to the given power.
 */
static inline uint8_t
vdev_raidz_exp2(const uint8_t a, const unsigned exp)
{
	if (a == 0)
		return (0);

	return (vdev_raidz_pow2[(exp + (unsigned)vdev_raidz_log2[a]) % 255]);
}

/*
 * Galois Field operations.
 *
 * gf_exp2	- computes 2 raised to the given power
 * gf_exp2	- computes 4 raised to the given power
 * gf_mul	- multiplication
 * gf_div	- division
 * gf_inv	- multiplicative inverse
 */
typedef unsigned gf_t;
typedef unsigned gf_log_t;

static inline gf_t
gf_mul(const gf_t a, const gf_t b)
{
	gf_log_t logsum;

	if (a == 0 || b == 0)
		return (0);

	logsum = (gf_log_t)vdev_raidz_log2[a] + (gf_log_t)vdev_raidz_log2[b];

	return ((gf_t)vdev_raidz_pow2[logsum % 255]);
}

static inline gf_t
gf_div(const gf_t  a, const gf_t b)
{
	gf_log_t logsum;

	ASSERT3U(b, >, 0);
	if (a == 0)
		return (0);

	logsum = (gf_log_t)255 + (gf_log_t)vdev_raidz_log2[a] -
	    (gf_log_t)vdev_raidz_log2[b];

	return ((gf_t)vdev_raidz_pow2[logsum % 255]);
}

static inline gf_t
gf_inv(const gf_t a)
{
	gf_log_t logsum;

	ASSERT3U(a, >, 0);

	logsum = (gf_log_t)255 - (gf_log_t)vdev_raidz_log2[a];

	return ((gf_t)vdev_raidz_pow2[logsum]);
}

static inline gf_t
gf_exp2(gf_log_t exp)
{
	return (vdev_raidz_pow2[exp % 255]);
}

static inline gf_t
gf_exp4(gf_log_t exp)
{
	ASSERT3U(exp, <=, 255);
	return ((gf_t)vdev_raidz_pow2[(2 * exp) % 255]);
}

#ifdef  __cplusplus
}
#endif

#endif /* _VDEV_RAIDZ_H */
