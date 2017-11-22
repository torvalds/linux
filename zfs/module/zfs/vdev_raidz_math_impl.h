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

#ifndef _VDEV_RAIDZ_MATH_IMPL_H
#define	_VDEV_RAIDZ_MATH_IMPL_H

#include <sys/types.h>

#define	raidz_inline inline __attribute__((always_inline))
#ifndef noinline
#define	noinline __attribute__((noinline))
#endif

/*
 * Functions calculate multiplication constants for data reconstruction.
 * Coefficients depend on RAIDZ geometry, indexes of failed child vdevs, and
 * used parity columns for reconstruction.
 * @rm			RAIDZ map
 * @tgtidx		array of missing data indexes
 * @coeff		output array of coefficients. Array must be provided by
 *         		user and must hold minimum MUL_CNT values.
 */
static noinline void
raidz_rec_q_coeff(const raidz_map_t *rm, const int *tgtidx, unsigned *coeff)
{
	const unsigned ncols = raidz_ncols(rm);
	const unsigned x = tgtidx[TARGET_X];

	coeff[MUL_Q_X] = gf_exp2(255 - (ncols - x - 1));
}

static noinline void
raidz_rec_r_coeff(const raidz_map_t *rm, const int *tgtidx, unsigned *coeff)
{
	const unsigned ncols = raidz_ncols(rm);
	const unsigned x = tgtidx[TARGET_X];

	coeff[MUL_R_X] = gf_exp4(255 - (ncols - x - 1));
}

static noinline void
raidz_rec_pq_coeff(const raidz_map_t *rm, const int *tgtidx, unsigned *coeff)
{
	const unsigned ncols = raidz_ncols(rm);
	const unsigned x = tgtidx[TARGET_X];
	const unsigned y = tgtidx[TARGET_Y];
	gf_t a, b, e;

	a = gf_exp2(x + 255 - y);
	b = gf_exp2(255 - (ncols - x - 1));
	e = a ^ 0x01;

	coeff[MUL_PQ_X] = gf_div(a, e);
	coeff[MUL_PQ_Y] = gf_div(b, e);
}

static noinline void
raidz_rec_pr_coeff(const raidz_map_t *rm, const int *tgtidx, unsigned *coeff)
{
	const unsigned ncols = raidz_ncols(rm);
	const unsigned x = tgtidx[TARGET_X];
	const unsigned y = tgtidx[TARGET_Y];

	gf_t a, b, e;

	a = gf_exp4(x + 255 - y);
	b = gf_exp4(255 - (ncols - x - 1));
	e = a ^ 0x01;

	coeff[MUL_PR_X] = gf_div(a, e);
	coeff[MUL_PR_Y] = gf_div(b, e);
}

static noinline void
raidz_rec_qr_coeff(const raidz_map_t *rm, const int *tgtidx, unsigned *coeff)
{
	const unsigned ncols = raidz_ncols(rm);
	const unsigned x = tgtidx[TARGET_X];
	const unsigned y = tgtidx[TARGET_Y];

	gf_t nx, ny, nxxy, nxyy, d;

	nx = gf_exp2(ncols - x - 1);
	ny = gf_exp2(ncols - y - 1);
	nxxy = gf_mul(gf_mul(nx, nx), ny);
	nxyy = gf_mul(gf_mul(nx, ny), ny);
	d = nxxy ^ nxyy;

	coeff[MUL_QR_XQ] = ny;
	coeff[MUL_QR_X]	= gf_div(ny, d);
	coeff[MUL_QR_YQ] = nx;
	coeff[MUL_QR_Y]	= gf_div(nx, d);
}

static noinline void
raidz_rec_pqr_coeff(const raidz_map_t *rm, const int *tgtidx, unsigned *coeff)
{
	const unsigned ncols = raidz_ncols(rm);
	const unsigned x = tgtidx[TARGET_X];
	const unsigned y = tgtidx[TARGET_Y];
	const unsigned z = tgtidx[TARGET_Z];

	gf_t nx, ny, nz, nxx, nyy, nzz, nyyz, nyzz, xd, yd;

	nx = gf_exp2(ncols - x - 1);
	ny = gf_exp2(ncols - y - 1);
	nz = gf_exp2(ncols - z - 1);

	nxx = gf_exp4(ncols - x - 1);
	nyy = gf_exp4(ncols - y - 1);
	nzz = gf_exp4(ncols - z - 1);

	nyyz = gf_mul(gf_mul(ny, nz), ny);
	nyzz = gf_mul(nzz, ny);

	xd = gf_mul(nxx, ny) ^ gf_mul(nx, nyy) ^ nyyz ^
	    gf_mul(nxx, nz) ^ gf_mul(nzz, nx) ^  nyzz;

	yd = gf_inv(ny ^ nz);

	coeff[MUL_PQR_XP] = gf_div(nyyz ^ nyzz, xd);
	coeff[MUL_PQR_XQ] = gf_div(nyy ^ nzz, xd);
	coeff[MUL_PQR_XR] = gf_div(ny ^ nz, xd);
	coeff[MUL_PQR_YU] = nx;
	coeff[MUL_PQR_YP] = gf_mul(nz, yd);
	coeff[MUL_PQR_YQ] = yd;
}

/*
 * Method for zeroing a buffer (can be implemented using SIMD).
 * This method is used by multiple for gen/rec functions.
 *
 * @dc		Destination buffer
 * @dsize	Destination buffer size
 * @private	Unused
 */
static int
raidz_zero_abd_cb(void *dc, size_t dsize, void *private)
{
	v_t *dst = (v_t *)dc;
	size_t i;

	ZERO_DEFINE();

	(void) private; /* unused */

	ZERO(ZERO_D);

	for (i = 0; i < dsize / sizeof (v_t); i += (2 * ZERO_STRIDE)) {
		STORE(dst + i, ZERO_D);
		STORE(dst + i + ZERO_STRIDE, ZERO_D);
	}

	return (0);
}

#define	raidz_zero(dabd, size)						\
{									\
	abd_iterate_func(dabd, 0, size, raidz_zero_abd_cb, NULL);	\
}

/*
 * Method for copying two buffers (can be implemented using SIMD).
 * This method is used by multiple for gen/rec functions.
 *
 * @dc		Destination buffer
 * @sc		Source buffer
 * @dsize	Destination buffer size
 * @ssize	Source buffer size
 * @private	Unused
 */
