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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 * Copyright (C) 2016 Gvozden Nešković. All rights reserved.
 */
/*
 * Copyright 2013 Saso Kiselkov. All rights reserved.
 */

/*
 * Copyright (c) 2016 by Delphix. All rights reserved.
 */

/*
 * Fletcher Checksums
 * ------------------
 *
 * ZFS's 2nd and 4th order Fletcher checksums are defined by the following
 * recurrence relations:
 *
 *	a  = a    + f
 *	 i    i-1    i-1
 *
 *	b  = b    + a
 *	 i    i-1    i
 *
 *	c  = c    + b		(fletcher-4 only)
 *	 i    i-1    i
 *
 *	d  = d    + c		(fletcher-4 only)
 *	 i    i-1    i
 *
 * Where
 *	a_0 = b_0 = c_0 = d_0 = 0
 * and
 *	f_0 .. f_(n-1) are the input data.
 *
 * Using standard techniques, these translate into the following series:
 *
 *	     __n_			     __n_
 *	     \   |			     \   |
 *	a  =  >     f			b  =  >     i * f
 *	 n   /___|   n - i		 n   /___|	 n - i
 *	     i = 1			     i = 1
 *
 *
 *	     __n_			     __n_
 *	     \   |  i*(i+1)		     \   |  i*(i+1)*(i+2)
 *	c  =  >     ------- f		d  =  >     ------------- f
 *	 n   /___|     2     n - i	 n   /___|	  6	   n - i
 *	     i = 1			     i = 1
 *
 * For fletcher-2, the f_is are 64-bit, and [ab]_i are 64-bit accumulators.
 * Since the additions are done mod (2^64), errors in the high bits may not
 * be noticed.  For this reason, fletcher-2 is deprecated.
 *
 * For fletcher-4, the f_is are 32-bit, and [abcd]_i are 64-bit accumulators.
 * A conservative estimate of how big the buffer can get before we overflow
 * can be estimated using f_i = 0xffffffff for all i:
 *
 * % bc
 *  f=2^32-1;d=0; for (i = 1; d<2^64; i++) { d += f*i*(i+1)*(i+2)/6 }; (i-1)*4
 * 2264
 *  quit
 * %
 *
 * So blocks of up to 2k will not overflow.  Our largest block size is
 * 128k, which has 32k 4-byte words, so we can compute the largest possible
 * accumulators, then divide by 2^64 to figure the max amount of overflow:
 *
 * % bc
 *  a=b=c=d=0; f=2^32-1; for (i=1; i<=32*1024; i++) { a+=f; b+=a; c+=b; d+=c }
 *  a/2^64;b/2^64;c/2^64;d/2^64
 * 0
 * 0
 * 1365
 * 11186858
 *  quit
 * %
 *
 * So a and b cannot overflow.  To make sure each bit of input has some
 * effect on the contents of c and d, we can look at what the factors of
 * the coefficients in the equations for c_n and d_n are.  The number of 2s
 * in the factors determines the lowest set bit in the multiplier.  Running
 * through the cases for n*(n+1)/2 reveals that the highest power of 2 is
 * 2^14, and for n*(n+1)*(n+2)/6 it is 2^15.  So while some data may overflow
 * the 64-bit accumulators, every bit of every f_i effects every accumulator,
 * even for 128k blocks.
 *
 * If we wanted to make a stronger version of fletcher4 (fletcher4c?),
 * we could do our calculations mod (2^32 - 1) by adding in the carries
 * periodically, and store the number of carries in the top 32-bits.
 *
 * --------------------
 * Checksum Performance
 * --------------------
 *
 * There are two interesting components to checksum performance: cached and
 * uncached performance.  With cached data, fletcher-2 is about four times
 * faster than fletcher-4.  With uncached data, the performance difference is
 * negligible, since the cost of a cache fill dominates the processing time.
 * Even though fletcher-4 is slower than fletcher-2, it is still a pretty
 * efficient pass over the data.
 *
 * In normal operation, the data which is being checksummed is in a buffer
 * which has been filled either by:
 *
 *	1. a compression step, which will be mostly cached, or
 *	2. a bcopy() or copyin(), which will be uncached (because the
 *	   copy is cache-bypassing).
 *
 * For both cached and uncached data, both fletcher checksums are much faster
 * than sha-256, and slower than 'off', which doesn't touch the data at all.
 */

