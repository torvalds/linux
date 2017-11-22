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

#include <sys/zfs_context.h>
#include <sys/types.h>
#include <sys/zio.h>
#include <sys/debug.h>
#include <sys/zfs_debug.h>

#include <sys/vdev_raidz.h>
#include <sys/vdev_raidz_impl.h>

extern boolean_t raidz_will_scalar_work(void);

/* Opaque implementation with NULL methods to represent original methods */
static const raidz_impl_ops_t vdev_raidz_original_impl = {
	.name = "original",
	.is_supported = raidz_will_scalar_work,
};

/* RAIDZ parity op that contain the fastest methods */
static raidz_impl_ops_t vdev_raidz_fastest_impl = {
	.name = "fastest"
};

/* All compiled in implementations */
const raidz_impl_ops_t *raidz_all_maths[] = {
	&vdev_raidz_original_impl,
	&vdev_raidz_scalar_impl,
#if defined(__x86_64) && defined(HAVE_SSE2)	/* only x86_64 for now */
	&vdev_raidz_sse2_impl,
#endif
#if defined(__x86_64) && defined(HAVE_SSSE3)	/* only x86_64 for now */
	&vdev_raidz_ssse3_impl,
#endif
#if defined(__x86_64) && defined(HAVE_AVX2)	/* only x86_64 for now */
	&vdev_raidz_avx2_impl,
#endif
#if defined(__x86_64) && defined(HAVE_AVX512F)	/* only x86_64 for now */
	&vdev_raidz_avx512f_impl,
#endif
#if defined(__x86_64) && defined(HAVE_AVX512BW)	/* only x86_64 for now */
	&vdev_raidz_avx512bw_impl,
#endif
#if defined(__aarch64__)
	&vdev_raidz_aarch64_neon_impl,
	&vdev_raidz_aarch64_neonx2_impl,
#endif
};

/* Indicate that benchmark has been completed */
static boolean_t raidz_math_initialized = B_FALSE;

/* Select raidz implementation */
#define	IMPL_FASTEST	(UINT32_MAX)
#define	IMPL_CYCLE	(UINT32_MAX - 1)
#define	IMPL_ORIGINAL	(0)
#define	IMPL_SCALAR	(1)

#define	RAIDZ_IMPL_READ(i)	(*(volatile uint32_t *) &(i))

static uint32_t zfs_vdev_raidz_impl = IMPL_SCALAR;
static uint32_t user_sel_impl = IMPL_FASTEST;

/* Hold all supported implementations */
static size_t raidz_supp_impl_cnt = 0;
static raidz_impl_ops_t *raidz_supp_impl[ARRAY_SIZE(raidz_all_maths)];

/*
 * kstats values for supported implementations
 * Values represent per disk throughput of 8 disk+parity raidz vdev [B/s]
 */
static raidz_impl_kstat_t raidz_impl_kstats[ARRAY_SIZE(raidz_all_maths) + 1];

/* kstat for benchmarked implementations */
static kstat_t *raidz_math_kstat = NULL;

/*
 * Selects the raidz operation for raidz_map
 * If rm_ops is set to NULL original raidz implementation will be used
 */
raidz_impl_ops_t *
vdev_raidz_math_get_ops()
{
	raidz_impl_ops_t *ops = NULL;
	const uint32_t impl = RAIDZ_IMPL_READ(zfs_vdev_raidz_impl);

	switch (impl) {
	case IMPL_FASTEST:
		ASSERT(raidz_math_initialized);
		ops = &vdev_raidz_fastest_impl;
		break;
#if !defined(_KERNEL)
	case IMPL_CYCLE:
	{
		ASSERT(raidz_math_initialized);
		ASSERT3U(raidz_supp_impl_cnt, >, 0);
		/* Cycle through all supported implementations */
		static size_t cycle_impl_idx = 0;
		size_t idx = (++cycle_impl_idx) % raidz_supp_impl_cnt;
		ops = raidz_supp_impl[idx];
	}
	break;
#endif
	case IMPL_ORIGINAL:
		ops = (raidz_impl_ops_t *)&vdev_raidz_original_impl;
		break;
	case IMPL_SCALAR:
		ops = (raidz_impl_ops_t *)&vdev_raidz_scalar_impl;
		break;
	default:
		ASSERT3U(impl, <, raidz_supp_impl_cnt);
		ASSERT3U(raidz_supp_impl_cnt, >, 0);
		ops = raidz_supp_impl[impl];
		break;
	}

	ASSERT3P(ops, !=, NULL);

	return (ops);
}