static int
raidz_copy_abd_cb(void *dc, void *sc, size_t size, void *private)
{
	v_t *dst = (v_t *)dc;
	const v_t *src = (v_t *)sc;
	size_t i;

	COPY_DEFINE();

	(void) private; /* unused */

	for (i = 0; i < size / sizeof (v_t); i += (2 * COPY_STRIDE)) {
		LOAD(src + i, COPY_D);
		STORE(dst + i, COPY_D);

		LOAD(src + i + COPY_STRIDE, COPY_D);
		STORE(dst + i + COPY_STRIDE, COPY_D);
	}

	return (0);
}


#define	raidz_copy(dabd, sabd, size)					\
{									\
	abd_iterate_func2(dabd, sabd, 0, 0, size, raidz_copy_abd_cb, NULL);\
}

/*
 * Method for adding (XORing) two buffers.
 * Source and destination are XORed together and result is stored in
 * destination buffer. This method is used by multiple for gen/rec functions.
 *
 * @dc		Destination buffer
 * @sc		Source buffer
 * @dsize	Destination buffer size
 * @ssize	Source buffer size
 * @private	Unused
 */
static int
raidz_add_abd_cb(void *dc, void *sc, size_t size, void *private)
{
	v_t *dst = (v_t *)dc;
	const v_t *src = (v_t *)sc;
	size_t i;

	ADD_DEFINE();

	(void) private; /* unused */

	for (i = 0; i < size / sizeof (v_t); i += (2 * ADD_STRIDE)) {
		LOAD(dst + i, ADD_D);
		XOR_ACC(src + i, ADD_D);
		STORE(dst + i, ADD_D);

		LOAD(dst + i + ADD_STRIDE, ADD_D);
		XOR_ACC(src + i + ADD_STRIDE, ADD_D);
		STORE(dst + i + ADD_STRIDE, ADD_D);
	}

	return (0);
}

#define	raidz_add(dabd, sabd, size)					\
{									\
	abd_iterate_func2(dabd, sabd, 0, 0, size, raidz_add_abd_cb, NULL);\
}

/*
 * Method for multiplying a buffer with a constant in GF(2^8).
 * Symbols from buffer are multiplied by a constant and result is stored
 * back in the same buffer.
 *
 * @dc		In/Out data buffer.
 * @size	Size of the buffer
 * @private	pointer to the multiplication constant (unsigned)
 */
static int
raidz_mul_abd_cb(void *dc, size_t size, void *private)
{
	const unsigned mul = *((unsigned *)private);
	v_t *d = (v_t *)dc;
	size_t i;

	MUL_DEFINE();

	for (i = 0; i < size / sizeof (v_t); i += (2 * MUL_STRIDE)) {
		LOAD(d + i, MUL_D);
		MUL(mul, MUL_D);
		STORE(d + i, MUL_D);

		LOAD(d + i + MUL_STRIDE, MUL_D);
		MUL(mul, MUL_D);
		STORE(d + i + MUL_STRIDE, MUL_D);
	}

	return (0);
}


/*
 * Syndrome generation/update macros
 *
 * Require LOAD(), XOR(), STORE(), MUL2(), and MUL4() macros
 */
#define	P_D_SYNDROME(D, T, t)		\
{					\
	LOAD((t), T);			\
	XOR(D, T);			\
	STORE((t), T);			\
}

#define	Q_D_SYNDROME(D, T, t)		\
{					\
	LOAD((t), T);			\
	MUL2(T);			\
	XOR(D, T);			\
	STORE((t), T);			\
}

#define	Q_SYNDROME(T, t)		\
{					\
	LOAD((t), T);			\
	MUL2(T);			\
	STORE((t), T);			\
}

#define	R_D_SYNDROME(D, T, t)		\
{					\
	LOAD((t), T);			\
	MUL4(T);			\
	XOR(D, T);			\
	STORE((t), T);			\
}

#define	R_SYNDROME(T, t)		\
{					\
	LOAD((t), T);			\
	MUL4(T);			\
	STORE((t), T);			\
}


/*
 * PARITY CALCULATION
 *
 * Macros *_SYNDROME are used for parity/syndrome calculation.
 * *_D_SYNDROME() macros are used to calculate syndrome between 0 and
 * length of data column, and *_SYNDROME() macros are only for updating
 * the parity/syndrome if data column is shorter.
 *
 * P parity is calculated using raidz_add_abd().
 */

/*
 * Generate P parity (RAIDZ1)
 *
 * @rm	RAIDZ map
 */
static raidz_inline void
raidz_generate_p_impl(raidz_map_t * const rm)
{
	size_t c;
	const size_t ncols = raidz_ncols(rm);
	const size_t psize = rm->rm_col[CODE_P].rc_size;
	abd_t *pabd = rm->rm_col[CODE_P].rc_abd;
	size_t size;
	abd_t *dabd;

	raidz_math_begin();

	/* start with first data column */
	raidz_copy(pabd, rm->rm_col[1].rc_abd, psize);

	for (c = 2; c < ncols; c++) {
		dabd = rm->rm_col[c].rc_abd;
		size = rm->rm_col[c].rc_size;

		/* add data column */
		raidz_add(pabd, dabd, size);
	}

	raidz_math_end();
}


/*
 * Generate PQ parity (RAIDZ2)
 * The function is called per data column.
 *
 * @c		array of pointers to parity (code) columns
 * @dc		pointer to data column
 * @csize	size of parity columns
 * @dsize	size of data column
 */
static void
raidz_gen_pq_add(void **c, const void *dc, const size_t csize,
    const size_t dsize)
{
	v_t *p = (v_t *)c[0];
	v_t *q = (v_t *)c[1];
	const v_t *d = (v_t *)dc;
	const v_t * const dend = d + (dsize / sizeof (v_t));
	const v_t * const qend = q + (csize / sizeof (v_t));

	GEN_PQ_DEFINE();

	MUL2_SETUP();

	for (; d < dend; d += GEN_PQ_STRIDE, p += GEN_PQ_STRIDE,
	    q += GEN_PQ_STRIDE) {
		LOAD(d, GEN_PQ_D);
		P_D_SYNDROME(GEN_PQ_D, GEN_PQ_C, p);
		Q_D_SYNDROME(GEN_PQ_D, GEN_PQ_C, q);
	}
	for (; q < qend; q += GEN_PQ_STRIDE) {
		Q_SYNDROME(GEN_PQ_C, q);
	}
}