#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/byteorder.h>
#include <sys/spa.h>
#include <sys/zio_checksum.h>
#include <sys/zfs_context.h>
#include <zfs_fletcher.h>

#define	FLETCHER_MIN_SIMD_SIZE	64

static void fletcher_4_scalar_init(fletcher_4_ctx_t *ctx);
static void fletcher_4_scalar_fini(fletcher_4_ctx_t *ctx, zio_cksum_t *zcp);
static void fletcher_4_scalar_native(fletcher_4_ctx_t *ctx,
    const void *buf, uint64_t size);
static void fletcher_4_scalar_byteswap(fletcher_4_ctx_t *ctx,
    const void *buf, uint64_t size);
static boolean_t fletcher_4_scalar_valid(void);

static const fletcher_4_ops_t fletcher_4_scalar_ops = {
	.init_native = fletcher_4_scalar_init,
	.fini_native = fletcher_4_scalar_fini,
	.compute_native = fletcher_4_scalar_native,
	.init_byteswap = fletcher_4_scalar_init,
	.fini_byteswap = fletcher_4_scalar_fini,
	.compute_byteswap = fletcher_4_scalar_byteswap,
	.valid = fletcher_4_scalar_valid,
	.name = "scalar"
};

static fletcher_4_ops_t fletcher_4_fastest_impl = {
	.name = "fastest",
	.valid = fletcher_4_scalar_valid
};

static const fletcher_4_ops_t *fletcher_4_impls[] = {
	&fletcher_4_scalar_ops,
	&fletcher_4_superscalar_ops,
	&fletcher_4_superscalar4_ops,
#if defined(HAVE_SSE2)
	&fletcher_4_sse2_ops,
#endif
#if defined(HAVE_SSE2) && defined(HAVE_SSSE3)
	&fletcher_4_ssse3_ops,
#endif
#if defined(HAVE_AVX) && defined(HAVE_AVX2)
	&fletcher_4_avx2_ops,
#endif
#if defined(__x86_64) && defined(HAVE_AVX512F)
	&fletcher_4_avx512f_ops,
#endif
#if defined(__aarch64__)
	&fletcher_4_aarch64_neon_ops,
#endif
};

/* Hold all supported implementations */
static uint32_t fletcher_4_supp_impls_cnt = 0;
static fletcher_4_ops_t *fletcher_4_supp_impls[ARRAY_SIZE(fletcher_4_impls)];

/* Select fletcher4 implementation */
#define	IMPL_FASTEST	(UINT32_MAX)
#define	IMPL_CYCLE	(UINT32_MAX - 1)
#define	IMPL_SCALAR	(0)

static uint32_t fletcher_4_impl_chosen = IMPL_FASTEST;

#define	IMPL_READ(i)	(*(volatile uint32_t *) &(i))

static struct fletcher_4_impl_selector {
	const char	*fis_name;
	uint32_t	fis_sel;
} fletcher_4_impl_selectors[] = {
#if !defined(_KERNEL)
	{ "cycle",	IMPL_CYCLE },
#endif
	{ "fastest",	IMPL_FASTEST },
	{ "scalar",	IMPL_SCALAR }
};

static kstat_t *fletcher_4_kstat;

static struct fletcher_4_kstat {
	uint64_t native;
	uint64_t byteswap;
} fletcher_4_stat_data[ARRAY_SIZE(fletcher_4_impls) + 1];

/* Indicate that benchmark has been completed */
static boolean_t fletcher_4_initialized = B_FALSE;

/*ARGSUSED*/
void
fletcher_init(zio_cksum_t *zcp)
{
	ZIO_SET_CHECKSUM(zcp, 0, 0, 0, 0);
}