/*
 * Select parity generation method for raidz_map
 */
int
vdev_raidz_math_generate(raidz_map_t *rm)
{
	raidz_gen_f gen_parity = NULL;

	switch (raidz_parity(rm)) {
		case 1:
			gen_parity = rm->rm_ops->gen[RAIDZ_GEN_P];
			break;
		case 2:
			gen_parity = rm->rm_ops->gen[RAIDZ_GEN_PQ];
			break;
		case 3:
			gen_parity = rm->rm_ops->gen[RAIDZ_GEN_PQR];
			break;
		default:
			gen_parity = NULL;
			cmn_err(CE_PANIC, "invalid RAID-Z configuration %d",
			    raidz_parity(rm));
			break;
	}

	/* if method is NULL execute the original implementation */
	if (gen_parity == NULL)
		return (RAIDZ_ORIGINAL_IMPL);

	gen_parity(rm);

	return (0);
}

static raidz_rec_f
reconstruct_fun_p_sel(raidz_map_t *rm, const int *parity_valid,
    const int nbaddata)
{
	if (nbaddata == 1 && parity_valid[CODE_P]) {
		return (rm->rm_ops->rec[RAIDZ_REC_P]);
	}
	return ((raidz_rec_f) NULL);
}

static raidz_rec_f
reconstruct_fun_pq_sel(raidz_map_t *rm, const int *parity_valid,
    const int nbaddata)
{
	if (nbaddata == 1) {
		if (parity_valid[CODE_P]) {
			return (rm->rm_ops->rec[RAIDZ_REC_P]);
		} else if (parity_valid[CODE_Q]) {
			return (rm->rm_ops->rec[RAIDZ_REC_Q]);
		}
	} else if (nbaddata == 2 &&
	    parity_valid[CODE_P] && parity_valid[CODE_Q]) {
		return (rm->rm_ops->rec[RAIDZ_REC_PQ]);
	}
	return ((raidz_rec_f) NULL);
}

static raidz_rec_f
reconstruct_fun_pqr_sel(raidz_map_t *rm, const int *parity_valid,
    const int nbaddata)
{
	if (nbaddata == 1) {
		if (parity_valid[CODE_P]) {
			return (rm->rm_ops->rec[RAIDZ_REC_P]);
		} else if (parity_valid[CODE_Q]) {
			return (rm->rm_ops->rec[RAIDZ_REC_Q]);
		} else if (parity_valid[CODE_R]) {
			return (rm->rm_ops->rec[RAIDZ_REC_R]);
		}
	} else if (nbaddata == 2) {
		if (parity_valid[CODE_P] && parity_valid[CODE_Q]) {
			return (rm->rm_ops->rec[RAIDZ_REC_PQ]);
		} else if (parity_valid[CODE_P] && parity_valid[CODE_R]) {
			return (rm->rm_ops->rec[RAIDZ_REC_PR]);
		} else if (parity_valid[CODE_Q] && parity_valid[CODE_R]) {
			return (rm->rm_ops->rec[RAIDZ_REC_QR]);
		}
	} else if (nbaddata == 3 &&
	    parity_valid[CODE_P] && parity_valid[CODE_Q] &&
	    parity_valid[CODE_R]) {
		return (rm->rm_ops->rec[RAIDZ_REC_PQR]);
	}
	return ((raidz_rec_f) NULL);
}