/*
 * Generate PQ parity (RAIDZ2)
 *
 * @rm	RAIDZ map
 */
static raidz_inline void
raidz_generate_pq_impl(raidz_map_t * const rm)
{
	size_t c;
	const size_t ncols = raidz_ncols(rm);
	const size_t csize = rm->rm_col[CODE_P].rc_size;
	size_t dsize;
	abd_t *dabd;
	abd_t *cabds[] = {
		rm->rm_col[CODE_P].rc_abd,
		rm->rm_col[CODE_Q].rc_abd
	};

	raidz_math_begin();

	raidz_copy(cabds[CODE_P], rm->rm_col[2].rc_abd, csize);
	raidz_copy(cabds[CODE_Q], rm->rm_col[2].rc_abd, csize);

	for (c = 3; c < ncols; c++) {
		dabd = rm->rm_col[c].rc_abd;
		dsize = rm->rm_col[c].rc_size;

		abd_raidz_gen_iterate(cabds, dabd, csize, dsize, 2,
		    raidz_gen_pq_add);
	}

	raidz_math_end();
}


/*
 * Generate PQR parity (RAIDZ3)
 * The function is called per data column.
 *
 * @c		array of pointers to parity (code) columns
 * @dc		pointer to data column
 * @csize	size of parity columns
 * @dsize	size of data column
 */
static void
raidz_gen_pqr_add(void **c, const void *dc, const size_t csize,
    const size_t dsize)
{
	v_t *p = (v_t *)c[0];
	v_t *q = (v_t *)c[1];
	v_t *r = (v_t *)c[CODE_R];
	const v_t *d = (v_t *)dc;
	const v_t * const dend = d + (dsize / sizeof (v_t));
	const v_t * const qend = q + (csize / sizeof (v_t));

	GEN_PQR_DEFINE();

	MUL2_SETUP();

	for (; d < dend; d += GEN_PQR_STRIDE, p += GEN_PQR_STRIDE,
	    q += GEN_PQR_STRIDE, r += GEN_PQR_STRIDE) {
		LOAD(d, GEN_PQR_D);
		P_D_SYNDROME(GEN_PQR_D, GEN_PQR_C, p);
		Q_D_SYNDROME(GEN_PQR_D, GEN_PQR_C, q);
		R_D_SYNDROME(GEN_PQR_D, GEN_PQR_C, r);
	}
	for (; q < qend; q += GEN_PQR_STRIDE, r += GEN_PQR_STRIDE) {
		Q_SYNDROME(GEN_PQR_C, q);
		R_SYNDROME(GEN_PQR_C, r);
	}
}


/*
 * Generate PQR parity (RAIDZ2)
 *
 * @rm	RAIDZ map
 */
static raidz_inline void
raidz_generate_pqr_impl(raidz_map_t * const rm)
{
	size_t c;
	const size_t ncols = raidz_ncols(rm);
	const size_t csize = rm->rm_col[CODE_P].rc_size;
	size_t dsize;
	abd_t *dabd;
	abd_t *cabds[] = {
		rm->rm_col[CODE_P].rc_abd,
		rm->rm_col[CODE_Q].rc_abd,
		rm->rm_col[CODE_R].rc_abd
	};

	raidz_math_begin();

	raidz_copy(cabds[CODE_P], rm->rm_col[3].rc_abd, csize);
	raidz_copy(cabds[CODE_Q], rm->rm_col[3].rc_abd, csize);
	raidz_copy(cabds[CODE_R], rm->rm_col[3].rc_abd, csize);

	for (c = 4; c < ncols; c++) {
		dabd = rm->rm_col[c].rc_abd;
		dsize = rm->rm_col[c].rc_size;

		abd_raidz_gen_iterate(cabds, dabd, csize, dsize, 3,
		    raidz_gen_pqr_add);
	}

	raidz_math_end();
}


/*
 * DATA RECONSTRUCTION
 *
 * Data reconstruction process consists of two phases:
 * 	- Syndrome calculation
 * 	- Data reconstruction
 *
 * Syndrome is calculated by generating parity using available data columns
 * and zeros in places of erasure. Existing parity is added to corresponding
 * syndrome value to obtain the [P|Q|R]syn values from equation:
 * 	P = Psyn + Dx + Dy + Dz
 * 	Q = Qsyn + 2^x * Dx + 2^y * Dy + 2^z * Dz
 * 	R = Rsyn + 4^x * Dx + 4^y * Dy + 4^z * Dz
 *
 * For data reconstruction phase, the corresponding equations are solved
 * for missing data (Dx, Dy, Dz). This generally involves multiplying known
 * symbols by an coefficient and adding them together. The multiplication
 * constant coefficients are calculated ahead of the operation in
 * raidz_rec_[q|r|pq|pq|qr|pqr]_coeff() functions.
 *
 * IMPLEMENTATION NOTE: RAID-Z block can have complex geometry, with "big"
 * and "short" columns.
 * For this reason, reconstruction is performed in minimum of
 * two steps. First, from offset 0 to short_size, then from short_size to
 * short_size. Calculation functions REC_[*]_BLOCK() are implemented to work
 * over both ranges. The split also enables removal of conditional expressions
 * from loop bodies, improving throughput of SIMD implementations.
 * For the best performance, all functions marked with raidz_inline attribute
 * must be inlined by compiler.
 *
 *    parity          data
 *    columns         columns
 * <----------> <------------------>
 *                   x       y  <----+ missing columns (x, y)
 *                   |       |
 * +---+---+---+---+-v-+---+-v-+---+   ^ 0
 * |   |   |   |   |   |   |   |   |   |
 * |   |   |   |   |   |   |   |   |   |
 * | P | Q | R | D | D | D | D | D |   |
 * |   |   |   | 0 | 1 | 2 | 3 | 4 |   |
 * |   |   |   |   |   |   |   |   |   v
 * |   |   |   |   |   +---+---+---+   ^ short_size
 * |   |   |   |   |   |               |
 * +---+---+---+---+---+               v big_size
 * <------------------> <---------->
 *      big columns     short columns
 *
 */




/*
 * Reconstruct single data column using P parity
 *
 * @syn_method	raidz_add_abd()
 * @rec_method	not applicable
 *
 * @rm		RAIDZ map
 * @tgtidx	array of missing data indexes
 */