int
fletcher_2_incremental_native(void *buf, size_t size, void *data)
{
	zio_cksum_t *zcp = data;

	const uint64_t *ip = buf;
	const uint64_t *ipend = ip + (size / sizeof (uint64_t));
	uint64_t a0, b0, a1, b1;

	a0 = zcp->zc_word[0];
	a1 = zcp->zc_word[1];
	b0 = zcp->zc_word[2];
	b1 = zcp->zc_word[3];

	for (; ip < ipend; ip += 2) {
		a0 += ip[0];
		a1 += ip[1];
		b0 += a0;
		b1 += a1;
	}

	ZIO_SET_CHECKSUM(zcp, a0, a1, b0, b1);
	return (0);
}

/*ARGSUSED*/
void
fletcher_2_native(const void *buf, uint64_t size,
    const void *ctx_template, zio_cksum_t *zcp)
{
	fletcher_init(zcp);
	(void) fletcher_2_incremental_native((void *) buf, size, zcp);
}

int
fletcher_2_incremental_byteswap(void *buf, size_t size, void *data)
{
	zio_cksum_t *zcp = data;

	const uint64_t *ip = buf;
	const uint64_t *ipend = ip + (size / sizeof (uint64_t));
	uint64_t a0, b0, a1, b1;

	a0 = zcp->zc_word[0];
	a1 = zcp->zc_word[1];
	b0 = zcp->zc_word[2];
	b1 = zcp->zc_word[3];

	for (; ip < ipend; ip += 2) {
		a0 += BSWAP_64(ip[0]);
		a1 += BSWAP_64(ip[1]);
		b0 += a0;
		b1 += a1;
	}

	ZIO_SET_CHECKSUM(zcp, a0, a1, b0, b1);
	return (0);
}

/*ARGSUSED*/
void
fletcher_2_byteswap(const void *buf, uint64_t size,
    const void *ctx_template, zio_cksum_t *zcp)
{
	fletcher_init(zcp);
	(void) fletcher_2_incremental_byteswap((void *) buf, size, zcp);
}

static void
fletcher_4_scalar_init(fletcher_4_ctx_t *ctx)
{
	ZIO_SET_CHECKSUM(&ctx->scalar, 0, 0, 0, 0);
}

static void
fletcher_4_scalar_fini(fletcher_4_ctx_t *ctx, zio_cksum_t *zcp)
{
	memcpy(zcp, &ctx->scalar, sizeof (zio_cksum_t));
}

static void
fletcher_4_scalar_native(fletcher_4_ctx_t *ctx, const void *buf,
    uint64_t size)
{
	const uint32_t *ip = buf;
	const uint32_t *ipend = ip + (size / sizeof (uint32_t));
	uint64_t a, b, c, d;

	a = ctx->scalar.zc_word[0];
	b = ctx->scalar.zc_word[1];
	c = ctx->scalar.zc_word[2];
	d = ctx->scalar.zc_word[3];

	for (; ip < ipend; ip++) {
		a += ip[0];
		b += a;
		c += b;
		d += c;
	}

	ZIO_SET_CHECKSUM(&ctx->scalar, a, b, c, d);
}

static void
fletcher_4_scalar_byteswap(fletcher_4_ctx_t *ctx, const void *buf,
    uint64_t size)
{
	const uint32_t *ip = buf;
	const uint32_t *ipend = ip + (size / sizeof (uint32_t));
	uint64_t a, b, c, d;

	a = ctx->scalar.zc_word[0];
	b = ctx->scalar.zc_word[1];
	c = ctx->scalar.zc_word[2];
	d = ctx->scalar.zc_word[3];

	for (; ip < ipend; ip++) {
		a += BSWAP_32(ip[0]);
		b += a;
		c += b;
		d += c;
	}

	ZIO_SET_CHECKSUM(&ctx->scalar, a, b, c, d);
}

static boolean_t
fletcher_4_scalar_valid(void)
{
	return (B_TRUE);
}