/*
 * Select data reconstruction method for raidz_map
 * @parity_valid - Parity validity flag
 * @dt           - Failed data index array
 * @nbaddata     - Number of failed data columns
 */
int
vdev_raidz_math_reconstruct(raidz_map_t *rm, const int *parity_valid,
    const int *dt, const int nbaddata)
{
	raidz_rec_f rec_fn = NULL;

	switch (raidz_parity(rm)) {
	case PARITY_P:
		rec_fn = reconstruct_fun_p_sel(rm, parity_valid, nbaddata);
		break;
	case PARITY_PQ:
		rec_fn = reconstruct_fun_pq_sel(rm, parity_valid, nbaddata);
		break;
	case PARITY_PQR:
		rec_fn = reconstruct_fun_pqr_sel(rm, parity_valid, nbaddata);
		break;
	default:
		cmn_err(CE_PANIC, "invalid RAID-Z configuration %d",
		    raidz_parity(rm));
		break;
	}

	if (rec_fn == NULL)
		return (RAIDZ_ORIGINAL_IMPL);
	else
		return (rec_fn(rm, dt));
}

const char *raidz_gen_name[] = {
	"gen_p", "gen_pq", "gen_pqr"
};
const char *raidz_rec_name[] = {
	"rec_p", "rec_q", "rec_r",
	"rec_pq", "rec_pr", "rec_qr", "rec_pqr"
};

#define	RAIDZ_KSTAT_LINE_LEN	(17 + 10*12 + 1)

static int
raidz_math_kstat_headers(char *buf, size_t size)
{
	int i;
	ssize_t off;

	ASSERT3U(size, >=, RAIDZ_KSTAT_LINE_LEN);

	off = snprintf(buf, size, "%-17s", "implementation");

	for (i = 0; i < ARRAY_SIZE(raidz_gen_name); i++)
		off += snprintf(buf + off, size - off, "%-16s",
		    raidz_gen_name[i]);

	for (i = 0; i < ARRAY_SIZE(raidz_rec_name); i++)
		off += snprintf(buf + off, size - off, "%-16s",
		    raidz_rec_name[i]);

	(void) snprintf(buf + off, size - off, "\n");

	return (0);
}

static int
raidz_math_kstat_data(char *buf, size_t size, void *data)
{
	raidz_impl_kstat_t *fstat = &raidz_impl_kstats[raidz_supp_impl_cnt];
	raidz_impl_kstat_t *cstat = (raidz_impl_kstat_t *)data;
	ssize_t off = 0;
	int i;

	ASSERT3U(size, >=, RAIDZ_KSTAT_LINE_LEN);

	if (cstat == fstat) {
		off += snprintf(buf + off, size - off, "%-17s", "fastest");

		for (i = 0; i < ARRAY_SIZE(raidz_gen_name); i++) {
			int id = fstat->gen[i];
			off += snprintf(buf + off, size - off, "%-16s",
			    raidz_supp_impl[id]->name);
		}
		for (i = 0; i < ARRAY_SIZE(raidz_rec_name); i++) {
			int id = fstat->rec[i];
			off += snprintf(buf + off, size - off, "%-16s",
			    raidz_supp_impl[id]->name);
		}
	} else {
		ptrdiff_t id = cstat - raidz_impl_kstats;

		off += snprintf(buf + off, size - off, "%-17s",
		    raidz_supp_impl[id]->name);

		for (i = 0; i < ARRAY_SIZE(raidz_gen_name); i++)
			off += snprintf(buf + off, size - off, "%-16llu",
			    (u_longlong_t)cstat->gen[i]);

		for (i = 0; i < ARRAY_SIZE(raidz_rec_name); i++)
			off += snprintf(buf + off, size - off, "%-16llu",
			    (u_longlong_t)cstat->rec[i]);
	}

	(void) snprintf(buf + off, size - off, "\n");

	return (0);
}