static raidz_inline int
raidz_reconstruct_p_impl(raidz_map_t *rm, const int *tgtidx)
{
	size_t c;
	const size_t firstdc = raidz_parity(rm);
	const size_t ncols = raidz_ncols(rm);
	const size_t x = tgtidx[TARGET_X];
	const size_t xsize = rm->rm_col[x].rc_size;
	abd_t *xabd = rm->rm_col[x].rc_abd;
	size_t size;
	abd_t *dabd;

	raidz_math_begin();

	/* copy P into target */
	raidz_copy(xabd, rm->rm_col[CODE_P].rc_abd, xsize);

	/* generate p_syndrome */
	for (c = firstdc; c < ncols; c++) {
		if (c == x)
			continue;

		dabd = rm->rm_col[c].rc_abd;
		size = MIN(rm->rm_col[c].rc_size, xsize);

		raidz_add(xabd, dabd, size);
	}

	raidz_math_end();

	return (1 << CODE_P);
}


/*
 * Generate Q syndrome (Qsyn)
 *
 * @xc		array of pointers to syndrome columns
 * @dc		data column (NULL if missing)
 * @xsize	size of syndrome columns
 * @dsize	size of data column (0 if missing)
 */
static void
raidz_syn_q_abd(void **xc, const void *dc, const size_t xsize,
    const size_t dsize)
{
	v_t *x = (v_t *)xc[TARGET_X];
	const v_t *d = (v_t *)dc;
	const v_t * const dend = d + (dsize / sizeof (v_t));
	const v_t * const xend = x + (xsize / sizeof (v_t));

	SYN_Q_DEFINE();

	MUL2_SETUP();

	for (; d < dend; d += SYN_STRIDE, x += SYN_STRIDE) {
		LOAD(d, SYN_Q_D);
		Q_D_SYNDROME(SYN_Q_D, SYN_Q_X, x);
	}
	for (; x < xend; x += SYN_STRIDE) {
		Q_SYNDROME(SYN_Q_X, x);
	}
}


/*
 * Reconstruct single data column using Q parity
 *
 * @syn_method	raidz_add_abd()
 * @rec_method	raidz_mul_abd_cb()
 *
 * @rm		RAIDZ map
 * @tgtidx	array of missing data indexes
 */
static raidz_inline int
raidz_reconstruct_q_impl(raidz_map_t *rm, const int *tgtidx)
{
	size_t c;
	size_t dsize;
	abd_t *dabd;
	const size_t firstdc = raidz_parity(rm);
	const size_t ncols = raidz_ncols(rm);
	const size_t x = tgtidx[TARGET_X];
	abd_t *xabd = rm->rm_col[x].rc_abd;
	const size_t xsize = rm->rm_col[x].rc_size;
	abd_t *tabds[] = { xabd };

	unsigned coeff[MUL_CNT];
	raidz_rec_q_coeff(rm, tgtidx, coeff);

	raidz_math_begin();

	/* Start with first data column if present */
	if (firstdc != x) {
		raidz_copy(xabd, rm->rm_col[firstdc].rc_abd, xsize);
	} else {
		raidz_zero(xabd, xsize);
	}

	/* generate q_syndrome */
	for (c = firstdc+1; c < ncols; c++) {
		if (c == x) {
			dabd = NULL;
			dsize = 0;
		} else {
			dabd = rm->rm_col[c].rc_abd;
			dsize = rm->rm_col[c].rc_size;
		}

		abd_raidz_gen_iterate(tabds, dabd, xsize, dsize, 1,
		    raidz_syn_q_abd);
	}

	/* add Q to the syndrome */
	raidz_add(xabd, rm->rm_col[CODE_Q].rc_abd, xsize);

	/* transform the syndrome */
	abd_iterate_func(xabd, 0, xsize, raidz_mul_abd_cb, (void*) coeff);

	raidz_math_end();

	return (1 << CODE_Q);
}


/*
 * Generate R syndrome (Rsyn)
 *
 * @xc		array of pointers to syndrome columns
 * @dc		data column (NULL if missing)
 * @tsize	size of syndrome columns
 * @dsize	size of data column (0 if missing)
 */
static void
raidz_syn_r_abd(void **xc, const void *dc, const size_t tsize,
    const size_t dsize)
{
	v_t *x = (v_t *)xc[TARGET_X];
	const v_t *d = (v_t *)dc;
	const v_t * const dend = d + (dsize / sizeof (v_t));
	const v_t * const xend = x + (tsize / sizeof (v_t));

	SYN_R_DEFINE();

	MUL2_SETUP();

	for (; d < dend; d += SYN_STRIDE, x += SYN_STRIDE) {
		LOAD(d, SYN_R_D);
		R_D_SYNDROME(SYN_R_D, SYN_R_X, x);
	}
	for (; x < xend; x += SYN_STRIDE) {
		R_SYNDROME(SYN_R_X, x);
	}
}


/*
 * Reconstruct single data column using R parity
 *
 * @syn_method	raidz_add_abd()
 * @rec_method	raidz_mul_abd_cb()
 *
 * @rm		RAIDZ map
 * @tgtidx	array of missing data indexes
 */
static raidz_inline int
raidz_reconstruct_r_impl(raidz_map_t *rm, const int *tgtidx)
{
	size_t c;
	size_t dsize;
	abd_t *dabd;
	const size_t firstdc = raidz_parity(rm);
	const size_t ncols = raidz_ncols(rm);
	const size_t x = tgtidx[TARGET_X];
	const size_t xsize = rm->rm_col[x].rc_size;
	abd_t *xabd = rm->rm_col[x].rc_abd;
	abd_t *tabds[] = { xabd };

	unsigned coeff[MUL_CNT];
	raidz_rec_r_coeff(rm, tgtidx, coeff);

	raidz_math_begin();

	/* Start with first data column if present */
	if (firstdc != x) {
		raidz_copy(xabd, rm->rm_col[firstdc].rc_abd, xsize);
	} else {
		raidz_zero(xabd, xsize);
	}


	/* generate q_syndrome */
	for (c = firstdc+1; c < ncols; c++) {
		if (c == x) {
			dabd = NULL;
			dsize = 0;
		} else {
			dabd = rm->rm_col[c].rc_abd;
			dsize = rm->rm_col[c].rc_size;
		}

		abd_raidz_gen_iterate(tabds, dabd, xsize, dsize, 1,
		    raidz_syn_r_abd);
	}

	/* add R to the syndrome */
	raidz_add(xabd, rm->rm_col[CODE_R].rc_abd, xsize);

	/* transform the syndrome */
	abd_iterate_func(xabd, 0, xsize, raidz_mul_abd_cb, (void *)coeff);

	raidz_math_end();

	return (1 << CODE_R);
}