int
fletcher_4_impl_set(const char *val)
{
	int err = -EINVAL;
	uint32_t impl = IMPL_READ(fletcher_4_impl_chosen);
	size_t i, val_len;

	val_len = strlen(val);
	while ((val_len > 0) && !!isspace(val[val_len-1])) /* trim '\n' */
		val_len--;

	/* check mandatory implementations */
	for (i = 0; i < ARRAY_SIZE(fletcher_4_impl_selectors); i++) {
		const char *name = fletcher_4_impl_selectors[i].fis_name;

		if (val_len == strlen(name) &&
		    strncmp(val, name, val_len) == 0) {
			impl = fletcher_4_impl_selectors[i].fis_sel;
			err = 0;
			break;
		}
	}

	if (err != 0 && fletcher_4_initialized) {
		/* check all supported implementations */
		for (i = 0; i < fletcher_4_supp_impls_cnt; i++) {
			const char *name = fletcher_4_supp_impls[i]->name;

			if (val_len == strlen(name) &&
			    strncmp(val, name, val_len) == 0) {
				impl = i;
				err = 0;
				break;
			}
		}
	}

	if (err == 0) {
		atomic_swap_32(&fletcher_4_impl_chosen, impl);
		membar_producer();
	}

	return (err);
}

static inline const fletcher_4_ops_t *
fletcher_4_impl_get(void)
{
	fletcher_4_ops_t *ops = NULL;
	const uint32_t impl = IMPL_READ(fletcher_4_impl_chosen);

	switch (impl) {
	case IMPL_FASTEST:
		ASSERT(fletcher_4_initialized);
		ops = &fletcher_4_fastest_impl;
		break;
#if !defined(_KERNEL)
	case IMPL_CYCLE: {
		ASSERT(fletcher_4_initialized);
		ASSERT3U(fletcher_4_supp_impls_cnt, >, 0);

		static uint32_t cycle_count = 0;
		uint32_t idx = (++cycle_count) % fletcher_4_supp_impls_cnt;
		ops = fletcher_4_supp_impls[idx];
	}
	break;
#endif
	default:
		ASSERT3U(fletcher_4_supp_impls_cnt, >, 0);
		ASSERT3U(impl, <, fletcher_4_supp_impls_cnt);

		ops = fletcher_4_supp_impls[impl];
		break;
	}

	ASSERT3P(ops, !=, NULL);

	return (ops);
}

static inline void
fletcher_4_native_impl(const void *buf, uint64_t size, zio_cksum_t *zcp)
{
	fletcher_4_ctx_t ctx;
	const fletcher_4_ops_t *ops = fletcher_4_impl_get();

	ops->init_native(&ctx);
	ops->compute_native(&ctx, buf, size);
	ops->fini_native(&ctx, zcp);
}

/*ARGSUSED*/
void
fletcher_4_native(const void *buf, uint64_t size,
    const void *ctx_template, zio_cksum_t *zcp)
{
	const uint64_t p2size = P2ALIGN(size, FLETCHER_MIN_SIMD_SIZE);

	ASSERT(IS_P2ALIGNED(size, sizeof (uint32_t)));

	if (size == 0 || p2size == 0) {
		ZIO_SET_CHECKSUM(zcp, 0, 0, 0, 0);

		if (size > 0)
			fletcher_4_scalar_native((fletcher_4_ctx_t *)zcp,
			    buf, size);
	} else {
		fletcher_4_native_impl(buf, p2size, zcp);

		if (p2size < size)
			fletcher_4_scalar_native((fletcher_4_ctx_t *)zcp,
			    (char *)buf + p2size, size - p2size);
	}
}

void
fletcher_4_native_varsize(const void *buf, uint64_t size, zio_cksum_t *zcp)
{
	ZIO_SET_CHECKSUM(zcp, 0, 0, 0, 0);
	fletcher_4_scalar_native((fletcher_4_ctx_t *)zcp, buf, size);
}

static inline void
fletcher_4_byteswap_impl(const void *buf, uint64_t size, zio_cksum_t *zcp)
{
	fletcher_4_ctx_t ctx;
	const fletcher_4_ops_t *ops = fletcher_4_impl_get();

	ops->init_byteswap(&ctx);
	ops->compute_byteswap(&ctx, buf, size);
	ops->fini_byteswap(&ctx, zcp);
}