static void *
raidz_math_kstat_addr(kstat_t *ksp, loff_t n)
{
	if (n <= raidz_supp_impl_cnt)
		ksp->ks_private = (void *) (raidz_impl_kstats + n);
	else
		ksp->ks_private = NULL;

	return (ksp->ks_private);
}

#define	BENCH_D_COLS	(8ULL)
#define	BENCH_COLS	(BENCH_D_COLS + PARITY_PQR)
#define	BENCH_ZIO_SIZE	(1ULL << SPA_OLD_MAXBLOCKSHIFT)	/* 128 kiB */
#define	BENCH_NS	MSEC2NSEC(25)			/* 25ms */

typedef void (*benchmark_fn)(raidz_map_t *rm, const int fn);

static void
benchmark_gen_impl(raidz_map_t *rm, const int fn)
{
	(void) fn;
	vdev_raidz_generate_parity(rm);
}

static void
benchmark_rec_impl(raidz_map_t *rm, const int fn)
{
	static const int rec_tgt[7][3] = {
		{1, 2, 3},	/* rec_p:   bad QR & D[0]	*/
		{0, 2, 3},	/* rec_q:   bad PR & D[0]	*/
		{0, 1, 3},	/* rec_r:   bad PQ & D[0]	*/
		{2, 3, 4},	/* rec_pq:  bad R  & D[0][1]	*/
		{1, 3, 4},	/* rec_pr:  bad Q  & D[0][1]	*/
		{0, 3, 4},	/* rec_qr:  bad P  & D[0][1]	*/
		{3, 4, 5}	/* rec_pqr: bad    & D[0][1][2] */
	};

	vdev_raidz_reconstruct(rm, rec_tgt[fn], 3);
}

/*
 * Benchmarking of all supported implementations (raidz_supp_impl_cnt)
 * is performed by setting the rm_ops pointer and calling the top level
 * generate/reconstruct methods of bench_rm.
 */
static void
benchmark_raidz_impl(raidz_map_t *bench_rm, const int fn, benchmark_fn bench_fn)
{
	uint64_t run_cnt, speed, best_speed = 0;
	hrtime_t t_start, t_diff;
	raidz_impl_ops_t *curr_impl;
	raidz_impl_kstat_t *fstat = &raidz_impl_kstats[raidz_supp_impl_cnt];
	int impl, i;

	for (impl = 0; impl < raidz_supp_impl_cnt; impl++) {
		/* set an implementation to benchmark */
		curr_impl = raidz_supp_impl[impl];
		bench_rm->rm_ops = curr_impl;

		run_cnt = 0;
		t_start = gethrtime();

		do {
			for (i = 0; i < 25; i++, run_cnt++)
				bench_fn(bench_rm, fn);

			t_diff = gethrtime() - t_start;
		} while (t_diff < BENCH_NS);

		speed = run_cnt * BENCH_ZIO_SIZE * NANOSEC;
		speed /= (t_diff * BENCH_COLS);

		if (bench_fn == benchmark_gen_impl)
			raidz_impl_kstats[impl].gen[fn] = speed;
		else
			raidz_impl_kstats[impl].rec[fn] = speed;

		/* Update fastest implementation method */
		if (speed > best_speed) {
			best_speed = speed;

			if (bench_fn == benchmark_gen_impl) {
				fstat->gen[fn] = impl;
				vdev_raidz_fastest_impl.gen[fn] =
				    curr_impl->gen[fn];
			} else {
				fstat->rec[fn] = impl;
				vdev_raidz_fastest_impl.rec[fn] =
				    curr_impl->rec[fn];
			}
		}
	}
}