/*
 * Generate P and Q syndromes
 *
 * @xc		array of pointers to syndrome columns
 * @dc		data column (NULL if missing)
 * @tsize	size of syndrome columns
 * @dsize	size of data column (0 if missing)
 */
static void
raidz_syn_pq_abd(void **tc, const void *dc, const size_t tsize,
    const size_t dsize)
{
	v_t *x = (v_t *)tc[TARGET_X];
	v_t *y = (v_t *)tc[TARGET_Y];
	const v_t *d = (v_t *)dc;
	const v_t * const dend = d + (dsize / sizeof (v_t));
	const v_t * const yend = y + (tsize / sizeof (v_t));

	SYN_PQ_DEFINE();

	MUL2_SETUP();

	for (; d < dend; d += SYN_STRIDE, x += SYN_STRIDE, y += SYN_STRIDE) {
		LOAD(d, SYN_PQ_D);
		P_D_SYNDROME(SYN_PQ_D, SYN_PQ_X, x);
		Q_D_SYNDROME(SYN_PQ_D, SYN_PQ_X, y);
	}
	for (; y < yend; y += SYN_STRIDE) {
		Q_SYNDROME(SYN_PQ_X, y);
	}
}

/*
 * Reconstruct data using PQ parity and PQ syndromes
 *
 * @tc		syndrome/result columns
 * @tsize	size of syndrome/result columns
 * @c		parity columns
 * @mul		array of multiplication constants
 */
static void
raidz_rec_pq_abd(void **tc, const size_t tsize, void **c,
    const unsigned *mul)
{
	v_t *x = (v_t *)tc[TARGET_X];
	v_t *y = (v_t *)tc[TARGET_Y];
	const v_t * const xend = x + (tsize / sizeof (v_t));
	const v_t *p = (v_t *)c[CODE_P];
	const v_t *q = (v_t *)c[CODE_Q];

	REC_PQ_DEFINE();

	for (; x < xend; x += REC_PQ_STRIDE, y += REC_PQ_STRIDE,
	    p += REC_PQ_STRIDE, q += REC_PQ_STRIDE) {
		LOAD(x, REC_PQ_X);
		LOAD(y, REC_PQ_Y);

		XOR_ACC(p, REC_PQ_X);
		XOR_ACC(q, REC_PQ_Y);

		/* Save Pxy */
		COPY(REC_PQ_X,  REC_PQ_T);

		/* Calc X */
		MUL(mul[MUL_PQ_X], REC_PQ_X);
		MUL(mul[MUL_PQ_Y], REC_PQ_Y);
		XOR(REC_PQ_Y,  REC_PQ_X);
		STORE(x, REC_PQ_X);

		/* Calc Y */
		XOR(REC_PQ_T,  REC_PQ_X);
		STORE(y, REC_PQ_X);
	}
}


/*
 * Reconstruct two data columns using PQ parity
 *
 * @syn_method	raidz_syn_pq_abd()
 * @rec_method	raidz_rec_pq_abd()
 *
 * @rm		RAIDZ map
 * @tgtidx	array of missing data indexes
 */
static raidz_inline int
raidz_reconstruct_pq_impl(raidz_map_t *rm, const int *tgtidx)
{
	size_t c;
	size_t dsize;
	abd_t *dabd;
	const size_t firstdc = raidz_parity(rm);
	const size_t ncols = raidz_ncols(rm);
	const size_t x = tgtidx[TARGET_X];
	const size_t y = tgtidx[TARGET_Y];
	const size_t xsize = rm->rm_col[x].rc_size;
	const size_t ysize = rm->rm_col[y].rc_size;
	abd_t *xabd = rm->rm_col[x].rc_abd;
	abd_t *yabd = rm->rm_col[y].rc_abd;
	abd_t *tabds[2] = { xabd, yabd };
	abd_t *cabds[] = {
		rm->rm_col[CODE_P].rc_abd,
		rm->rm_col[CODE_Q].rc_abd
	};

	unsigned coeff[MUL_CNT];
	raidz_rec_pq_coeff(rm, tgtidx, coeff);

	/*
	 * Check if some of targets is shorter then others
	 * In this case, shorter target needs to be replaced with
	 * new buffer so that syndrome can be calculated.
	 */
	if (ysize < xsize) {
		yabd = abd_alloc(xsize, B_FALSE);
		tabds[1] = yabd;
	}

	raidz_math_begin();

	/* Start with first data column if present */
	if (firstdc != x) {
		raidz_copy(xabd, rm->rm_col[firstdc].rc_abd, xsize);
		raidz_copy(yabd, rm->rm_col[firstdc].rc_abd, xsize);
	} else {
		raidz_zero(xabd, xsize);
		raidz_zero(yabd, xsize);
	}

	/* generate q_syndrome */
	for (c = firstdc+1; c < ncols; c++) {
		if (c == x || c == y) {
			dabd = NULL;
			dsize = 0;
		} else {
			dabd = rm->rm_col[c].rc_abd;
			dsize = rm->rm_col[c].rc_size;
		}

		abd_raidz_gen_iterate(tabds, dabd, xsize, dsize, 2,
		    raidz_syn_pq_abd);
	}

	abd_raidz_rec_iterate(cabds, tabds, xsize, 2, raidz_rec_pq_abd, coeff);

	/* Copy shorter targets back to the original abd buffer */
	if (ysize < xsize)
		raidz_copy(rm->rm_col[y].rc_abd, yabd, ysize);

	raidz_math_end();

	if (ysize < xsize)
		abd_free(yabd);

	return ((1 << CODE_P) | (1 << CODE_Q));
}


/*
 * Generate P and R syndromes
 *
 * @xc		array of pointers to syndrome columns
 * @dc		data column (NULL if missing)
 * @tsize	size of syndrome columns
 * @dsize	size of data column (0 if missing)
 */