/*ARGSUSED*/
void
fletcher_4_byteswap(const void *buf, uint64_t size,
    const void *ctx_template, zio_cksum_t *zcp)
{
	const uint64_t p2size = P2ALIGN(size, FLETCHER_MIN_SIMD_SIZE);

	ASSERT(IS_P2ALIGNED(size, sizeof (uint32_t)));

	if (size == 0 || p2size == 0) {
		ZIO_SET_CHECKSUM(zcp, 0, 0, 0, 0);

		if (size > 0)
			fletcher_4_scalar_byteswap((fletcher_4_ctx_t *)zcp,
			    buf, size);
	} else {
		fletcher_4_byteswap_impl(buf, p2size, zcp);

		if (p2size < size)
			fletcher_4_scalar_byteswap((fletcher_4_ctx_t *)zcp,
			    (char *)buf + p2size, size - p2size);
	}
}

/* Incremental Fletcher 4 */

#define	ZFS_FLETCHER_4_INC_MAX_SIZE	(8ULL << 20)

static inline void
fletcher_4_incremental_combine(zio_cksum_t *zcp, const uint64_t size,
    const zio_cksum_t *nzcp)
{
	const uint64_t c1 = size / sizeof (uint32_t);
	const uint64_t c2 = c1 * (c1 + 1) / 2;
	const uint64_t c3 = c2 * (c1 + 2) / 3;

	/*
	 * Value of 'c3' overflows on buffer sizes close to 16MiB. For that
	 * reason we split incremental fletcher4 computation of large buffers
	 * to steps of (ZFS_FLETCHER_4_INC_MAX_SIZE) size.
	 */
	ASSERT3U(size, <=, ZFS_FLETCHER_4_INC_MAX_SIZE);

	zcp->zc_word[3] += nzcp->zc_word[3] + c1 * zcp->zc_word[2] +
	    c2 * zcp->zc_word[1] + c3 * zcp->zc_word[0];
	zcp->zc_word[2] += nzcp->zc_word[2] + c1 * zcp->zc_word[1] +
	    c2 * zcp->zc_word[0];
	zcp->zc_word[1] += nzcp->zc_word[1] + c1 * zcp->zc_word[0];
	zcp->zc_word[0] += nzcp->zc_word[0];
}

static inline void
fletcher_4_incremental_impl(boolean_t native, const void *buf, uint64_t size,
    zio_cksum_t *zcp)
{
	while (size > 0) {
		zio_cksum_t nzc;
		uint64_t len = MIN(size, ZFS_FLETCHER_4_INC_MAX_SIZE);

		if (native)
			fletcher_4_native(buf, len, NULL, &nzc);
		else
			fletcher_4_byteswap(buf, len, NULL, &nzc);

		fletcher_4_incremental_combine(zcp, len, &nzc);

		size -= len;
		buf += len;
	}
}

int
fletcher_4_incremental_native(void *buf, size_t size, void *data)
{
	zio_cksum_t *zcp = data;
	/* Use scalar impl to directly update cksum of small blocks */
	if (size < SPA_MINBLOCKSIZE)
		fletcher_4_scalar_native((fletcher_4_ctx_t *)zcp, buf, size);
	else
		fletcher_4_incremental_impl(B_TRUE, buf, size, zcp);
	return (0);
}

int
fletcher_4_incremental_byteswap(void *buf, size_t size, void *data)
{
	zio_cksum_t *zcp = data;
	/* Use scalar impl to directly update cksum of small blocks */
	if (size < SPA_MINBLOCKSIZE)
		fletcher_4_scalar_byteswap((fletcher_4_ctx_t *)zcp, buf, size);
	else
		fletcher_4_incremental_impl(B_FALSE, buf, size, zcp);
	return (0);
}


/* Fletcher 4 kstats */