void
vdev_raidz_math_init(void)
{
	raidz_impl_ops_t *curr_impl;
	zio_t *bench_zio = NULL;
	raidz_map_t *bench_rm = NULL;
	uint64_t bench_parity;
	int i, c, fn;

	/* move supported impl into raidz_supp_impl */
	for (i = 0, c = 0; i < ARRAY_SIZE(raidz_all_maths); i++) {
		curr_impl = (raidz_impl_ops_t *)raidz_all_maths[i];

		/* initialize impl */
		if (curr_impl->init)
			curr_impl->init();

		if (curr_impl->is_supported())
			raidz_supp_impl[c++] = (raidz_impl_ops_t *)curr_impl;
	}
	membar_producer();		/* complete raidz_supp_impl[] init */
	raidz_supp_impl_cnt = c;	/* number of supported impl */

#if !defined(_KERNEL)
	/* Skip benchmarking and use last implementation as fastest */
	memcpy(&vdev_raidz_fastest_impl, raidz_supp_impl[raidz_supp_impl_cnt-1],
	    sizeof (vdev_raidz_fastest_impl));
	strcpy(vdev_raidz_fastest_impl.name, "fastest");

	raidz_math_initialized = B_TRUE;

	/* Use 'cycle' math selection method for userspace */
	VERIFY0(vdev_raidz_impl_set("cycle"));
	return;
#endif

	/* Fake an zio and run the benchmark on a warmed up buffer */
	bench_zio = kmem_zalloc(sizeof (zio_t), KM_SLEEP);
	bench_zio->io_offset = 0;
	bench_zio->io_size = BENCH_ZIO_SIZE; /* only data columns */
	bench_zio->io_abd = abd_alloc_linear(BENCH_ZIO_SIZE, B_TRUE);
	memset(abd_to_buf(bench_zio->io_abd), 0xAA, BENCH_ZIO_SIZE);

	/* Benchmark parity generation methods */
	for (fn = 0; fn < RAIDZ_GEN_NUM; fn++) {
		bench_parity = fn + 1;
		/* New raidz_map is needed for each generate_p/q/r */
		bench_rm = vdev_raidz_map_alloc(bench_zio, SPA_MINBLOCKSHIFT,
		    BENCH_D_COLS + bench_parity, bench_parity);

		benchmark_raidz_impl(bench_rm, fn, benchmark_gen_impl);

		vdev_raidz_map_free(bench_rm);
	}

	/* Benchmark data reconstruction methods */
	bench_rm = vdev_raidz_map_alloc(bench_zio, SPA_MINBLOCKSHIFT,
	    BENCH_COLS, PARITY_PQR);

	for (fn = 0; fn < RAIDZ_REC_NUM; fn++)
		benchmark_raidz_impl(bench_rm, fn, benchmark_rec_impl);

	vdev_raidz_map_free(bench_rm);

	/* cleanup the bench zio */
	abd_free(bench_zio->io_abd);
	kmem_free(bench_zio, sizeof (zio_t));

	/* install kstats for all impl */
	raidz_math_kstat = kstat_create("zfs", 0, "vdev_raidz_bench", "misc",
	    KSTAT_TYPE_RAW, 0, KSTAT_FLAG_VIRTUAL);

	if (raidz_math_kstat != NULL) {
		raidz_math_kstat->ks_data = NULL;
		raidz_math_kstat->ks_ndata = UINT32_MAX;
		kstat_set_raw_ops(raidz_math_kstat,
		    raidz_math_kstat_headers,
		    raidz_math_kstat_data,
		    raidz_math_kstat_addr);
		kstat_install(raidz_math_kstat);
	}

	/* Finish initialization */
	atomic_swap_32(&zfs_vdev_raidz_impl, user_sel_impl);
	raidz_math_initialized = B_TRUE;
}

void
vdev_raidz_math_fini(void)
{
	raidz_impl_ops_t const *curr_impl;
	int i;

	if (raidz_math_kstat != NULL) {
		kstat_delete(raidz_math_kstat);
		raidz_math_kstat = NULL;
	}

	/* fini impl */
	for (i = 0; i < ARRAY_SIZE(raidz_all_maths); i++) {
		curr_impl = raidz_all_maths[i];
		if (curr_impl->fini)
			curr_impl->fini();
	}
}