static void
raidz_syn_pr_abd(void **c, const void *dc, const size_t tsize,
    const size_t dsize)
{
	v_t *x = (v_t *)c[TARGET_X];
	v_t *y = (v_t *)c[TARGET_Y];
	const v_t *d = (v_t *)dc;
	const v_t * const dend = d + (dsize / sizeof (v_t));
	const v_t * const yend = y + (tsize / sizeof (v_t));

	SYN_PR_DEFINE();

	MUL2_SETUP();

	for (; d < dend; d += SYN_STRIDE, x += SYN_STRIDE, y += SYN_STRIDE) {
		LOAD(d, SYN_PR_D);
		P_D_SYNDROME(SYN_PR_D, SYN_PR_X, x);
		R_D_SYNDROME(SYN_PR_D, SYN_PR_X, y);
	}
	for (; y < yend; y += SYN_STRIDE) {
		R_SYNDROME(SYN_PR_X, y);
	}
}

/*
 * Reconstruct data using PR parity and PR syndromes
 *
 * @tc		syndrome/result columns
 * @tsize	size of syndrome/result columns
 * @c		parity columns
 * @mul		array of multiplication constants
 */
static void
raidz_rec_pr_abd(void **t, const size_t tsize, void **c,
    const unsigned *mul)
{
	v_t *x = (v_t *)t[TARGET_X];
	v_t *y = (v_t *)t[TARGET_Y];
	const v_t * const xend = x + (tsize / sizeof (v_t));
	const v_t *p = (v_t *)c[CODE_P];
	const v_t *q = (v_t *)c[CODE_Q];

	REC_PR_DEFINE();

	for (; x < xend; x += REC_PR_STRIDE, y += REC_PR_STRIDE,
	    p += REC_PR_STRIDE, q += REC_PR_STRIDE) {
		LOAD(x, REC_PR_X);
		LOAD(y, REC_PR_Y);
		XOR_ACC(p, REC_PR_X);
		XOR_ACC(q, REC_PR_Y);

		/* Save Pxy */
		COPY(REC_PR_X,  REC_PR_T);

		/* Calc X */
		MUL(mul[MUL_PR_X], REC_PR_X);
		MUL(mul[MUL_PR_Y], REC_PR_Y);
		XOR(REC_PR_Y,  REC_PR_X);
		STORE(x, REC_PR_X);

		/* Calc Y */
		XOR(REC_PR_T,  REC_PR_X);
		STORE(y, REC_PR_X);
	}
}


/*
 * Reconstruct two data columns using PR parity
 *
 * @syn_method	raidz_syn_pr_abd()
 * @rec_method	raidz_rec_pr_abd()
 *
 * @rm		RAIDZ map
 * @tgtidx	array of missing data indexes
 */
static raidz_inline int
raidz_reconstruct_pr_impl(raidz_map_t *rm, const int *tgtidx)
{
	size_t c;
	size_t dsize;
	abd_t *dabd;
	const size_t firstdc = raidz_parity(rm);
	const size_t ncols = raidz_ncols(rm);
	const size_t x = tgtidx[0];
	const size_t y = tgtidx[1];
	const size_t xsize = rm->rm_col[x].rc_size;
	const size_t ysize = rm->rm_col[y].rc_size;
	abd_t *xabd = rm->rm_col[x].rc_abd;
	abd_t *yabd = rm->rm_col[y].rc_abd;
	abd_t *tabds[2] = { xabd, yabd };
	abd_t *cabds[] = {
		rm->rm_col[CODE_P].rc_abd,
		rm->rm_col[CODE_R].rc_abd
	};
	unsigned coeff[MUL_CNT];
	raidz_rec_pr_coeff(rm, tgtidx, coeff);

	/*
	 * Check if some of targets are shorter then others.
	 * They need to be replaced with a new buffer so that syndrome can
	 * be calculated on full length.
	 */
	if (ysize < xsize) {
		yabd = abd_alloc(xsize, B_FALSE);
		tabds[1] = yabd;
	}

	raidz_math_begin();

	/* Start with first data column if present */
	if (firstdc != x) {
		raidz_copy(xabd, rm->rm_col[firstdc].rc_abd, xsize);
		raidz_copy(yabd, rm->rm_col[firstdc].rc_abd, xsize);
	} else {
		raidz_zero(xabd, xsize);
		raidz_zero(yabd, xsize);
	}

	/* generate q_syndrome */
	for (c = firstdc+1; c < ncols; c++) {
		if (c == x || c == y) {
			dabd = NULL;
			dsize = 0;
		} else {
			dabd = rm->rm_col[c].rc_abd;
			dsize = rm->rm_col[c].rc_size;
		}

		abd_raidz_gen_iterate(tabds, dabd, xsize, dsize, 2,
		    raidz_syn_pr_abd);
	}

	abd_raidz_rec_iterate(cabds, tabds, xsize, 2, raidz_rec_pr_abd, coeff);

	/*
	 * Copy shorter targets back to the original abd buffer
	 */
	if (ysize < xsize)
		raidz_copy(rm->rm_col[y].rc_abd, yabd, ysize);

	raidz_math_end();

	if (ysize < xsize)
		abd_free(yabd);

	return ((1 << CODE_P) | (1 << CODE_Q));
}


/*
 * Generate Q and R syndromes
 *
 * @xc		array of pointers to syndrome columns
 * @dc		data column (NULL if missing)
 * @tsize	size of syndrome columns
 * @dsize	size of data column (0 if missing)
 */
static void
raidz_syn_qr_abd(void **c, const void *dc, const size_t tsize,
    const size_t dsize)
{
	v_t *x = (v_t *)c[TARGET_X];
	v_t *y = (v_t *)c[TARGET_Y];
	const v_t * const xend = x + (tsize / sizeof (v_t));
	const v_t *d = (v_t *)dc;
	const v_t * const dend = d + (dsize / sizeof (v_t));

	SYN_QR_DEFINE();

	MUL2_SETUP();

	for (; d < dend; d += SYN_STRIDE, x += SYN_STRIDE, y += SYN_STRIDE) {
		LOAD(d, SYN_PQ_D);
		Q_D_SYNDROME(SYN_QR_D, SYN_QR_X, x);
		R_D_SYNDROME(SYN_QR_D, SYN_QR_X, y);
	}
	for (; x < xend; x += SYN_STRIDE, y += SYN_STRIDE) {
		Q_SYNDROME(SYN_QR_X, x);
		R_SYNDROME(SYN_QR_X, y);
	}
}


/*
 * Reconstruct data using QR parity and QR syndromes
 *
 * @tc		syndrome/result columns
 * @tsize	size of syndrome/result columns
 * @c		parity columns
 * @mul		array of multiplication constants
 */