static int
fletcher_4_kstat_headers(char *buf, size_t size)
{
	ssize_t off = 0;

	off += snprintf(buf + off, size, "%-17s", "implementation");
	off += snprintf(buf + off, size - off, "%-15s", "native");
	(void) snprintf(buf + off, size - off, "%-15s\n", "byteswap");

	return (0);
}

static int
fletcher_4_kstat_data(char *buf, size_t size, void *data)
{
	struct fletcher_4_kstat *fastest_stat =
	    &fletcher_4_stat_data[fletcher_4_supp_impls_cnt];
	struct fletcher_4_kstat *curr_stat = (struct fletcher_4_kstat *)data;
	ssize_t off = 0;

	if (curr_stat == fastest_stat) {
		off += snprintf(buf + off, size - off, "%-17s", "fastest");
		off += snprintf(buf + off, size - off, "%-15s",
		    fletcher_4_supp_impls[fastest_stat->native]->name);
		off += snprintf(buf + off, size - off, "%-15s\n",
		    fletcher_4_supp_impls[fastest_stat->byteswap]->name);
	} else {
		ptrdiff_t id = curr_stat - fletcher_4_stat_data;

		off += snprintf(buf + off, size - off, "%-17s",
		    fletcher_4_supp_impls[id]->name);
		off += snprintf(buf + off, size - off, "%-15llu",
		    (u_longlong_t)curr_stat->native);
		off += snprintf(buf + off, size - off, "%-15llu\n",
		    (u_longlong_t)curr_stat->byteswap);
	}

	return (0);
}

static void *
fletcher_4_kstat_addr(kstat_t *ksp, loff_t n)
{
	if (n <= fletcher_4_supp_impls_cnt)
		ksp->ks_private = (void *) (fletcher_4_stat_data + n);
	else
		ksp->ks_private = NULL;

	return (ksp->ks_private);
}

#define	FLETCHER_4_FASTEST_FN_COPY(type, src)				  \
{									  \
	fletcher_4_fastest_impl.init_ ## type = src->init_ ## type;	  \
	fletcher_4_fastest_impl.fini_ ## type = src->fini_ ## type;	  \
	fletcher_4_fastest_impl.compute_ ## type = src->compute_ ## type; \
}

#define	FLETCHER_4_BENCH_NS	(MSEC2NSEC(50))		/* 50ms */

typedef void fletcher_checksum_func_t(const void *, uint64_t, const void *,
					zio_cksum_t *);

static void
fletcher_4_benchmark_impl(boolean_t native, char *data, uint64_t data_size)
{

	struct fletcher_4_kstat *fastest_stat =
	    &fletcher_4_stat_data[fletcher_4_supp_impls_cnt];
	hrtime_t start;
	uint64_t run_bw, run_time_ns, best_run = 0;
	zio_cksum_t zc;
	uint32_t i, l, sel_save = IMPL_READ(fletcher_4_impl_chosen);


	fletcher_checksum_func_t *fletcher_4_test = native ?
	    fletcher_4_native : fletcher_4_byteswap;

	for (i = 0; i < fletcher_4_supp_impls_cnt; i++) {
		struct fletcher_4_kstat *stat = &fletcher_4_stat_data[i];
		uint64_t run_count = 0;

		/* temporary set an implementation */
		fletcher_4_impl_chosen = i;

		kpreempt_disable();
		start = gethrtime();
		do {
			for (l = 0; l < 32; l++, run_count++)
				fletcher_4_test(data, data_size, NULL, &zc);

			run_time_ns = gethrtime() - start;
		} while (run_time_ns < FLETCHER_4_BENCH_NS);
		kpreempt_enable();

		run_bw = data_size * run_count * NANOSEC;
		run_bw /= run_time_ns;	/* B/s */

		if (native)
			stat->native = run_bw;
		else
			stat->byteswap = run_bw;

		if (run_bw > best_run) {
			best_run = run_bw;

			if (native) {
				fastest_stat->native = i;
				FLETCHER_4_FASTEST_FN_COPY(native,
				    fletcher_4_supp_impls[i]);
			} else {
				fastest_stat->byteswap = i;
				FLETCHER_4_FASTEST_FN_COPY(byteswap,
				    fletcher_4_supp_impls[i]);
			}
		}
	}

	/* restore original selection */
	atomic_swap_32(&fletcher_4_impl_chosen, sel_save);
}