static const struct {
	char *name;
	uint32_t sel;
} math_impl_opts[] = {
#if !defined(_KERNEL)
		{ "cycle",	IMPL_CYCLE },
#endif
		{ "fastest",	IMPL_FASTEST },
		{ "original",	IMPL_ORIGINAL },
		{ "scalar",	IMPL_SCALAR }
};

/*
 * Function sets desired raidz implementation.
 *
 * If we are called before init(), user preference will be saved in
 * user_sel_impl, and applied in later init() call. This occurs when module
 * parameter is specified on module load. Otherwise, directly update
 * zfs_vdev_raidz_impl.
 *
 * @val		Name of raidz implementation to use
 * @param	Unused.
 */
int
vdev_raidz_impl_set(const char *val)
{
	int err = -EINVAL;
	char req_name[RAIDZ_IMPL_NAME_MAX];
	uint32_t impl = RAIDZ_IMPL_READ(user_sel_impl);
	size_t i;

	/* sanitize input */
	i = strnlen(val, RAIDZ_IMPL_NAME_MAX);
	if (i == 0 || i == RAIDZ_IMPL_NAME_MAX)
		return (err);

	strlcpy(req_name, val, RAIDZ_IMPL_NAME_MAX);
	while (i > 0 && !!isspace(req_name[i-1]))
		i--;
	req_name[i] = '\0';

	/* Check mandatory options */
	for (i = 0; i < ARRAY_SIZE(math_impl_opts); i++) {
		if (strcmp(req_name, math_impl_opts[i].name) == 0) {
			impl = math_impl_opts[i].sel;
			err = 0;
			break;
		}
	}

	/* check all supported impl if init() was already called */
	if (err != 0 && raidz_math_initialized) {
		/* check all supported implementations */
		for (i = 0; i < raidz_supp_impl_cnt; i++) {
			if (strcmp(req_name, raidz_supp_impl[i]->name) == 0) {
				impl = i;
				err = 0;
				break;
			}
		}
	}

	if (err == 0) {
		if (raidz_math_initialized)
			atomic_swap_32(&zfs_vdev_raidz_impl, impl);
		else
			atomic_swap_32(&user_sel_impl, impl);
	}

	return (err);
}

#if defined(_KERNEL) && defined(HAVE_SPL)
#include <linux/mod_compat.h>

static int
zfs_vdev_raidz_impl_set(const char *val, zfs_kernel_param_t *kp)
{
	return (vdev_raidz_impl_set(val));
}

static int
zfs_vdev_raidz_impl_get(char *buffer, zfs_kernel_param_t *kp)
{
	int i, cnt = 0;
	char *fmt;
	const uint32_t impl = RAIDZ_IMPL_READ(zfs_vdev_raidz_impl);

	ASSERT(raidz_math_initialized);

	/* list mandatory options */
	for (i = 0; i < ARRAY_SIZE(math_impl_opts) - 2; i++) {
		fmt = (impl == math_impl_opts[i].sel) ? "[%s] " : "%s ";
		cnt += sprintf(buffer + cnt, fmt, math_impl_opts[i].name);
	}

	/* list all supported implementations */
	for (i = 0; i < raidz_supp_impl_cnt; i++) {
		fmt = (i == impl) ? "[%s] " : "%s ";
		cnt += sprintf(buffer + cnt, fmt, raidz_supp_impl[i]->name);
	}

	return (cnt);
}

module_param_call(zfs_vdev_raidz_impl, zfs_vdev_raidz_impl_set,
    zfs_vdev_raidz_impl_get, NULL, 0644);
MODULE_PARM_DESC(zfs_vdev_raidz_impl, "Select raidz implementation.");
#endif