static void
raidz_rec_qr_abd(void **t, const size_t tsize, void **c,
    const unsigned *mul)
{
	v_t *x = (v_t *)t[TARGET_X];
	v_t *y = (v_t *)t[TARGET_Y];
	const v_t * const xend = x + (tsize / sizeof (v_t));
	const v_t *p = (v_t *)c[CODE_P];
	const v_t *q = (v_t *)c[CODE_Q];

	REC_QR_DEFINE();

	for (; x < xend; x += REC_QR_STRIDE, y += REC_QR_STRIDE,
	    p += REC_QR_STRIDE, q += REC_QR_STRIDE) {
		LOAD(x, REC_QR_X);
		LOAD(y, REC_QR_Y);

		XOR_ACC(p, REC_QR_X);
		XOR_ACC(q, REC_QR_Y);

		/* Save Pxy */
		COPY(REC_QR_X,  REC_QR_T);

		/* Calc X */
		MUL(mul[MUL_QR_XQ], REC_QR_X);	/* X = Q * xqm */
		XOR(REC_QR_Y, REC_QR_X);	/* X = R ^ X   */
		MUL(mul[MUL_QR_X], REC_QR_X);	/* X = X * xm  */
		STORE(x, REC_QR_X);

		/* Calc Y */
		MUL(mul[MUL_QR_YQ], REC_QR_T);	/* X = Q * xqm */
		XOR(REC_QR_Y, REC_QR_T);	/* X = R ^ X   */
		MUL(mul[MUL_QR_Y], REC_QR_T);	/* X = X * xm  */
		STORE(y, REC_QR_T);
	}
}


/*
 * Reconstruct two data columns using QR parity
 *
 * @syn_method	raidz_syn_qr_abd()
 * @rec_method	raidz_rec_qr_abd()
 *
 * @rm		RAIDZ map
 * @tgtidx	array of missing data indexes
 */
static raidz_inline int
raidz_reconstruct_qr_impl(raidz_map_t *rm, const int *tgtidx)
{
	size_t c;
	size_t dsize;
	abd_t *dabd;
	const size_t firstdc = raidz_parity(rm);
	const size_t ncols = raidz_ncols(rm);
	const size_t x = tgtidx[TARGET_X];
	const size_t y = tgtidx[TARGET_Y];
	const size_t xsize = rm->rm_col[x].rc_size;
	const size_t ysize = rm->rm_col[y].rc_size;
	abd_t *xabd = rm->rm_col[x].rc_abd;
	abd_t *yabd = rm->rm_col[y].rc_abd;
	abd_t *tabds[2] = { xabd, yabd };
	abd_t *cabds[] = {
		rm->rm_col[CODE_Q].rc_abd,
		rm->rm_col[CODE_R].rc_abd
	};
	unsigned coeff[MUL_CNT];
	raidz_rec_qr_coeff(rm, tgtidx, coeff);

	/*
	 * Check if some of targets is shorter then others
	 * In this case, shorter target needs to be replaced with
	 * new buffer so that syndrome can be calculated.
	 */
	if (ysize < xsize) {
		yabd = abd_alloc(xsize, B_FALSE);
		tabds[1] = yabd;
	}

	raidz_math_begin();

	/* Start with first data column if present */
	if (firstdc != x) {
		raidz_copy(xabd, rm->rm_col[firstdc].rc_abd, xsize);
		raidz_copy(yabd, rm->rm_col[firstdc].rc_abd, xsize);
	} else {
		raidz_zero(xabd, xsize);
		raidz_zero(yabd, xsize);
	}

	/* generate q_syndrome */
	for (c = firstdc+1; c < ncols; c++) {
		if (c == x || c == y) {
			dabd = NULL;
			dsize = 0;
		} else {
			dabd = rm->rm_col[c].rc_abd;
			dsize = rm->rm_col[c].rc_size;
		}

		abd_raidz_gen_iterate(tabds, dabd, xsize, dsize, 2,
		    raidz_syn_qr_abd);
	}

	abd_raidz_rec_iterate(cabds, tabds, xsize, 2, raidz_rec_qr_abd, coeff);

	/*
	 * Copy shorter targets back to the original abd buffer
	 */
	if (ysize < xsize)
		raidz_copy(rm->rm_col[y].rc_abd, yabd, ysize);

	raidz_math_end();

	if (ysize < xsize)
		abd_free(yabd);


	return ((1 << CODE_Q) | (1 << CODE_R));
}


/*
 * Generate P, Q, and R syndromes
 *
 * @xc		array of pointers to syndrome columns
 * @dc		data column (NULL if missing)
 * @tsize	size of syndrome columns
 * @dsize	size of data column (0 if missing)
 */
static void
raidz_syn_pqr_abd(void **c, const void *dc, const size_t tsize,
    const size_t dsize)
{
	v_t *x = (v_t *)c[TARGET_X];
	v_t *y = (v_t *)c[TARGET_Y];
	v_t *z = (v_t *)c[TARGET_Z];
	const v_t * const yend = y + (tsize / sizeof (v_t));
	const v_t *d = (v_t *)dc;
	const v_t * const dend = d + (dsize / sizeof (v_t));

	SYN_PQR_DEFINE();

	MUL2_SETUP();

	for (; d < dend;  d += SYN_STRIDE, x += SYN_STRIDE, y += SYN_STRIDE,
	    z += SYN_STRIDE) {
		LOAD(d, SYN_PQR_D);
		P_D_SYNDROME(SYN_PQR_D, SYN_PQR_X, x)
		Q_D_SYNDROME(SYN_PQR_D, SYN_PQR_X, y);
		R_D_SYNDROME(SYN_PQR_D, SYN_PQR_X, z);
	}
	for (; y < yend; y += SYN_STRIDE, z += SYN_STRIDE) {
		Q_SYNDROME(SYN_PQR_X, y);
		R_SYNDROME(SYN_PQR_X, z);
	}
}


/*
 * Reconstruct data using PRQ parity and PQR syndromes
 *
 * @tc		syndrome/result columns
 * @tsize	size of syndrome/result columns
 * @c		parity columns
 * @mul		array of multiplication constants
 */