void
fletcher_4_init(void)
{
	static const size_t data_size = 1 << SPA_OLD_MAXBLOCKSHIFT; /* 128kiB */
	fletcher_4_ops_t *curr_impl;
	char *databuf;
	int i, c;

	/* move supported impl into fletcher_4_supp_impls */
	for (i = 0, c = 0; i < ARRAY_SIZE(fletcher_4_impls); i++) {
		curr_impl = (fletcher_4_ops_t *)fletcher_4_impls[i];

		if (curr_impl->valid && curr_impl->valid())
			fletcher_4_supp_impls[c++] = curr_impl;
	}
	membar_producer();	/* complete fletcher_4_supp_impls[] init */
	fletcher_4_supp_impls_cnt = c;	/* number of supported impl */

#if !defined(_KERNEL)
	/* Skip benchmarking and use last implementation as fastest */
	memcpy(&fletcher_4_fastest_impl,
	    fletcher_4_supp_impls[fletcher_4_supp_impls_cnt-1],
	    sizeof (fletcher_4_fastest_impl));
	fletcher_4_fastest_impl.name = "fastest";
	membar_producer();

	fletcher_4_initialized = B_TRUE;
	return;
#endif
	/* Benchmark all supported implementations */
	databuf = vmem_alloc(data_size, KM_SLEEP);
	for (i = 0; i < data_size / sizeof (uint64_t); i++)
		((uint64_t *)databuf)[i] = (uintptr_t)(databuf+i); /* warm-up */

	fletcher_4_benchmark_impl(B_FALSE, databuf, data_size);
	fletcher_4_benchmark_impl(B_TRUE, databuf, data_size);

	vmem_free(databuf, data_size);

	/* install kstats for all implementations */
	fletcher_4_kstat = kstat_create("zfs", 0, "fletcher_4_bench", "misc",
	    KSTAT_TYPE_RAW, 0, KSTAT_FLAG_VIRTUAL);
	if (fletcher_4_kstat != NULL) {
		fletcher_4_kstat->ks_data = NULL;
		fletcher_4_kstat->ks_ndata = UINT32_MAX;
		kstat_set_raw_ops(fletcher_4_kstat,
		    fletcher_4_kstat_headers,
		    fletcher_4_kstat_data,
		    fletcher_4_kstat_addr);
		kstat_install(fletcher_4_kstat);
	}

	/* Finish initialization */
	fletcher_4_initialized = B_TRUE;
}

void
fletcher_4_fini(void)
{
	if (fletcher_4_kstat != NULL) {
		kstat_delete(fletcher_4_kstat);
		fletcher_4_kstat = NULL;
	}
}

/* ABD adapters */

static void
abd_fletcher_4_init(zio_abd_checksum_data_t *cdp)
{
	const fletcher_4_ops_t *ops = fletcher_4_impl_get();
	cdp->acd_private = (void *) ops;

	if (cdp->acd_byteorder == ZIO_CHECKSUM_NATIVE)
		ops->init_native(cdp->acd_ctx);
	else
		ops->init_byteswap(cdp->acd_ctx);
}

static void
abd_fletcher_4_fini(zio_abd_checksum_data_t *cdp)
{
	fletcher_4_ops_t *ops = (fletcher_4_ops_t *)cdp->acd_private;

	ASSERT(ops);

	if (cdp->acd_byteorder == ZIO_CHECKSUM_NATIVE)
		ops->fini_native(cdp->acd_ctx, cdp->acd_zcp);
	else
		ops->fini_byteswap(cdp->acd_ctx, cdp->acd_zcp);
}