static void
raidz_rec_pqr_abd(void **t, const size_t tsize, void **c,
    const unsigned * const mul)
{
	v_t *x = (v_t *)t[TARGET_X];
	v_t *y = (v_t *)t[TARGET_Y];
	v_t *z = (v_t *)t[TARGET_Z];
	const v_t * const xend = x + (tsize / sizeof (v_t));
	const v_t *p = (v_t *)c[CODE_P];
	const v_t *q = (v_t *)c[CODE_Q];
	const v_t *r = (v_t *)c[CODE_R];

	REC_PQR_DEFINE();

	for (; x < xend; x += REC_PQR_STRIDE, y += REC_PQR_STRIDE,
	    z += REC_PQR_STRIDE, p += REC_PQR_STRIDE, q += REC_PQR_STRIDE,
	    r += REC_PQR_STRIDE) {
		LOAD(x, REC_PQR_X);
		LOAD(y, REC_PQR_Y);
		LOAD(z, REC_PQR_Z);

		XOR_ACC(p, REC_PQR_X);
		XOR_ACC(q, REC_PQR_Y);
		XOR_ACC(r, REC_PQR_Z);

		/* Save Pxyz and Qxyz */
		COPY(REC_PQR_X, REC_PQR_XS);
		COPY(REC_PQR_Y, REC_PQR_YS);

		/* Calc X */
		MUL(mul[MUL_PQR_XP], REC_PQR_X);	/* Xp = Pxyz * xp   */
		MUL(mul[MUL_PQR_XQ], REC_PQR_Y);	/* Xq = Qxyz * xq   */
		XOR(REC_PQR_Y, REC_PQR_X);
		MUL(mul[MUL_PQR_XR], REC_PQR_Z);	/* Xr = Rxyz * xr   */
		XOR(REC_PQR_Z, REC_PQR_X);		/* X = Xp + Xq + Xr */
		STORE(x, REC_PQR_X);

		/* Calc Y */
		XOR(REC_PQR_X, REC_PQR_XS); 		/* Pyz = Pxyz + X */
		MUL(mul[MUL_PQR_YU], REC_PQR_X);  	/* Xq = X * upd_q */
		XOR(REC_PQR_X, REC_PQR_YS); 		/* Qyz = Qxyz + Xq */
		COPY(REC_PQR_XS, REC_PQR_X);		/* restore Pyz */
		MUL(mul[MUL_PQR_YP], REC_PQR_X);	/* Yp = Pyz * yp */
		MUL(mul[MUL_PQR_YQ], REC_PQR_YS);	/* Yq = Qyz * yq */
		XOR(REC_PQR_X, REC_PQR_YS); 		/* Y = Yp + Yq */
		STORE(y, REC_PQR_YS);

		/* Calc Z */
		XOR(REC_PQR_XS, REC_PQR_YS);		/* Z = Pz = Pyz + Y */
		STORE(z, REC_PQR_YS);
	}
}


/*
 * Reconstruct three data columns using PQR parity
 *
 * @syn_method	raidz_syn_pqr_abd()
 * @rec_method	raidz_rec_pqr_abd()
 *
 * @rm		RAIDZ map
 * @tgtidx	array of missing data indexes
 */
static raidz_inline int
raidz_reconstruct_pqr_impl(raidz_map_t *rm, const int *tgtidx)
{
	size_t c;
	size_t dsize;
	abd_t *dabd;
	const size_t firstdc = raidz_parity(rm);
	const size_t ncols = raidz_ncols(rm);
	const size_t x = tgtidx[TARGET_X];
	const size_t y = tgtidx[TARGET_Y];
	const size_t z = tgtidx[TARGET_Z];
	const size_t xsize = rm->rm_col[x].rc_size;
	const size_t ysize = rm->rm_col[y].rc_size;
	const size_t zsize = rm->rm_col[z].rc_size;
	abd_t *xabd = rm->rm_col[x].rc_abd;
	abd_t *yabd = rm->rm_col[y].rc_abd;
	abd_t *zabd = rm->rm_col[z].rc_abd;
	abd_t *tabds[] = { xabd, yabd, zabd };
	abd_t *cabds[] = {
		rm->rm_col[CODE_P].rc_abd,
		rm->rm_col[CODE_Q].rc_abd,
		rm->rm_col[CODE_R].rc_abd
	};
	unsigned coeff[MUL_CNT];
	raidz_rec_pqr_coeff(rm, tgtidx, coeff);

	/*
	 * Check if some of targets is shorter then others
	 * In this case, shorter target needs to be replaced with
	 * new buffer so that syndrome can be calculated.
	 */
	if (ysize < xsize) {
		yabd = abd_alloc(xsize, B_FALSE);
		tabds[1] = yabd;
	}
	if (zsize < xsize) {
		zabd = abd_alloc(xsize, B_FALSE);
		tabds[2] = zabd;
	}

	raidz_math_begin();

	/* Start with first data column if present */
	if (firstdc != x) {
		raidz_copy(xabd, rm->rm_col[firstdc].rc_abd, xsize);
		raidz_copy(yabd, rm->rm_col[firstdc].rc_abd, xsize);
		raidz_copy(zabd, rm->rm_col[firstdc].rc_abd, xsize);
	} else {
		raidz_zero(xabd, xsize);
		raidz_zero(yabd, xsize);
		raidz_zero(zabd, xsize);
	}

	/* generate q_syndrome */
	for (c = firstdc+1; c < ncols; c++) {
		if (c == x || c == y || c == z) {
			dabd = NULL;
			dsize = 0;
		} else {
			dabd = rm->rm_col[c].rc_abd;
			dsize = rm->rm_col[c].rc_size;
		}

		abd_raidz_gen_iterate(tabds, dabd, xsize, dsize, 3,
		    raidz_syn_pqr_abd);
	}

	abd_raidz_rec_iterate(cabds, tabds, xsize, 3, raidz_rec_pqr_abd, coeff);

	/*
	 * Copy shorter targets back to the original abd buffer
	 */
	if (ysize < xsize)
		raidz_copy(rm->rm_col[y].rc_abd, yabd, ysize);
	if (zsize < xsize)
		raidz_copy(rm->rm_col[z].rc_abd, zabd, zsize);

	raidz_math_end();

	if (ysize < xsize)
		abd_free(yabd);
	if (zsize < xsize)
		abd_free(zabd);

	return ((1 << CODE_P) | (1 << CODE_Q) | (1 << CODE_R));
}

#endif /* _VDEV_RAIDZ_MATH_IMPL_H */