static void
abd_fletcher_4_simd2scalar(boolean_t native, void *data, size_t size,
    zio_abd_checksum_data_t *cdp)
{
	zio_cksum_t *zcp = cdp->acd_zcp;

	ASSERT3U(size, <, FLETCHER_MIN_SIMD_SIZE);

	abd_fletcher_4_fini(cdp);
	cdp->acd_private = (void *)&fletcher_4_scalar_ops;

	if (native)
		fletcher_4_incremental_native(data, size, zcp);
	else
		fletcher_4_incremental_byteswap(data, size, zcp);
}

static int
abd_fletcher_4_iter(void *data, size_t size, void *private)
{
	zio_abd_checksum_data_t *cdp = (zio_abd_checksum_data_t *)private;
	fletcher_4_ctx_t *ctx = cdp->acd_ctx;
	fletcher_4_ops_t *ops = (fletcher_4_ops_t *)cdp->acd_private;
	boolean_t native = cdp->acd_byteorder == ZIO_CHECKSUM_NATIVE;
	uint64_t asize = P2ALIGN(size, FLETCHER_MIN_SIMD_SIZE);

	ASSERT(IS_P2ALIGNED(size, sizeof (uint32_t)));

	if (asize > 0) {
		if (native)
			ops->compute_native(ctx, data, asize);
		else
			ops->compute_byteswap(ctx, data, asize);

		size -= asize;
		data = (char *)data + asize;
	}

	if (size > 0) {
		ASSERT3U(size, <, FLETCHER_MIN_SIMD_SIZE);
		/* At this point we have to switch to scalar impl */
		abd_fletcher_4_simd2scalar(native, data, size, cdp);
	}

	return (0);
}

zio_abd_checksum_func_t fletcher_4_abd_ops = {
	.acf_init = abd_fletcher_4_init,
	.acf_fini = abd_fletcher_4_fini,
	.acf_iter = abd_fletcher_4_iter
};


#if defined(_KERNEL) && defined(HAVE_SPL)
#include <linux/mod_compat.h>

static int
fletcher_4_param_get(char *buffer, zfs_kernel_param_t *unused)
{
	const uint32_t impl = IMPL_READ(fletcher_4_impl_chosen);
	char *fmt;
	int i, cnt = 0;

	/* list fastest */
	fmt = (impl == IMPL_FASTEST) ? "[%s] " : "%s ";
	cnt += sprintf(buffer + cnt, fmt, "fastest");

	/* list all supported implementations */
	for (i = 0; i < fletcher_4_supp_impls_cnt; i++) {
		fmt = (i == impl) ? "[%s] " : "%s ";
		cnt += sprintf(buffer + cnt, fmt,
		    fletcher_4_supp_impls[i]->name);
	}

	return (cnt);
}

static int
fletcher_4_param_set(const char *val, zfs_kernel_param_t *unused)
{
	return (fletcher_4_impl_set(val));
}

/*
 * Choose a fletcher 4 implementation in ZFS.
 * Users can choose "cycle" to exercise all implementations, but this is
 * for testing purpose therefore it can only be set in user space.
 */
module_param_call(zfs_fletcher_4_impl,
    fletcher_4_param_set, fletcher_4_param_get, NULL, 0644);
MODULE_PARM_DESC(zfs_fletcher_4_impl, "Select fletcher 4 implementation.");

EXPORT_SYMBOL(fletcher_init);
EXPORT_SYMBOL(fletcher_2_incremental_native);
EXPORT_SYMBOL(fletcher_2_incremental_byteswap);
EXPORT_SYMBOL(fletcher_4_init);
EXPORT_SYMBOL(fletcher_4_fini);
EXPORT_SYMBOL(fletcher_2_native);
EXPORT_SYMBOL(fletcher_2_byteswap);
EXPORT_SYMBOL(fletcher_4_native);
EXPORT_SYMBOL(fletcher_4_native_varsize);
EXPORT_SYMBOL(fletcher_4_byteswap);
EXPORT_SYMBOL(fletcher_4_incremental_native);
EXPORT_SYMBOL(fletcher_4_incremental_byteswap);
EXPORT_SYMBOL(fletcher_4_abd_ops);
#endif
