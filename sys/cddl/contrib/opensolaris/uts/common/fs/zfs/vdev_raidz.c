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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2017 by Delphix. All rights reserved.
 * Copyright (c) 2013, Joyent, Inc. All rights reserved.
 * Copyright (c) 2014 Integros [integros.com]
 */

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/vdev_impl.h>
#ifdef illumos
#include <sys/vdev_disk.h>
#endif
#include <sys/vdev_file.h>
#include <sys/vdev_raidz.h>
#include <sys/zio.h>
#include <sys/zio_checksum.h>
#include <sys/abd.h>
#include <sys/fs/zfs.h>
#include <sys/fm/fs/zfs.h>
#include <sys/bio.h>

#ifdef ZFS_DEBUG
#include <sys/vdev_initialize.h>	/* vdev_xlate testing */
#endif

/*
 * Virtual device vector for RAID-Z.
 *
 * This vdev supports single, double, and triple parity. For single parity,
 * we use a simple XOR of all the data columns. For double or triple parity,
 * we use a special case of Reed-Solomon coding. This extends the
 * technique described in "The mathematics of RAID-6" by H. Peter Anvin by
 * drawing on the system described in "A Tutorial on Reed-Solomon Coding for
 * Fault-Tolerance in RAID-like Systems" by James S. Plank on which the
 * former is also based. The latter is designed to provide higher performance
 * for writes.
 *
 * Note that the Plank paper claimed to support arbitrary N+M, but was then
 * amended six years later identifying a critical flaw that invalidates its
 * claims. Nevertheless, the technique can be adapted to work for up to
 * triple parity. For additional parity, the amendment "Note: Correction to
 * the 1997 Tutorial on Reed-Solomon Coding" by James S. Plank and Ying Ding
 * is viable, but the additional complexity means that write performance will
 * suffer.
 *
 * All of the methods above operate on a Galois field, defined over the
 * integers mod 2^N. In our case we choose N=8 for GF(8) so that all elements
 * can be expressed with a single byte. Briefly, the operations on the
 * field are defined as follows:
 *
 *   o addition (+) is represented by a bitwise XOR
 *   o subtraction (-) is therefore identical to addition: A + B = A - B
 *   o multiplication of A by 2 is defined by the following bitwise expression:
 *
 *	(A * 2)_7 = A_6
 *	(A * 2)_6 = A_5
 *	(A * 2)_5 = A_4
 *	(A * 2)_4 = A_3 + A_7
 *	(A * 2)_3 = A_2 + A_7
 *	(A * 2)_2 = A_1 + A_7
 *	(A * 2)_1 = A_0
 *	(A * 2)_0 = A_7
 *
 * In C, multiplying by 2 is therefore ((a << 1) ^ ((a & 0x80) ? 0x1d : 0)).
 * As an aside, this multiplication is derived from the error correcting
 * primitive polynomial x^8 + x^4 + x^3 + x^2 + 1.
 *
 * Observe that any number in the field (except for 0) can be expressed as a
 * power of 2 -- a generator for the field. We store a table of the powers of
 * 2 and logs base 2 for quick look ups, and exploit the fact that A * B can
 * be rewritten as 2^(log_2(A) + log_2(B)) (where '+' is normal addition rather
 * than field addition). The inverse of a field element A (A^-1) is therefore
 * A ^ (255 - 1) = A^254.
 *
 * The up-to-three parity columns, P, Q, R over several data columns,
 * D_0, ... D_n-1, can be expressed by field operations:
 *
 *	P = D_0 + D_1 + ... + D_n-2 + D_n-1
 *	Q = 2^n-1 * D_0 + 2^n-2 * D_1 + ... + 2^1 * D_n-2 + 2^0 * D_n-1
 *	  = ((...((D_0) * 2 + D_1) * 2 + ...) * 2 + D_n-2) * 2 + D_n-1
 *	R = 4^n-1 * D_0 + 4^n-2 * D_1 + ... + 4^1 * D_n-2 + 4^0 * D_n-1
 *	  = ((...((D_0) * 4 + D_1) * 4 + ...) * 4 + D_n-2) * 4 + D_n-1
 *
 * We chose 1, 2, and 4 as our generators because 1 corresponds to the trival
 * XOR operation, and 2 and 4 can be computed quickly and generate linearly-
 * independent coefficients. (There are no additional coefficients that have
 * this property which is why the uncorrected Plank method breaks down.)
 *
 * See the reconstruction code below for how P, Q and R can used individually
 * or in concert to recover missing data columns.
 */

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
	raidz_col_t rm_col[1];		/* Flexible array of I/O columns */
} raidz_map_t;

#define	VDEV_RAIDZ_P		0
#define	VDEV_RAIDZ_Q		1
#define	VDEV_RAIDZ_R		2

#define	VDEV_RAIDZ_MUL_2(x)	(((x) << 1) ^ (((x) & 0x80) ? 0x1d : 0))
#define	VDEV_RAIDZ_MUL_4(x)	(VDEV_RAIDZ_MUL_2(VDEV_RAIDZ_MUL_2(x)))

/*
 * We provide a mechanism to perform the field multiplication operation on a
 * 64-bit value all at once rather than a byte at a time. This works by
 * creating a mask from the top bit in each byte and using that to
 * conditionally apply the XOR of 0x1d.
 */
#define	VDEV_RAIDZ_64MUL_2(x, mask) \
{ \
	(mask) = (x) & 0x8080808080808080ULL; \
	(mask) = ((mask) << 1) - ((mask) >> 7); \
	(x) = (((x) << 1) & 0xfefefefefefefefeULL) ^ \
	    ((mask) & 0x1d1d1d1d1d1d1d1d); \
}

#define	VDEV_RAIDZ_64MUL_4(x, mask) \
{ \
	VDEV_RAIDZ_64MUL_2((x), mask); \
	VDEV_RAIDZ_64MUL_2((x), mask); \
}

#define	VDEV_LABEL_OFFSET(x)	(x + VDEV_LABEL_START_SIZE)

/*
 * Force reconstruction to use the general purpose method.
 */
int vdev_raidz_default_to_general;

/* Powers of 2 in the Galois field defined above. */
static const uint8_t vdev_raidz_pow2[256] = {
	0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
	0x1d, 0x3a, 0x74, 0xe8, 0xcd, 0x87, 0x13, 0x26,
	0x4c, 0x98, 0x2d, 0x5a, 0xb4, 0x75, 0xea, 0xc9,
	0x8f, 0x03, 0x06, 0x0c, 0x18, 0x30, 0x60, 0xc0,
	0x9d, 0x27, 0x4e, 0x9c, 0x25, 0x4a, 0x94, 0x35,
	0x6a, 0xd4, 0xb5, 0x77, 0xee, 0xc1, 0x9f, 0x23,
	0x46, 0x8c, 0x05, 0x0a, 0x14, 0x28, 0x50, 0xa0,
	0x5d, 0xba, 0x69, 0xd2, 0xb9, 0x6f, 0xde, 0xa1,
	0x5f, 0xbe, 0x61, 0xc2, 0x99, 0x2f, 0x5e, 0xbc,
	0x65, 0xca, 0x89, 0x0f, 0x1e, 0x3c, 0x78, 0xf0,
	0xfd, 0xe7, 0xd3, 0xbb, 0x6b, 0xd6, 0xb1, 0x7f,
	0xfe, 0xe1, 0xdf, 0xa3, 0x5b, 0xb6, 0x71, 0xe2,
	0xd9, 0xaf, 0x43, 0x86, 0x11, 0x22, 0x44, 0x88,
	0x0d, 0x1a, 0x34, 0x68, 0xd0, 0xbd, 0x67, 0xce,
	0x81, 0x1f, 0x3e, 0x7c, 0xf8, 0xed, 0xc7, 0x93,
	0x3b, 0x76, 0xec, 0xc5, 0x97, 0x33, 0x66, 0xcc,
	0x85, 0x17, 0x2e, 0x5c, 0xb8, 0x6d, 0xda, 0xa9,
	0x4f, 0x9e, 0x21, 0x42, 0x84, 0x15, 0x2a, 0x54,
	0xa8, 0x4d, 0x9a, 0x29, 0x52, 0xa4, 0x55, 0xaa,
	0x49, 0x92, 0x39, 0x72, 0xe4, 0xd5, 0xb7, 0x73,
	0xe6, 0xd1, 0xbf, 0x63, 0xc6, 0x91, 0x3f, 0x7e,
	0xfc, 0xe5, 0xd7, 0xb3, 0x7b, 0xf6, 0xf1, 0xff,
	0xe3, 0xdb, 0xab, 0x4b, 0x96, 0x31, 0x62, 0xc4,
	0x95, 0x37, 0x6e, 0xdc, 0xa5, 0x57, 0xae, 0x41,
	0x82, 0x19, 0x32, 0x64, 0xc8, 0x8d, 0x07, 0x0e,
	0x1c, 0x38, 0x70, 0xe0, 0xdd, 0xa7, 0x53, 0xa6,
	0x51, 0xa2, 0x59, 0xb2, 0x79, 0xf2, 0xf9, 0xef,
	0xc3, 0x9b, 0x2b, 0x56, 0xac, 0x45, 0x8a, 0x09,
	0x12, 0x24, 0x48, 0x90, 0x3d, 0x7a, 0xf4, 0xf5,
	0xf7, 0xf3, 0xfb, 0xeb, 0xcb, 0x8b, 0x0b, 0x16,
	0x2c, 0x58, 0xb0, 0x7d, 0xfa, 0xe9, 0xcf, 0x83,
	0x1b, 0x36, 0x6c, 0xd8, 0xad, 0x47, 0x8e, 0x01
};
/* Logs of 2 in the Galois field defined above. */
static const uint8_t vdev_raidz_log2[256] = {
	0x00, 0x00, 0x01, 0x19, 0x02, 0x32, 0x1a, 0xc6,
	0x03, 0xdf, 0x33, 0xee, 0x1b, 0x68, 0xc7, 0x4b,
	0x04, 0x64, 0xe0, 0x0e, 0x34, 0x8d, 0xef, 0x81,
	0x1c, 0xc1, 0x69, 0xf8, 0xc8, 0x08, 0x4c, 0x71,
	0x05, 0x8a, 0x65, 0x2f, 0xe1, 0x24, 0x0f, 0x21,
	0x35, 0x93, 0x8e, 0xda, 0xf0, 0x12, 0x82, 0x45,
	0x1d, 0xb5, 0xc2, 0x7d, 0x6a, 0x27, 0xf9, 0xb9,
	0xc9, 0x9a, 0x09, 0x78, 0x4d, 0xe4, 0x72, 0xa6,
	0x06, 0xbf, 0x8b, 0x62, 0x66, 0xdd, 0x30, 0xfd,
	0xe2, 0x98, 0x25, 0xb3, 0x10, 0x91, 0x22, 0x88,
	0x36, 0xd0, 0x94, 0xce, 0x8f, 0x96, 0xdb, 0xbd,
	0xf1, 0xd2, 0x13, 0x5c, 0x83, 0x38, 0x46, 0x40,
	0x1e, 0x42, 0xb6, 0xa3, 0xc3, 0x48, 0x7e, 0x6e,
	0x6b, 0x3a, 0x28, 0x54, 0xfa, 0x85, 0xba, 0x3d,
	0xca, 0x5e, 0x9b, 0x9f, 0x0a, 0x15, 0x79, 0x2b,
	0x4e, 0xd4, 0xe5, 0xac, 0x73, 0xf3, 0xa7, 0x57,
	0x07, 0x70, 0xc0, 0xf7, 0x8c, 0x80, 0x63, 0x0d,
	0x67, 0x4a, 0xde, 0xed, 0x31, 0xc5, 0xfe, 0x18,
	0xe3, 0xa5, 0x99, 0x77, 0x26, 0xb8, 0xb4, 0x7c,
	0x11, 0x44, 0x92, 0xd9, 0x23, 0x20, 0x89, 0x2e,
	0x37, 0x3f, 0xd1, 0x5b, 0x95, 0xbc, 0xcf, 0xcd,
	0x90, 0x87, 0x97, 0xb2, 0xdc, 0xfc, 0xbe, 0x61,
	0xf2, 0x56, 0xd3, 0xab, 0x14, 0x2a, 0x5d, 0x9e,
	0x84, 0x3c, 0x39, 0x53, 0x47, 0x6d, 0x41, 0xa2,
	0x1f, 0x2d, 0x43, 0xd8, 0xb7, 0x7b, 0xa4, 0x76,
	0xc4, 0x17, 0x49, 0xec, 0x7f, 0x0c, 0x6f, 0xf6,
	0x6c, 0xa1, 0x3b, 0x52, 0x29, 0x9d, 0x55, 0xaa,
	0xfb, 0x60, 0x86, 0xb1, 0xbb, 0xcc, 0x3e, 0x5a,
	0xcb, 0x59, 0x5f, 0xb0, 0x9c, 0xa9, 0xa0, 0x51,
	0x0b, 0xf5, 0x16, 0xeb, 0x7a, 0x75, 0x2c, 0xd7,
	0x4f, 0xae, 0xd5, 0xe9, 0xe6, 0xe7, 0xad, 0xe8,
	0x74, 0xd6, 0xf4, 0xea, 0xa8, 0x50, 0x58, 0xaf,
};

static void vdev_raidz_generate_parity(raidz_map_t *rm);

/*
 * Multiply a given number by 2 raised to the given power.
 */
static uint8_t
vdev_raidz_exp2(uint_t a, int exp)
{
	if (a == 0)
		return (0);

	ASSERT(exp >= 0);
	ASSERT(vdev_raidz_log2[a] > 0 || a == 1);

	exp += vdev_raidz_log2[a];
	if (exp > 255)
		exp -= 255;

	return (vdev_raidz_pow2[exp]);
}

static void
vdev_raidz_map_free(raidz_map_t *rm)
{
	int c;

	for (c = 0; c < rm->rm_firstdatacol; c++) {
		if (rm->rm_col[c].rc_abd != NULL)
			abd_free(rm->rm_col[c].rc_abd);

		if (rm->rm_col[c].rc_gdata != NULL)
			zio_buf_free(rm->rm_col[c].rc_gdata,
			    rm->rm_col[c].rc_size);
	}

	for (c = rm->rm_firstdatacol; c < rm->rm_cols; c++) {
		if (rm->rm_col[c].rc_abd != NULL)
			abd_put(rm->rm_col[c].rc_abd);
	}

	if (rm->rm_abd_copy != NULL)
		abd_free(rm->rm_abd_copy);

	kmem_free(rm, offsetof(raidz_map_t, rm_col[rm->rm_scols]));
}

static void
vdev_raidz_map_free_vsd(zio_t *zio)
{
	raidz_map_t *rm = zio->io_vsd;

	ASSERT0(rm->rm_freed);
	rm->rm_freed = 1;

	if (rm->rm_reports == 0)
		vdev_raidz_map_free(rm);
}

/*ARGSUSED*/
static void
vdev_raidz_cksum_free(void *arg, size_t ignored)
{
	raidz_map_t *rm = arg;

	ASSERT3U(rm->rm_reports, >, 0);

	if (--rm->rm_reports == 0 && rm->rm_freed != 0)
		vdev_raidz_map_free(rm);
}

static void
vdev_raidz_cksum_finish(zio_cksum_report_t *zcr, const void *good_data)
{
	raidz_map_t *rm = zcr->zcr_cbdata;
	size_t c = zcr->zcr_cbinfo;
	size_t x;

	const char *good = NULL;
	char *bad;

	if (good_data == NULL) {
		zfs_ereport_finish_checksum(zcr, NULL, NULL, B_FALSE);
		return;
	}

	if (c < rm->rm_firstdatacol) {
		/*
		 * The first time through, calculate the parity blocks for
		 * the good data (this relies on the fact that the good
		 * data never changes for a given logical ZIO)
		 */
		if (rm->rm_col[0].rc_gdata == NULL) {
			abd_t *bad_parity[VDEV_RAIDZ_MAXPARITY];
			char *buf;
			int offset;

			/*
			 * Set up the rm_col[]s to generate the parity for
			 * good_data, first saving the parity bufs and
			 * replacing them with buffers to hold the result.
			 */
			for (x = 0; x < rm->rm_firstdatacol; x++) {
				bad_parity[x] = rm->rm_col[x].rc_abd;
				rm->rm_col[x].rc_gdata =
				    zio_buf_alloc(rm->rm_col[x].rc_size);
				rm->rm_col[x].rc_abd =
				    abd_get_from_buf(rm->rm_col[x].rc_gdata,
				    rm->rm_col[x].rc_size);
			}

			/* fill in the data columns from good_data */
			buf = (char *)good_data;
			for (; x < rm->rm_cols; x++) {
				abd_put(rm->rm_col[x].rc_abd);
				rm->rm_col[x].rc_abd = abd_get_from_buf(buf,
				    rm->rm_col[x].rc_size);
				buf += rm->rm_col[x].rc_size;
			}

			/*
			 * Construct the parity from the good data.
			 */
			vdev_raidz_generate_parity(rm);

			/* restore everything back to its original state */
			for (x = 0; x < rm->rm_firstdatacol; x++) {
				abd_put(rm->rm_col[x].rc_abd);
				rm->rm_col[x].rc_abd = bad_parity[x];
			}

			offset = 0;
			for (x = rm->rm_firstdatacol; x < rm->rm_cols; x++) {
				abd_put(rm->rm_col[x].rc_abd);
				rm->rm_col[x].rc_abd = abd_get_offset(
				    rm->rm_abd_copy, offset);
				offset += rm->rm_col[x].rc_size;
			}
		}

		ASSERT3P(rm->rm_col[c].rc_gdata, !=, NULL);
		good = rm->rm_col[c].rc_gdata;
	} else {
		/* adjust good_data to point at the start of our column */
		good = good_data;

		for (x = rm->rm_firstdatacol; x < c; x++)
			good += rm->rm_col[x].rc_size;
	}

	bad = abd_borrow_buf_copy(rm->rm_col[c].rc_abd, rm->rm_col[c].rc_size);
	/* we drop the ereport if it ends up that the data was good */
	zfs_ereport_finish_checksum(zcr, good, bad, B_TRUE);
	abd_return_buf(rm->rm_col[c].rc_abd, bad, rm->rm_col[c].rc_size);
}

/*
 * Invoked indirectly by zfs_ereport_start_checksum(), called
 * below when our read operation fails completely.  The main point
 * is to keep a copy of everything we read from disk, so that at
 * vdev_raidz_cksum_finish() time we can compare it with the good data.
 */
static void
vdev_raidz_cksum_report(zio_t *zio, zio_cksum_report_t *zcr, void *arg)
{
	size_t c = (size_t)(uintptr_t)arg;
	size_t offset;

	raidz_map_t *rm = zio->io_vsd;
	size_t size;

	/* set up the report and bump the refcount  */
	zcr->zcr_cbdata = rm;
	zcr->zcr_cbinfo = c;
	zcr->zcr_finish = vdev_raidz_cksum_finish;
	zcr->zcr_free = vdev_raidz_cksum_free;

	rm->rm_reports++;
	ASSERT3U(rm->rm_reports, >, 0);

	if (rm->rm_abd_copy != NULL)
		return;

	/*
	 * It's the first time we're called for this raidz_map_t, so we need
	 * to copy the data aside; there's no guarantee that our zio's buffer
	 * won't be re-used for something else.
	 *
	 * Our parity data is already in separate buffers, so there's no need
	 * to copy them.
	 */

	size = 0;
	for (c = rm->rm_firstdatacol; c < rm->rm_cols; c++)
		size += rm->rm_col[c].rc_size;

	rm->rm_abd_copy =
	    abd_alloc_sametype(rm->rm_col[rm->rm_firstdatacol].rc_abd, size);

	for (offset = 0, c = rm->rm_firstdatacol; c < rm->rm_cols; c++) {
		raidz_col_t *col = &rm->rm_col[c];
		abd_t *tmp = abd_get_offset(rm->rm_abd_copy, offset);

		abd_copy(tmp, col->rc_abd, col->rc_size);
		abd_put(col->rc_abd);
		col->rc_abd = tmp;

		offset += col->rc_size;
	}
	ASSERT3U(offset, ==, size);
}

static const zio_vsd_ops_t vdev_raidz_vsd_ops = {
	vdev_raidz_map_free_vsd,
	vdev_raidz_cksum_report
};

/*
 * Divides the IO evenly across all child vdevs; usually, dcols is
 * the number of children in the target vdev.
 */
static raidz_map_t *
vdev_raidz_map_alloc(abd_t *abd, uint64_t size, uint64_t offset, boolean_t dofree,
    uint64_t unit_shift, uint64_t dcols, uint64_t nparity)
{
	raidz_map_t *rm;
	/* The starting RAIDZ (parent) vdev sector of the block. */
	uint64_t b = offset >> unit_shift;
	/* The zio's size in units of the vdev's minimum sector size. */
	uint64_t s = size >> unit_shift;
	/* The first column for this stripe. */
	uint64_t f = b % dcols;
	/* The starting byte offset on each child vdev. */
	uint64_t o = (b / dcols) << unit_shift;
	uint64_t q, r, c, bc, col, acols, scols, coff, devidx, asize, tot;
	uint64_t off = 0;

	/*
	 * "Quotient": The number of data sectors for this stripe on all but
	 * the "big column" child vdevs that also contain "remainder" data.
	 */
	q = s / (dcols - nparity);

	/*
	 * "Remainder": The number of partial stripe data sectors in this I/O.
	 * This will add a sector to some, but not all, child vdevs.
	 */
	r = s - q * (dcols - nparity);

	/* The number of "big columns" - those which contain remainder data. */
	bc = (r == 0 ? 0 : r + nparity);

	/*
	 * The total number of data and parity sectors associated with
	 * this I/O.
	 */
	tot = s + nparity * (q + (r == 0 ? 0 : 1));

	/* acols: The columns that will be accessed. */
	/* scols: The columns that will be accessed or skipped. */
	if (q == 0) {
		/* Our I/O request doesn't span all child vdevs. */
		acols = bc;
		scols = MIN(dcols, roundup(bc, nparity + 1));
	} else {
		acols = dcols;
		scols = dcols;
	}

	ASSERT3U(acols, <=, scols);

	rm = kmem_alloc(offsetof(raidz_map_t, rm_col[scols]), KM_SLEEP);

	rm->rm_cols = acols;
	rm->rm_scols = scols;
	rm->rm_bigcols = bc;
	rm->rm_skipstart = bc;
	rm->rm_missingdata = 0;
	rm->rm_missingparity = 0;
	rm->rm_firstdatacol = nparity;
	rm->rm_abd_copy = NULL;
	rm->rm_reports = 0;
	rm->rm_freed = 0;
	rm->rm_ecksuminjected = 0;

	asize = 0;

	for (c = 0; c < scols; c++) {
		col = f + c;
		coff = o;
		if (col >= dcols) {
			col -= dcols;
			coff += 1ULL << unit_shift;
		}
		rm->rm_col[c].rc_devidx = col;
		rm->rm_col[c].rc_offset = coff;
		rm->rm_col[c].rc_abd = NULL;
		rm->rm_col[c].rc_gdata = NULL;
		rm->rm_col[c].rc_error = 0;
		rm->rm_col[c].rc_tried = 0;
		rm->rm_col[c].rc_skipped = 0;

		if (c >= acols)
			rm->rm_col[c].rc_size = 0;
		else if (c < bc)
			rm->rm_col[c].rc_size = (q + 1) << unit_shift;
		else
			rm->rm_col[c].rc_size = q << unit_shift;

		asize += rm->rm_col[c].rc_size;
	}

	ASSERT3U(asize, ==, tot << unit_shift);
	rm->rm_asize = roundup(asize, (nparity + 1) << unit_shift);
	rm->rm_nskip = roundup(tot, nparity + 1) - tot;
	ASSERT3U(rm->rm_asize - asize, ==, rm->rm_nskip << unit_shift);
	ASSERT3U(rm->rm_nskip, <=, nparity);

	if (!dofree) {
		for (c = 0; c < rm->rm_firstdatacol; c++) {
			rm->rm_col[c].rc_abd =
			    abd_alloc_linear(rm->rm_col[c].rc_size, B_TRUE);
		}

		for (off = 0, c = rm->rm_firstdatacol; c < acols; c++) {
			rm->rm_col[c].rc_abd = abd_get_offset(abd, off);
			off += rm->rm_col[c].rc_size;
		}
	}

	/*
	 * If all data stored spans all columns, there's a danger that parity
	 * will always be on the same device and, since parity isn't read
	 * during normal operation, that that device's I/O bandwidth won't be
	 * used effectively. We therefore switch the parity every 1MB.
	 *
	 * ... at least that was, ostensibly, the theory. As a practical
	 * matter unless we juggle the parity between all devices evenly, we
	 * won't see any benefit. Further, occasional writes that aren't a
	 * multiple of the LCM of the number of children and the minimum
	 * stripe width are sufficient to avoid pessimal behavior.
	 * Unfortunately, this decision created an implicit on-disk format
	 * requirement that we need to support for all eternity, but only
	 * for single-parity RAID-Z.
	 *
	 * If we intend to skip a sector in the zeroth column for padding
	 * we must make sure to note this swap. We will never intend to
	 * skip the first column since at least one data and one parity
	 * column must appear in each row.
	 */
	ASSERT(rm->rm_cols >= 2);
	ASSERT(rm->rm_col[0].rc_size == rm->rm_col[1].rc_size);

	if (rm->rm_firstdatacol == 1 && (offset & (1ULL << 20))) {
		devidx = rm->rm_col[0].rc_devidx;
		o = rm->rm_col[0].rc_offset;
		rm->rm_col[0].rc_devidx = rm->rm_col[1].rc_devidx;
		rm->rm_col[0].rc_offset = rm->rm_col[1].rc_offset;
		rm->rm_col[1].rc_devidx = devidx;
		rm->rm_col[1].rc_offset = o;

		if (rm->rm_skipstart == 0)
			rm->rm_skipstart = 1;
	}

	return (rm);
}

struct pqr_struct {
	uint64_t *p;
	uint64_t *q;
	uint64_t *r;
};

static int
vdev_raidz_p_func(void *buf, size_t size, void *private)
{
	struct pqr_struct *pqr = private;
	const uint64_t *src = buf;
	int i, cnt = size / sizeof (src[0]);

	ASSERT(pqr->p && !pqr->q && !pqr->r);

	for (i = 0; i < cnt; i++, src++, pqr->p++)
		*pqr->p ^= *src;

	return (0);
}

static int
vdev_raidz_pq_func(void *buf, size_t size, void *private)
{
	struct pqr_struct *pqr = private;
	const uint64_t *src = buf;
	uint64_t mask;
	int i, cnt = size / sizeof (src[0]);

	ASSERT(pqr->p && pqr->q && !pqr->r);

	for (i = 0; i < cnt; i++, src++, pqr->p++, pqr->q++) {
		*pqr->p ^= *src;
		VDEV_RAIDZ_64MUL_2(*pqr->q, mask);
		*pqr->q ^= *src;
	}

	return (0);
}

static int
vdev_raidz_pqr_func(void *buf, size_t size, void *private)
{
	struct pqr_struct *pqr = private;
	const uint64_t *src = buf;
	uint64_t mask;
	int i, cnt = size / sizeof (src[0]);

	ASSERT(pqr->p && pqr->q && pqr->r);

	for (i = 0; i < cnt; i++, src++, pqr->p++, pqr->q++, pqr->r++) {
		*pqr->p ^= *src;
		VDEV_RAIDZ_64MUL_2(*pqr->q, mask);
		*pqr->q ^= *src;
		VDEV_RAIDZ_64MUL_4(*pqr->r, mask);
		*pqr->r ^= *src;
	}

	return (0);
}

static void
vdev_raidz_generate_parity_p(raidz_map_t *rm)
{
	uint64_t *p;
	int c;
	abd_t *src;

	for (c = rm->rm_firstdatacol; c < rm->rm_cols; c++) {
		src = rm->rm_col[c].rc_abd;
		p = abd_to_buf(rm->rm_col[VDEV_RAIDZ_P].rc_abd);

		if (c == rm->rm_firstdatacol) {
			abd_copy_to_buf(p, src, rm->rm_col[c].rc_size);
		} else {
			struct pqr_struct pqr = { p, NULL, NULL };
			(void) abd_iterate_func(src, 0, rm->rm_col[c].rc_size,
			    vdev_raidz_p_func, &pqr);
		}
	}
}

static void
vdev_raidz_generate_parity_pq(raidz_map_t *rm)
{
	uint64_t *p, *q, pcnt, ccnt, mask, i;
	int c;
	abd_t *src;

	pcnt = rm->rm_col[VDEV_RAIDZ_P].rc_size / sizeof (p[0]);
	ASSERT(rm->rm_col[VDEV_RAIDZ_P].rc_size ==
	    rm->rm_col[VDEV_RAIDZ_Q].rc_size);

	for (c = rm->rm_firstdatacol; c < rm->rm_cols; c++) {
		src = rm->rm_col[c].rc_abd;
		p = abd_to_buf(rm->rm_col[VDEV_RAIDZ_P].rc_abd);
		q = abd_to_buf(rm->rm_col[VDEV_RAIDZ_Q].rc_abd);

		ccnt = rm->rm_col[c].rc_size / sizeof (p[0]);

		if (c == rm->rm_firstdatacol) {
			abd_copy_to_buf(p, src, rm->rm_col[c].rc_size);
			(void) memcpy(q, p, rm->rm_col[c].rc_size);
		} else {
			struct pqr_struct pqr = { p, q, NULL };
			(void) abd_iterate_func(src, 0, rm->rm_col[c].rc_size,
			    vdev_raidz_pq_func, &pqr);
		}

		if (c == rm->rm_firstdatacol) {
			for (i = ccnt; i < pcnt; i++) {
				p[i] = 0;
				q[i] = 0;
			}
		} else {
			/*
			 * Treat short columns as though they are full of 0s.
			 * Note that there's therefore nothing needed for P.
			 */
			for (i = ccnt; i < pcnt; i++) {
				VDEV_RAIDZ_64MUL_2(q[i], mask);
			}
		}
	}
}

static void
vdev_raidz_generate_parity_pqr(raidz_map_t *rm)
{
	uint64_t *p, *q, *r, pcnt, ccnt, mask, i;
	int c;
	abd_t *src;

	pcnt = rm->rm_col[VDEV_RAIDZ_P].rc_size / sizeof (p[0]);
	ASSERT(rm->rm_col[VDEV_RAIDZ_P].rc_size ==
	    rm->rm_col[VDEV_RAIDZ_Q].rc_size);
	ASSERT(rm->rm_col[VDEV_RAIDZ_P].rc_size ==
	    rm->rm_col[VDEV_RAIDZ_R].rc_size);

	for (c = rm->rm_firstdatacol; c < rm->rm_cols; c++) {
		src = rm->rm_col[c].rc_abd;
		p = abd_to_buf(rm->rm_col[VDEV_RAIDZ_P].rc_abd);
		q = abd_to_buf(rm->rm_col[VDEV_RAIDZ_Q].rc_abd);
		r = abd_to_buf(rm->rm_col[VDEV_RAIDZ_R].rc_abd);

		ccnt = rm->rm_col[c].rc_size / sizeof (p[0]);

		if (c == rm->rm_firstdatacol) {
			abd_copy_to_buf(p, src, rm->rm_col[c].rc_size);
			(void) memcpy(q, p, rm->rm_col[c].rc_size);
			(void) memcpy(r, p, rm->rm_col[c].rc_size);
		} else {
			struct pqr_struct pqr = { p, q, r };
			(void) abd_iterate_func(src, 0, rm->rm_col[c].rc_size,
			    vdev_raidz_pqr_func, &pqr);
		}

		if (c == rm->rm_firstdatacol) {
			for (i = ccnt; i < pcnt; i++) {
				p[i] = 0;
				q[i] = 0;
				r[i] = 0;
			}
		} else {
			/*
			 * Treat short columns as though they are full of 0s.
			 * Note that there's therefore nothing needed for P.
			 */
			for (i = ccnt; i < pcnt; i++) {
				VDEV_RAIDZ_64MUL_2(q[i], mask);
				VDEV_RAIDZ_64MUL_4(r[i], mask);
			}
		}
	}
}

/*
 * Generate RAID parity in the first virtual columns according to the number of
 * parity columns available.
 */
static void
vdev_raidz_generate_parity(raidz_map_t *rm)
{
	switch (rm->rm_firstdatacol) {
	case 1:
		vdev_raidz_generate_parity_p(rm);
		break;
	case 2:
		vdev_raidz_generate_parity_pq(rm);
		break;
	case 3:
		vdev_raidz_generate_parity_pqr(rm);
		break;
	default:
		cmn_err(CE_PANIC, "invalid RAID-Z configuration");
	}
}

/* ARGSUSED */
static int
vdev_raidz_reconst_p_func(void *dbuf, void *sbuf, size_t size, void *private)
{
	uint64_t *dst = dbuf;
	uint64_t *src = sbuf;
	int cnt = size / sizeof (src[0]);

	for (int i = 0; i < cnt; i++) {
		dst[i] ^= src[i];
	}

	return (0);
}

/* ARGSUSED */
static int
vdev_raidz_reconst_q_pre_func(void *dbuf, void *sbuf, size_t size,
    void *private)
{
	uint64_t *dst = dbuf;
	uint64_t *src = sbuf;
	uint64_t mask;
	int cnt = size / sizeof (dst[0]);

	for (int i = 0; i < cnt; i++, dst++, src++) {
		VDEV_RAIDZ_64MUL_2(*dst, mask);
		*dst ^= *src;
	}

	return (0);
}

/* ARGSUSED */
static int
vdev_raidz_reconst_q_pre_tail_func(void *buf, size_t size, void *private)
{
	uint64_t *dst = buf;
	uint64_t mask;
	int cnt = size / sizeof (dst[0]);

	for (int i = 0; i < cnt; i++, dst++) {
		/* same operation as vdev_raidz_reconst_q_pre_func() on dst */
		VDEV_RAIDZ_64MUL_2(*dst, mask);
	}

	return (0);
}

struct reconst_q_struct {
	uint64_t *q;
	int exp;
};

static int
vdev_raidz_reconst_q_post_func(void *buf, size_t size, void *private)
{
	struct reconst_q_struct *rq = private;
	uint64_t *dst = buf;
	int cnt = size / sizeof (dst[0]);

	for (int i = 0; i < cnt; i++, dst++, rq->q++) {
		*dst ^= *rq->q;

		int j;
		uint8_t *b;
		for (j = 0, b = (uint8_t *)dst; j < 8; j++, b++) {
			*b = vdev_raidz_exp2(*b, rq->exp);
		}
	}

	return (0);
}

struct reconst_pq_struct {
	uint8_t *p;
	uint8_t *q;
	uint8_t *pxy;
	uint8_t *qxy;
	int aexp;
	int bexp;
};

static int
vdev_raidz_reconst_pq_func(void *xbuf, void *ybuf, size_t size, void *private)
{
	struct reconst_pq_struct *rpq = private;
	uint8_t *xd = xbuf;
	uint8_t *yd = ybuf;

	for (int i = 0; i < size;
	    i++, rpq->p++, rpq->q++, rpq->pxy++, rpq->qxy++, xd++, yd++) {
		*xd = vdev_raidz_exp2(*rpq->p ^ *rpq->pxy, rpq->aexp) ^
		    vdev_raidz_exp2(*rpq->q ^ *rpq->qxy, rpq->bexp);
		*yd = *rpq->p ^ *rpq->pxy ^ *xd;
	}

	return (0);
}

static int
vdev_raidz_reconst_pq_tail_func(void *xbuf, size_t size, void *private)
{
	struct reconst_pq_struct *rpq = private;
	uint8_t *xd = xbuf;

	for (int i = 0; i < size;
	    i++, rpq->p++, rpq->q++, rpq->pxy++, rpq->qxy++, xd++) {
		/* same operation as vdev_raidz_reconst_pq_func() on xd */
		*xd = vdev_raidz_exp2(*rpq->p ^ *rpq->pxy, rpq->aexp) ^
		    vdev_raidz_exp2(*rpq->q ^ *rpq->qxy, rpq->bexp);
	}

	return (0);
}

static int
vdev_raidz_reconstruct_p(raidz_map_t *rm, int *tgts, int ntgts)
{
	int x = tgts[0];
	int c;
	abd_t *dst, *src;

	ASSERT(ntgts == 1);
	ASSERT(x >= rm->rm_firstdatacol);
	ASSERT(x < rm->rm_cols);

	ASSERT(rm->rm_col[x].rc_size <= rm->rm_col[VDEV_RAIDZ_P].rc_size);
	ASSERT(rm->rm_col[x].rc_size > 0);

	src = rm->rm_col[VDEV_RAIDZ_P].rc_abd;
	dst = rm->rm_col[x].rc_abd;

	abd_copy(dst, src, rm->rm_col[x].rc_size);

	for (c = rm->rm_firstdatacol; c < rm->rm_cols; c++) {
		uint64_t size = MIN(rm->rm_col[x].rc_size,
		    rm->rm_col[c].rc_size);

		src = rm->rm_col[c].rc_abd;
		dst = rm->rm_col[x].rc_abd;

		if (c == x)
			continue;

		(void) abd_iterate_func2(dst, src, 0, 0, size,
		    vdev_raidz_reconst_p_func, NULL);
	}

	return (1 << VDEV_RAIDZ_P);
}

static int
vdev_raidz_reconstruct_q(raidz_map_t *rm, int *tgts, int ntgts)
{
	int x = tgts[0];
	int c, exp;
	abd_t *dst, *src;

	ASSERT(ntgts == 1);

	ASSERT(rm->rm_col[x].rc_size <= rm->rm_col[VDEV_RAIDZ_Q].rc_size);

	for (c = rm->rm_firstdatacol; c < rm->rm_cols; c++) {
		uint64_t size = (c == x) ? 0 : MIN(rm->rm_col[x].rc_size,
		    rm->rm_col[c].rc_size);

		src = rm->rm_col[c].rc_abd;
		dst = rm->rm_col[x].rc_abd;

		if (c == rm->rm_firstdatacol) {
			abd_copy(dst, src, size);
			if (rm->rm_col[x].rc_size > size)
				abd_zero_off(dst, size,
				    rm->rm_col[x].rc_size - size);
		} else {
			ASSERT3U(size, <=, rm->rm_col[x].rc_size);
			(void) abd_iterate_func2(dst, src, 0, 0, size,
			    vdev_raidz_reconst_q_pre_func, NULL);
			(void) abd_iterate_func(dst,
			    size, rm->rm_col[x].rc_size - size,
			    vdev_raidz_reconst_q_pre_tail_func, NULL);
		}
	}

	src = rm->rm_col[VDEV_RAIDZ_Q].rc_abd;
	dst = rm->rm_col[x].rc_abd;
	exp = 255 - (rm->rm_cols - 1 - x);

	struct reconst_q_struct rq = { abd_to_buf(src), exp };
	(void) abd_iterate_func(dst, 0, rm->rm_col[x].rc_size,
	    vdev_raidz_reconst_q_post_func, &rq);

	return (1 << VDEV_RAIDZ_Q);
}

static int
vdev_raidz_reconstruct_pq(raidz_map_t *rm, int *tgts, int ntgts)
{
	uint8_t *p, *q, *pxy, *qxy, tmp, a, b, aexp, bexp;
	abd_t *pdata, *qdata;
	uint64_t xsize, ysize;
	int x = tgts[0];
	int y = tgts[1];
	abd_t *xd, *yd;

	ASSERT(ntgts == 2);
	ASSERT(x < y);
	ASSERT(x >= rm->rm_firstdatacol);
	ASSERT(y < rm->rm_cols);

	ASSERT(rm->rm_col[x].rc_size >= rm->rm_col[y].rc_size);

	/*
	 * Move the parity data aside -- we're going to compute parity as
	 * though columns x and y were full of zeros -- Pxy and Qxy. We want to
	 * reuse the parity generation mechanism without trashing the actual
	 * parity so we make those columns appear to be full of zeros by
	 * setting their lengths to zero.
	 */
	pdata = rm->rm_col[VDEV_RAIDZ_P].rc_abd;
	qdata = rm->rm_col[VDEV_RAIDZ_Q].rc_abd;
	xsize = rm->rm_col[x].rc_size;
	ysize = rm->rm_col[y].rc_size;

	rm->rm_col[VDEV_RAIDZ_P].rc_abd =
	    abd_alloc_linear(rm->rm_col[VDEV_RAIDZ_P].rc_size, B_TRUE);
	rm->rm_col[VDEV_RAIDZ_Q].rc_abd =
	    abd_alloc_linear(rm->rm_col[VDEV_RAIDZ_Q].rc_size, B_TRUE);
	rm->rm_col[x].rc_size = 0;
	rm->rm_col[y].rc_size = 0;

	vdev_raidz_generate_parity_pq(rm);

	rm->rm_col[x].rc_size = xsize;
	rm->rm_col[y].rc_size = ysize;

	p = abd_to_buf(pdata);
	q = abd_to_buf(qdata);
	pxy = abd_to_buf(rm->rm_col[VDEV_RAIDZ_P].rc_abd);
	qxy = abd_to_buf(rm->rm_col[VDEV_RAIDZ_Q].rc_abd);
	xd = rm->rm_col[x].rc_abd;
	yd = rm->rm_col[y].rc_abd;

	/*
	 * We now have:
	 *	Pxy = P + D_x + D_y
	 *	Qxy = Q + 2^(ndevs - 1 - x) * D_x + 2^(ndevs - 1 - y) * D_y
	 *
	 * We can then solve for D_x:
	 *	D_x = A * (P + Pxy) + B * (Q + Qxy)
	 * where
	 *	A = 2^(x - y) * (2^(x - y) + 1)^-1
	 *	B = 2^(ndevs - 1 - x) * (2^(x - y) + 1)^-1
	 *
	 * With D_x in hand, we can easily solve for D_y:
	 *	D_y = P + Pxy + D_x
	 */

	a = vdev_raidz_pow2[255 + x - y];
	b = vdev_raidz_pow2[255 - (rm->rm_cols - 1 - x)];
	tmp = 255 - vdev_raidz_log2[a ^ 1];

	aexp = vdev_raidz_log2[vdev_raidz_exp2(a, tmp)];
	bexp = vdev_raidz_log2[vdev_raidz_exp2(b, tmp)];

	ASSERT3U(xsize, >=, ysize);
	struct reconst_pq_struct rpq = { p, q, pxy, qxy, aexp, bexp };
	(void) abd_iterate_func2(xd, yd, 0, 0, ysize,
	    vdev_raidz_reconst_pq_func, &rpq);
	(void) abd_iterate_func(xd, ysize, xsize - ysize,
	    vdev_raidz_reconst_pq_tail_func, &rpq);

	abd_free(rm->rm_col[VDEV_RAIDZ_P].rc_abd);
	abd_free(rm->rm_col[VDEV_RAIDZ_Q].rc_abd);

	/*
	 * Restore the saved parity data.
	 */
	rm->rm_col[VDEV_RAIDZ_P].rc_abd = pdata;
	rm->rm_col[VDEV_RAIDZ_Q].rc_abd = qdata;

	return ((1 << VDEV_RAIDZ_P) | (1 << VDEV_RAIDZ_Q));
}

/* BEGIN CSTYLED */
/*
 * In the general case of reconstruction, we must solve the system of linear
 * equations defined by the coeffecients used to generate parity as well as
 * the contents of the data and parity disks. This can be expressed with
 * vectors for the original data (D) and the actual data (d) and parity (p)
 * and a matrix composed of the identity matrix (I) and a dispersal matrix (V):
 *
 *            __   __                     __     __
 *            |     |         __     __   |  p_0  |
 *            |  V  |         |  D_0  |   | p_m-1 |
 *            |     |    x    |   :   | = |  d_0  |
 *            |  I  |         | D_n-1 |   |   :   |
 *            |     |         ~~     ~~   | d_n-1 |
 *            ~~   ~~                     ~~     ~~
 *
 * I is simply a square identity matrix of size n, and V is a vandermonde
 * matrix defined by the coeffecients we chose for the various parity columns
 * (1, 2, 4). Note that these values were chosen both for simplicity, speedy
 * computation as well as linear separability.
 *
 *      __               __               __     __
 *      |   1   ..  1 1 1 |               |  p_0  |
 *      | 2^n-1 ..  4 2 1 |   __     __   |   :   |
 *      | 4^n-1 .. 16 4 1 |   |  D_0  |   | p_m-1 |
 *      |   1   ..  0 0 0 |   |  D_1  |   |  d_0  |
 *      |   0   ..  0 0 0 | x |  D_2  | = |  d_1  |
 *      |   :       : : : |   |   :   |   |  d_2  |
 *      |   0   ..  1 0 0 |   | D_n-1 |   |   :   |
 *      |   0   ..  0 1 0 |   ~~     ~~   |   :   |
 *      |   0   ..  0 0 1 |               | d_n-1 |
 *      ~~               ~~               ~~     ~~
 *
 * Note that I, V, d, and p are known. To compute D, we must invert the
 * matrix and use the known data and parity values to reconstruct the unknown
 * data values. We begin by removing the rows in V|I and d|p that correspond
 * to failed or missing columns; we then make V|I square (n x n) and d|p
 * sized n by removing rows corresponding to unused parity from the bottom up
 * to generate (V|I)' and (d|p)'. We can then generate the inverse of (V|I)'
 * using Gauss-Jordan elimination. In the example below we use m=3 parity
 * columns, n=8 data columns, with errors in d_1, d_2, and p_1:
 *           __                               __
 *           |  1   1   1   1   1   1   1   1  |
 *           | 128  64  32  16  8   4   2   1  | <-----+-+-- missing disks
 *           |  19 205 116  29  64  16  4   1  |      / /
 *           |  1   0   0   0   0   0   0   0  |     / /
 *           |  0   1   0   0   0   0   0   0  | <--' /
 *  (V|I)  = |  0   0   1   0   0   0   0   0  | <---'
 *           |  0   0   0   1   0   0   0   0  |
 *           |  0   0   0   0   1   0   0   0  |
 *           |  0   0   0   0   0   1   0   0  |
 *           |  0   0   0   0   0   0   1   0  |
 *           |  0   0   0   0   0   0   0   1  |
 *           ~~                               ~~
 *           __                               __
 *           |  1   1   1   1   1   1   1   1  |
 *           |  19 205 116  29  64  16  4   1  |
 *           |  1   0   0   0   0   0   0   0  |
 *  (V|I)' = |  0   0   0   1   0   0   0   0  |
 *           |  0   0   0   0   1   0   0   0  |
 *           |  0   0   0   0   0   1   0   0  |
 *           |  0   0   0   0   0   0   1   0  |
 *           |  0   0   0   0   0   0   0   1  |
 *           ~~                               ~~
 *
 * Here we employ Gauss-Jordan elimination to find the inverse of (V|I)'. We
 * have carefully chosen the seed values 1, 2, and 4 to ensure that this
 * matrix is not singular.
 * __                                                                 __
 * |  1   1   1   1   1   1   1   1     1   0   0   0   0   0   0   0  |
 * |  19 205 116  29  64  16  4   1     0   1   0   0   0   0   0   0  |
 * |  1   0   0   0   0   0   0   0     0   0   1   0   0   0   0   0  |
 * |  0   0   0   1   0   0   0   0     0   0   0   1   0   0   0   0  |
 * |  0   0   0   0   1   0   0   0     0   0   0   0   1   0   0   0  |
 * |  0   0   0   0   0   1   0   0     0   0   0   0   0   1   0   0  |
 * |  0   0   0   0   0   0   1   0     0   0   0   0   0   0   1   0  |
 * |  0   0   0   0   0   0   0   1     0   0   0   0   0   0   0   1  |
 * ~~                                                                 ~~
 * __                                                                 __
 * |  1   0   0   0   0   0   0   0     0   0   1   0   0   0   0   0  |
 * |  1   1   1   1   1   1   1   1     1   0   0   0   0   0   0   0  |
 * |  19 205 116  29  64  16  4   1     0   1   0   0   0   0   0   0  |
 * |  0   0   0   1   0   0   0   0     0   0   0   1   0   0   0   0  |
 * |  0   0   0   0   1   0   0   0     0   0   0   0   1   0   0   0  |
 * |  0   0   0   0   0   1   0   0     0   0   0   0   0   1   0   0  |
 * |  0   0   0   0   0   0   1   0     0   0   0   0   0   0   1   0  |
 * |  0   0   0   0   0   0   0   1     0   0   0   0   0   0   0   1  |
 * ~~                                                                 ~~
 * __                                                                 __
 * |  1   0   0   0   0   0   0   0     0   0   1   0   0   0   0   0  |
 * |  0   1   1   0   0   0   0   0     1   0   1   1   1   1   1   1  |
 * |  0  205 116  0   0   0   0   0     0   1   19  29  64  16  4   1  |
 * |  0   0   0   1   0   0   0   0     0   0   0   1   0   0   0   0  |
 * |  0   0   0   0   1   0   0   0     0   0   0   0   1   0   0   0  |
 * |  0   0   0   0   0   1   0   0     0   0   0   0   0   1   0   0  |
 * |  0   0   0   0   0   0   1   0     0   0   0   0   0   0   1   0  |
 * |  0   0   0   0   0   0   0   1     0   0   0   0   0   0   0   1  |
 * ~~                                                                 ~~
 * __                                                                 __
 * |  1   0   0   0   0   0   0   0     0   0   1   0   0   0   0   0  |
 * |  0   1   1   0   0   0   0   0     1   0   1   1   1   1   1   1  |
 * |  0   0  185  0   0   0   0   0    205  1  222 208 141 221 201 204 |
 * |  0   0   0   1   0   0   0   0     0   0   0   1   0   0   0   0  |
 * |  0   0   0   0   1   0   0   0     0   0   0   0   1   0   0   0  |
 * |  0   0   0   0   0   1   0   0     0   0   0   0   0   1   0   0  |
 * |  0   0   0   0   0   0   1   0     0   0   0   0   0   0   1   0  |
 * |  0   0   0   0   0   0   0   1     0   0   0   0   0   0   0   1  |
 * ~~                                                                 ~~
 * __                                                                 __
 * |  1   0   0   0   0   0   0   0     0   0   1   0   0   0   0   0  |
 * |  0   1   1   0   0   0   0   0     1   0   1   1   1   1   1   1  |
 * |  0   0   1   0   0   0   0   0    166 100  4   40 158 168 216 209 |
 * |  0   0   0   1   0   0   0   0     0   0   0   1   0   0   0   0  |
 * |  0   0   0   0   1   0   0   0     0   0   0   0   1   0   0   0  |
 * |  0   0   0   0   0   1   0   0     0   0   0   0   0   1   0   0  |
 * |  0   0   0   0   0   0   1   0     0   0   0   0   0   0   1   0  |
 * |  0   0   0   0   0   0   0   1     0   0   0   0   0   0   0   1  |
 * ~~                                                                 ~~
 * __                                                                 __
 * |  1   0   0   0   0   0   0   0     0   0   1   0   0   0   0   0  |
 * |  0   1   0   0   0   0   0   0    167 100  5   41 159 169 217 208 |
 * |  0   0   1   0   0   0   0   0    166 100  4   40 158 168 216 209 |
 * |  0   0   0   1   0   0   0   0     0   0   0   1   0   0   0   0  |
 * |  0   0   0   0   1   0   0   0     0   0   0   0   1   0   0   0  |
 * |  0   0   0   0   0   1   0   0     0   0   0   0   0   1   0   0  |
 * |  0   0   0   0   0   0   1   0     0   0   0   0   0   0   1   0  |
 * |  0   0   0   0   0   0   0   1     0   0   0   0   0   0   0   1  |
 * ~~                                                                 ~~
 *                   __                               __
 *                   |  0   0   1   0   0   0   0   0  |
 *                   | 167 100  5   41 159 169 217 208 |
 *                   | 166 100  4   40 158 168 216 209 |
 *       (V|I)'^-1 = |  0   0   0   1   0   0   0   0  |
 *                   |  0   0   0   0   1   0   0   0  |
 *                   |  0   0   0   0   0   1   0   0  |
 *                   |  0   0   0   0   0   0   1   0  |
 *                   |  0   0   0   0   0   0   0   1  |
 *                   ~~                               ~~
 *
 * We can then simply compute D = (V|I)'^-1 x (d|p)' to discover the values
 * of the missing data.
 *
 * As is apparent from the example above, the only non-trivial rows in the
 * inverse matrix correspond to the data disks that we're trying to
 * reconstruct. Indeed, those are the only rows we need as the others would
 * only be useful for reconstructing data known or assumed to be valid. For
 * that reason, we only build the coefficients in the rows that correspond to
 * targeted columns.
 */
/* END CSTYLED */

static void
vdev_raidz_matrix_init(raidz_map_t *rm, int n, int nmap, int *map,
    uint8_t **rows)
{
	int i, j;
	int pow;

	ASSERT(n == rm->rm_cols - rm->rm_firstdatacol);

	/*
	 * Fill in the missing rows of interest.
	 */
	for (i = 0; i < nmap; i++) {
		ASSERT3S(0, <=, map[i]);
		ASSERT3S(map[i], <=, 2);

		pow = map[i] * n;
		if (pow > 255)
			pow -= 255;
		ASSERT(pow <= 255);

		for (j = 0; j < n; j++) {
			pow -= map[i];
			if (pow < 0)
				pow += 255;
			rows[i][j] = vdev_raidz_pow2[pow];
		}
	}
}

static void
vdev_raidz_matrix_invert(raidz_map_t *rm, int n, int nmissing, int *missing,
    uint8_t **rows, uint8_t **invrows, const uint8_t *used)
{
	int i, j, ii, jj;
	uint8_t log;

	/*
	 * Assert that the first nmissing entries from the array of used
	 * columns correspond to parity columns and that subsequent entries
	 * correspond to data columns.
	 */
	for (i = 0; i < nmissing; i++) {
		ASSERT3S(used[i], <, rm->rm_firstdatacol);
	}
	for (; i < n; i++) {
		ASSERT3S(used[i], >=, rm->rm_firstdatacol);
	}

	/*
	 * First initialize the storage where we'll compute the inverse rows.
	 */
	for (i = 0; i < nmissing; i++) {
		for (j = 0; j < n; j++) {
			invrows[i][j] = (i == j) ? 1 : 0;
		}
	}

	/*
	 * Subtract all trivial rows from the rows of consequence.
	 */
	for (i = 0; i < nmissing; i++) {
		for (j = nmissing; j < n; j++) {
			ASSERT3U(used[j], >=, rm->rm_firstdatacol);
			jj = used[j] - rm->rm_firstdatacol;
			ASSERT3S(jj, <, n);
			invrows[i][j] = rows[i][jj];
			rows[i][jj] = 0;
		}
	}

	/*
	 * For each of the rows of interest, we must normalize it and subtract
	 * a multiple of it from the other rows.
	 */
	for (i = 0; i < nmissing; i++) {
		for (j = 0; j < missing[i]; j++) {
			ASSERT0(rows[i][j]);
		}
		ASSERT3U(rows[i][missing[i]], !=, 0);

		/*
		 * Compute the inverse of the first element and multiply each
		 * element in the row by that value.
		 */
		log = 255 - vdev_raidz_log2[rows[i][missing[i]]];

		for (j = 0; j < n; j++) {
			rows[i][j] = vdev_raidz_exp2(rows[i][j], log);
			invrows[i][j] = vdev_raidz_exp2(invrows[i][j], log);
		}

		for (ii = 0; ii < nmissing; ii++) {
			if (i == ii)
				continue;

			ASSERT3U(rows[ii][missing[i]], !=, 0);

			log = vdev_raidz_log2[rows[ii][missing[i]]];

			for (j = 0; j < n; j++) {
				rows[ii][j] ^=
				    vdev_raidz_exp2(rows[i][j], log);
				invrows[ii][j] ^=
				    vdev_raidz_exp2(invrows[i][j], log);
			}
		}
	}

	/*
	 * Verify that the data that is left in the rows are properly part of
	 * an identity matrix.
	 */
	for (i = 0; i < nmissing; i++) {
		for (j = 0; j < n; j++) {
			if (j == missing[i]) {
				ASSERT3U(rows[i][j], ==, 1);
			} else {
				ASSERT0(rows[i][j]);
			}
		}
	}
}

static void
vdev_raidz_matrix_reconstruct(raidz_map_t *rm, int n, int nmissing,
    int *missing, uint8_t **invrows, const uint8_t *used)
{
	int i, j, x, cc, c;
	uint8_t *src;
	uint64_t ccount;
	uint8_t *dst[VDEV_RAIDZ_MAXPARITY];
	uint64_t dcount[VDEV_RAIDZ_MAXPARITY];
	uint8_t log = 0;
	uint8_t val;
	int ll;
	uint8_t *invlog[VDEV_RAIDZ_MAXPARITY];
	uint8_t *p, *pp;
	size_t psize;

	psize = sizeof (invlog[0][0]) * n * nmissing;
	p = kmem_alloc(psize, KM_SLEEP);

	for (pp = p, i = 0; i < nmissing; i++) {
		invlog[i] = pp;
		pp += n;
	}

	for (i = 0; i < nmissing; i++) {
		for (j = 0; j < n; j++) {
			ASSERT3U(invrows[i][j], !=, 0);
			invlog[i][j] = vdev_raidz_log2[invrows[i][j]];
		}
	}

	for (i = 0; i < n; i++) {
		c = used[i];
		ASSERT3U(c, <, rm->rm_cols);

		src = abd_to_buf(rm->rm_col[c].rc_abd);
		ccount = rm->rm_col[c].rc_size;
		for (j = 0; j < nmissing; j++) {
			cc = missing[j] + rm->rm_firstdatacol;
			ASSERT3U(cc, >=, rm->rm_firstdatacol);
			ASSERT3U(cc, <, rm->rm_cols);
			ASSERT3U(cc, !=, c);

			dst[j] = abd_to_buf(rm->rm_col[cc].rc_abd);
			dcount[j] = rm->rm_col[cc].rc_size;
		}

		ASSERT(ccount >= rm->rm_col[missing[0]].rc_size || i > 0);

		for (x = 0; x < ccount; x++, src++) {
			if (*src != 0)
				log = vdev_raidz_log2[*src];

			for (cc = 0; cc < nmissing; cc++) {
				if (x >= dcount[cc])
					continue;

				if (*src == 0) {
					val = 0;
				} else {
					if ((ll = log + invlog[cc][i]) >= 255)
						ll -= 255;
					val = vdev_raidz_pow2[ll];
				}

				if (i == 0)
					dst[cc][x] = val;
				else
					dst[cc][x] ^= val;
			}
		}
	}

	kmem_free(p, psize);
}

static int
vdev_raidz_reconstruct_general(raidz_map_t *rm, int *tgts, int ntgts)
{
	int n, i, c, t, tt;
	int nmissing_rows;
	int missing_rows[VDEV_RAIDZ_MAXPARITY];
	int parity_map[VDEV_RAIDZ_MAXPARITY];

	uint8_t *p, *pp;
	size_t psize;

	uint8_t *rows[VDEV_RAIDZ_MAXPARITY];
	uint8_t *invrows[VDEV_RAIDZ_MAXPARITY];
	uint8_t *used;

	abd_t **bufs = NULL;

	int code = 0;

	/*
	 * Matrix reconstruction can't use scatter ABDs yet, so we allocate
	 * temporary linear ABDs.
	 */
	if (!abd_is_linear(rm->rm_col[rm->rm_firstdatacol].rc_abd)) {
		bufs = kmem_alloc(rm->rm_cols * sizeof (abd_t *), KM_PUSHPAGE);

		for (c = rm->rm_firstdatacol; c < rm->rm_cols; c++) {
			raidz_col_t *col = &rm->rm_col[c];

			bufs[c] = col->rc_abd;
			col->rc_abd = abd_alloc_linear(col->rc_size, B_TRUE);
			abd_copy(col->rc_abd, bufs[c], col->rc_size);
		}
	}

	n = rm->rm_cols - rm->rm_firstdatacol;

	/*
	 * Figure out which data columns are missing.
	 */
	nmissing_rows = 0;
	for (t = 0; t < ntgts; t++) {
		if (tgts[t] >= rm->rm_firstdatacol) {
			missing_rows[nmissing_rows++] =
			    tgts[t] - rm->rm_firstdatacol;
		}
	}

	/*
	 * Figure out which parity columns to use to help generate the missing
	 * data columns.
	 */
	for (tt = 0, c = 0, i = 0; i < nmissing_rows; c++) {
		ASSERT(tt < ntgts);
		ASSERT(c < rm->rm_firstdatacol);

		/*
		 * Skip any targeted parity columns.
		 */
		if (c == tgts[tt]) {
			tt++;
			continue;
		}

		code |= 1 << c;

		parity_map[i] = c;
		i++;
	}

	ASSERT(code != 0);
	ASSERT3U(code, <, 1 << VDEV_RAIDZ_MAXPARITY);

	psize = (sizeof (rows[0][0]) + sizeof (invrows[0][0])) *
	    nmissing_rows * n + sizeof (used[0]) * n;
	p = kmem_alloc(psize, KM_SLEEP);

	for (pp = p, i = 0; i < nmissing_rows; i++) {
		rows[i] = pp;
		pp += n;
		invrows[i] = pp;
		pp += n;
	}
	used = pp;

	for (i = 0; i < nmissing_rows; i++) {
		used[i] = parity_map[i];
	}

	for (tt = 0, c = rm->rm_firstdatacol; c < rm->rm_cols; c++) {
		if (tt < nmissing_rows &&
		    c == missing_rows[tt] + rm->rm_firstdatacol) {
			tt++;
			continue;
		}

		ASSERT3S(i, <, n);
		used[i] = c;
		i++;
	}

	/*
	 * Initialize the interesting rows of the matrix.
	 */
	vdev_raidz_matrix_init(rm, n, nmissing_rows, parity_map, rows);

	/*
	 * Invert the matrix.
	 */
	vdev_raidz_matrix_invert(rm, n, nmissing_rows, missing_rows, rows,
	    invrows, used);

	/*
	 * Reconstruct the missing data using the generated matrix.
	 */
	vdev_raidz_matrix_reconstruct(rm, n, nmissing_rows, missing_rows,
	    invrows, used);

	kmem_free(p, psize);

	/*
	 * copy back from temporary linear abds and free them
	 */
	if (bufs) {
		for (c = rm->rm_firstdatacol; c < rm->rm_cols; c++) {
			raidz_col_t *col = &rm->rm_col[c];

			abd_copy(bufs[c], col->rc_abd, col->rc_size);
			abd_free(col->rc_abd);
			col->rc_abd = bufs[c];
		}
		kmem_free(bufs, rm->rm_cols * sizeof (abd_t *));
	}

	return (code);
}

static int
vdev_raidz_reconstruct(raidz_map_t *rm, int *t, int nt)
{
	int tgts[VDEV_RAIDZ_MAXPARITY], *dt;
	int ntgts;
	int i, c;
	int code;
	int nbadparity, nbaddata;
	int parity_valid[VDEV_RAIDZ_MAXPARITY];

	/*
	 * The tgts list must already be sorted.
	 */
	for (i = 1; i < nt; i++) {
		ASSERT(t[i] > t[i - 1]);
	}

	nbadparity = rm->rm_firstdatacol;
	nbaddata = rm->rm_cols - nbadparity;
	ntgts = 0;
	for (i = 0, c = 0; c < rm->rm_cols; c++) {
		if (c < rm->rm_firstdatacol)
			parity_valid[c] = B_FALSE;

		if (i < nt && c == t[i]) {
			tgts[ntgts++] = c;
			i++;
		} else if (rm->rm_col[c].rc_error != 0) {
			tgts[ntgts++] = c;
		} else if (c >= rm->rm_firstdatacol) {
			nbaddata--;
		} else {
			parity_valid[c] = B_TRUE;
			nbadparity--;
		}
	}

	ASSERT(ntgts >= nt);
	ASSERT(nbaddata >= 0);
	ASSERT(nbaddata + nbadparity == ntgts);

	dt = &tgts[nbadparity];

	/*
	 * See if we can use any of our optimized reconstruction routines.
	 */
	if (!vdev_raidz_default_to_general) {
		switch (nbaddata) {
		case 1:
			if (parity_valid[VDEV_RAIDZ_P])
				return (vdev_raidz_reconstruct_p(rm, dt, 1));

			ASSERT(rm->rm_firstdatacol > 1);

			if (parity_valid[VDEV_RAIDZ_Q])
				return (vdev_raidz_reconstruct_q(rm, dt, 1));

			ASSERT(rm->rm_firstdatacol > 2);
			break;

		case 2:
			ASSERT(rm->rm_firstdatacol > 1);

			if (parity_valid[VDEV_RAIDZ_P] &&
			    parity_valid[VDEV_RAIDZ_Q])
				return (vdev_raidz_reconstruct_pq(rm, dt, 2));

			ASSERT(rm->rm_firstdatacol > 2);

			break;
		}
	}

	code = vdev_raidz_reconstruct_general(rm, tgts, ntgts);
	ASSERT(code < (1 << VDEV_RAIDZ_MAXPARITY));
	ASSERT(code > 0);
	return (code);
}

static int
vdev_raidz_open(vdev_t *vd, uint64_t *asize, uint64_t *max_asize,
    uint64_t *logical_ashift, uint64_t *physical_ashift)
{
	vdev_t *cvd;
	uint64_t nparity = vd->vdev_nparity;
	int c;
	int lasterror = 0;
	int numerrors = 0;

	ASSERT(nparity > 0);

	if (nparity > VDEV_RAIDZ_MAXPARITY ||
	    vd->vdev_children < nparity + 1) {
		vd->vdev_stat.vs_aux = VDEV_AUX_BAD_LABEL;
		return (SET_ERROR(EINVAL));
	}

	vdev_open_children(vd);

	for (c = 0; c < vd->vdev_children; c++) {
		cvd = vd->vdev_child[c];

		if (cvd->vdev_open_error != 0) {
			lasterror = cvd->vdev_open_error;
			numerrors++;
			continue;
		}

		*asize = MIN(*asize - 1, cvd->vdev_asize - 1) + 1;
		*max_asize = MIN(*max_asize - 1, cvd->vdev_max_asize - 1) + 1;
		*logical_ashift = MAX(*logical_ashift, cvd->vdev_ashift);
		*physical_ashift = MAX(*physical_ashift,
		    cvd->vdev_physical_ashift);
	}

	*asize *= vd->vdev_children;
	*max_asize *= vd->vdev_children;

	if (numerrors > nparity) {
		vd->vdev_stat.vs_aux = VDEV_AUX_NO_REPLICAS;
		return (lasterror);
	}

	return (0);
}

static void
vdev_raidz_close(vdev_t *vd)
{
	int c;

	for (c = 0; c < vd->vdev_children; c++)
		vdev_close(vd->vdev_child[c]);
}

#ifdef illumos
/*
 * Handle a read or write I/O to a RAID-Z dump device.
 *
 * The dump device is in a unique situation compared to other ZFS datasets:
 * writing to this device should be as simple and fast as possible.  In
 * addition, durability matters much less since the dump will be extracted
 * once the machine reboots.  For that reason, this function eschews parity for
 * performance and simplicity.  The dump device uses the checksum setting
 * ZIO_CHECKSUM_NOPARITY to indicate that parity is not maintained for this
 * dataset.
 *
 * Blocks of size 128 KB have been preallocated for this volume.  I/Os less than
 * 128 KB will not fill an entire block; in addition, they may not be properly
 * aligned.  In that case, this function uses the preallocated 128 KB block and
 * omits reading or writing any "empty" portions of that block, as opposed to
 * allocating a fresh appropriately-sized block.
 *
 * Looking at an example of a 32 KB I/O to a RAID-Z vdev with 5 child vdevs:
 *
 *     vdev_raidz_io_start(data, size: 32 KB, offset: 64 KB)
 *
 * If this were a standard RAID-Z dataset, a block of at least 40 KB would be
 * allocated which spans all five child vdevs.  8 KB of data would be written to
 * each of four vdevs, with the fifth containing the parity bits.
 *
 *       parity    data     data     data     data
 *     |   PP   |   XX   |   XX   |   XX   |   XX   |
 *         ^        ^        ^        ^        ^
 *         |        |        |        |        |
 *   8 KB parity    ------8 KB data blocks------
 *
 * However, when writing to the dump device, the behavior is different:
 *
 *     vdev_raidz_physio(data, size: 32 KB, offset: 64 KB)
 *
 * Unlike the normal RAID-Z case in which the block is allocated based on the
 * I/O size, reads and writes here always use a 128 KB logical I/O size.  If the
 * I/O size is less than 128 KB, only the actual portions of data are written.
 * In this example the data is written to the third data vdev since that vdev
 * contains the offset [64 KB, 96 KB).
 *
 *       parity    data     data     data     data
 *     |        |        |        |   XX   |        |
 *                                    ^
 *                                    |
 *                             32 KB data block
 *
 * As a result, an individual I/O may not span all child vdevs; moreover, a
 * small I/O may only operate on a single child vdev.
 *
 * Note that since there are no parity bits calculated or written, this format
 * remains the same no matter how many parity bits are used in a normal RAID-Z
 * stripe.  On a RAID-Z3 configuration with seven child vdevs, the example above
 * would look like:
 *
 *       parity   parity   parity    data     data     data     data
 *     |        |        |        |        |        |   XX   |        |
 *                                                      ^
 *                                                      |
 *                                               32 KB data block
 */
int
vdev_raidz_physio(vdev_t *vd, caddr_t data, size_t size,
    uint64_t offset, uint64_t origoffset, boolean_t doread, boolean_t isdump)
{
	vdev_t *tvd = vd->vdev_top;
	vdev_t *cvd;
	raidz_map_t *rm;
	raidz_col_t *rc;
	int c, err = 0;

	uint64_t start, end, colstart, colend;
	uint64_t coloffset, colsize, colskip;

	int flags = doread ? BIO_READ : BIO_WRITE;

#ifdef	_KERNEL

	/*
	 * Don't write past the end of the block
	 */
	VERIFY3U(offset + size, <=, origoffset + SPA_OLD_MAXBLOCKSIZE);

	start = offset;
	end = start + size;

	/*
	 * Allocate a RAID-Z map for this block.  Note that this block starts
	 * from the "original" offset, this is, the offset of the extent which
	 * contains the requisite offset of the data being read or written.
	 *
	 * Even if this I/O operation doesn't span the full block size, let's
	 * treat the on-disk format as if the only blocks are the complete 128
	 * KB size.
	 */
	abd_t *abd = abd_get_from_buf(data - (offset - origoffset),
	    SPA_OLD_MAXBLOCKSIZE);
	rm = vdev_raidz_map_alloc(abd,
	    SPA_OLD_MAXBLOCKSIZE, origoffset, B_FALSE, tvd->vdev_ashift,
	    vd->vdev_children, vd->vdev_nparity);

	coloffset = origoffset;

	for (c = rm->rm_firstdatacol; c < rm->rm_cols;
	    c++, coloffset += rc->rc_size) {
		rc = &rm->rm_col[c];
		cvd = vd->vdev_child[rc->rc_devidx];

		/*
		 * Find the start and end of this column in the RAID-Z map,
		 * keeping in mind that the stated size and offset of the
		 * operation may not fill the entire column for this vdev.
		 *
		 * If any portion of the data spans this column, issue the
		 * appropriate operation to the vdev.
		 */
		if (coloffset + rc->rc_size <= start)
			continue;
		if (coloffset >= end)
			continue;

		colstart = MAX(coloffset, start);
		colend = MIN(end, coloffset + rc->rc_size);
		colsize = colend - colstart;
		colskip = colstart - coloffset;

		VERIFY3U(colsize, <=, rc->rc_size);
		VERIFY3U(colskip, <=, rc->rc_size);

		/*
		 * Note that the child vdev will have a vdev label at the start
		 * of its range of offsets, hence the need for
		 * VDEV_LABEL_OFFSET().  See zio_vdev_child_io() for another
		 * example of why this calculation is needed.
		 */
		if ((err = vdev_disk_physio(cvd,
		    ((char *)abd_to_buf(rc->rc_abd)) + colskip, colsize,
		    VDEV_LABEL_OFFSET(rc->rc_offset) + colskip,
		    flags, isdump)) != 0)
			break;
	}

	vdev_raidz_map_free(rm);
	abd_put(abd);
#endif	/* KERNEL */

	return (err);
}
#endif

static uint64_t
vdev_raidz_asize(vdev_t *vd, uint64_t psize)
{
	uint64_t asize;
	uint64_t ashift = vd->vdev_top->vdev_ashift;
	uint64_t cols = vd->vdev_children;
	uint64_t nparity = vd->vdev_nparity;

	asize = ((psize - 1) >> ashift) + 1;
	asize += nparity * ((asize + cols - nparity - 1) / (cols - nparity));
	asize = roundup(asize, nparity + 1) << ashift;

	return (asize);
}

static void
vdev_raidz_child_done(zio_t *zio)
{
	raidz_col_t *rc = zio->io_private;

	rc->rc_error = zio->io_error;
	rc->rc_tried = 1;
	rc->rc_skipped = 0;
}

static void
vdev_raidz_io_verify(zio_t *zio, raidz_map_t *rm, int col)
{
#ifdef ZFS_DEBUG
	vdev_t *vd = zio->io_vd;
	vdev_t *tvd = vd->vdev_top;

	range_seg_t logical_rs, physical_rs;
	logical_rs.rs_start = zio->io_offset;
	logical_rs.rs_end = logical_rs.rs_start +
	    vdev_raidz_asize(zio->io_vd, zio->io_size);

	raidz_col_t *rc = &rm->rm_col[col];
	vdev_t *cvd = vd->vdev_child[rc->rc_devidx];

	vdev_xlate(cvd, &logical_rs, &physical_rs);
	ASSERT3U(rc->rc_offset, ==, physical_rs.rs_start);
	ASSERT3U(rc->rc_offset, <, physical_rs.rs_end);
	/*
	 * It would be nice to assert that rs_end is equal
	 * to rc_offset + rc_size but there might be an
	 * optional I/O at the end that is not accounted in
	 * rc_size.
	 */
	if (physical_rs.rs_end > rc->rc_offset + rc->rc_size) {
		ASSERT3U(physical_rs.rs_end, ==, rc->rc_offset +
		    rc->rc_size + (1 << tvd->vdev_ashift));
	} else {
		ASSERT3U(physical_rs.rs_end, ==, rc->rc_offset + rc->rc_size);
	}
#endif
}

/*
 * Start an IO operation on a RAIDZ VDev
 *
 * Outline:
 * - For write operations:
 *   1. Generate the parity data
 *   2. Create child zio write operations to each column's vdev, for both
 *      data and parity.
 *   3. If the column skips any sectors for padding, create optional dummy
 *      write zio children for those areas to improve aggregation continuity.
 * - For read operations:
 *   1. Create child zio read operations to each data column's vdev to read
 *      the range of data required for zio.
 *   2. If this is a scrub or resilver operation, or if any of the data
 *      vdevs have had errors, then create zio read operations to the parity
 *      columns' VDevs as well.
 */
static void
vdev_raidz_io_start(zio_t *zio)
{
	vdev_t *vd = zio->io_vd;
	vdev_t *tvd = vd->vdev_top;
	vdev_t *cvd;
	raidz_map_t *rm;
	raidz_col_t *rc;
	int c, i;

	rm = vdev_raidz_map_alloc(zio->io_abd, zio->io_size, zio->io_offset,
	    zio->io_type == ZIO_TYPE_FREE,
	    tvd->vdev_ashift, vd->vdev_children,
	    vd->vdev_nparity);

	zio->io_vsd = rm;
	zio->io_vsd_ops = &vdev_raidz_vsd_ops;

	ASSERT3U(rm->rm_asize, ==, vdev_psize_to_asize(vd, zio->io_size));

	if (zio->io_type == ZIO_TYPE_FREE) {
		for (c = 0; c < rm->rm_cols; c++) {
			rc = &rm->rm_col[c];
			cvd = vd->vdev_child[rc->rc_devidx];
			zio_nowait(zio_vdev_child_io(zio, NULL, cvd,
			    rc->rc_offset, rc->rc_abd, rc->rc_size,
			    zio->io_type, zio->io_priority, 0,
			    vdev_raidz_child_done, rc));
		}

		zio_execute(zio);
		return;
	}

	if (zio->io_type == ZIO_TYPE_WRITE) {
		vdev_raidz_generate_parity(rm);

		for (c = 0; c < rm->rm_cols; c++) {
			rc = &rm->rm_col[c];
			cvd = vd->vdev_child[rc->rc_devidx];

			/*
			 * Verify physical to logical translation.
			 */
			vdev_raidz_io_verify(zio, rm, c);

			zio_nowait(zio_vdev_child_io(zio, NULL, cvd,
			    rc->rc_offset, rc->rc_abd, rc->rc_size,
			    zio->io_type, zio->io_priority, 0,
			    vdev_raidz_child_done, rc));
		}

		/*
		 * Generate optional I/Os for any skipped sectors to improve
		 * aggregation contiguity.
		 */
		for (c = rm->rm_skipstart, i = 0; i < rm->rm_nskip; c++, i++) {
			ASSERT(c <= rm->rm_scols);
			if (c == rm->rm_scols)
				c = 0;
			rc = &rm->rm_col[c];
			cvd = vd->vdev_child[rc->rc_devidx];
			zio_nowait(zio_vdev_child_io(zio, NULL, cvd,
			    rc->rc_offset + rc->rc_size, NULL,
			    1 << tvd->vdev_ashift,
			    zio->io_type, zio->io_priority,
			    ZIO_FLAG_NODATA | ZIO_FLAG_OPTIONAL, NULL, NULL));
		}

		zio_execute(zio);
		return;
	}

	ASSERT3U(zio->io_type, ==, ZIO_TYPE_READ);

	/*
	 * Iterate over the columns in reverse order so that we hit the parity
	 * last -- any errors along the way will force us to read the parity.
	 */
	for (c = rm->rm_cols - 1; c >= 0; c--) {
		rc = &rm->rm_col[c];
		cvd = vd->vdev_child[rc->rc_devidx];
		if (!vdev_readable(cvd)) {
			if (c >= rm->rm_firstdatacol)
				rm->rm_missingdata++;
			else
				rm->rm_missingparity++;
			rc->rc_error = SET_ERROR(ENXIO);
			rc->rc_tried = 1;	/* don't even try */
			rc->rc_skipped = 1;
			continue;
		}
		if (vdev_dtl_contains(cvd, DTL_MISSING, zio->io_txg, 1)) {
			if (c >= rm->rm_firstdatacol)
				rm->rm_missingdata++;
			else
				rm->rm_missingparity++;
			rc->rc_error = SET_ERROR(ESTALE);
			rc->rc_skipped = 1;
			continue;
		}
		if (c >= rm->rm_firstdatacol || rm->rm_missingdata > 0 ||
		    (zio->io_flags & (ZIO_FLAG_SCRUB | ZIO_FLAG_RESILVER))) {
			zio_nowait(zio_vdev_child_io(zio, NULL, cvd,
			    rc->rc_offset, rc->rc_abd, rc->rc_size,
			    zio->io_type, zio->io_priority, 0,
			    vdev_raidz_child_done, rc));
		}
	}

	zio_execute(zio);
}


/*
 * Report a checksum error for a child of a RAID-Z device.
 */
static void
raidz_checksum_error(zio_t *zio, raidz_col_t *rc, void *bad_data)
{
	void *buf;
	vdev_t *vd = zio->io_vd->vdev_child[rc->rc_devidx];

	if (!(zio->io_flags & ZIO_FLAG_SPECULATIVE)) {
		zio_bad_cksum_t zbc;
		raidz_map_t *rm = zio->io_vsd;

		mutex_enter(&vd->vdev_stat_lock);
		vd->vdev_stat.vs_checksum_errors++;
		mutex_exit(&vd->vdev_stat_lock);

		zbc.zbc_has_cksum = 0;
		zbc.zbc_injected = rm->rm_ecksuminjected;

		buf = abd_borrow_buf_copy(rc->rc_abd, rc->rc_size);
		zfs_ereport_post_checksum(zio->io_spa, vd, zio,
		    rc->rc_offset, rc->rc_size, buf, bad_data,
		    &zbc);
		abd_return_buf(rc->rc_abd, buf, rc->rc_size);
	}
}

/*
 * We keep track of whether or not there were any injected errors, so that
 * any ereports we generate can note it.
 */
static int
raidz_checksum_verify(zio_t *zio)
{
	zio_bad_cksum_t zbc;
	raidz_map_t *rm = zio->io_vsd;

	int ret = zio_checksum_error(zio, &zbc);
	if (ret != 0 && zbc.zbc_injected != 0)
		rm->rm_ecksuminjected = 1;

	return (ret);
}

/*
 * Generate the parity from the data columns. If we tried and were able to
 * read the parity without error, verify that the generated parity matches the
 * data we read. If it doesn't, we fire off a checksum error. Return the
 * number such failures.
 */
static int
raidz_parity_verify(zio_t *zio, raidz_map_t *rm)
{
	void *orig[VDEV_RAIDZ_MAXPARITY];
	int c, ret = 0;
	raidz_col_t *rc;

	blkptr_t *bp = zio->io_bp;
	enum zio_checksum checksum = (bp == NULL ? zio->io_prop.zp_checksum :
	    (BP_IS_GANG(bp) ? ZIO_CHECKSUM_GANG_HEADER : BP_GET_CHECKSUM(bp)));

	if (checksum == ZIO_CHECKSUM_NOPARITY)
		return (ret);

	for (c = 0; c < rm->rm_firstdatacol; c++) {
		rc = &rm->rm_col[c];
		if (!rc->rc_tried || rc->rc_error != 0)
			continue;
		orig[c] = zio_buf_alloc(rc->rc_size);
		abd_copy_to_buf(orig[c], rc->rc_abd, rc->rc_size);
	}

	vdev_raidz_generate_parity(rm);

	for (c = 0; c < rm->rm_firstdatacol; c++) {
		rc = &rm->rm_col[c];
		if (!rc->rc_tried || rc->rc_error != 0)
			continue;
		if (abd_cmp_buf(rc->rc_abd, orig[c], rc->rc_size) != 0) {
			raidz_checksum_error(zio, rc, orig[c]);
			rc->rc_error = SET_ERROR(ECKSUM);
			ret++;
		}
		zio_buf_free(orig[c], rc->rc_size);
	}

	return (ret);
}

/*
 * Keep statistics on all the ways that we used parity to correct data.
 */
static uint64_t raidz_corrected[1 << VDEV_RAIDZ_MAXPARITY];

static int
vdev_raidz_worst_error(raidz_map_t *rm)
{
	int error = 0;

	for (int c = 0; c < rm->rm_cols; c++)
		error = zio_worst_error(error, rm->rm_col[c].rc_error);

	return (error);
}

/*
 * Iterate over all combinations of bad data and attempt a reconstruction.
 * Note that the algorithm below is non-optimal because it doesn't take into
 * account how reconstruction is actually performed. For example, with
 * triple-parity RAID-Z the reconstruction procedure is the same if column 4
 * is targeted as invalid as if columns 1 and 4 are targeted since in both
 * cases we'd only use parity information in column 0.
 */
static int
vdev_raidz_combrec(zio_t *zio, int total_errors, int data_errors)
{
	raidz_map_t *rm = zio->io_vsd;
	raidz_col_t *rc;
	void *orig[VDEV_RAIDZ_MAXPARITY];
	int tstore[VDEV_RAIDZ_MAXPARITY + 2];
	int *tgts = &tstore[1];
	int current, next, i, c, n;
	int code, ret = 0;

	ASSERT(total_errors < rm->rm_firstdatacol);

	/*
	 * This simplifies one edge condition.
	 */
	tgts[-1] = -1;

	for (n = 1; n <= rm->rm_firstdatacol - total_errors; n++) {
		/*
		 * Initialize the targets array by finding the first n columns
		 * that contain no error.
		 *
		 * If there were no data errors, we need to ensure that we're
		 * always explicitly attempting to reconstruct at least one
		 * data column. To do this, we simply push the highest target
		 * up into the data columns.
		 */
		for (c = 0, i = 0; i < n; i++) {
			if (i == n - 1 && data_errors == 0 &&
			    c < rm->rm_firstdatacol) {
				c = rm->rm_firstdatacol;
			}

			while (rm->rm_col[c].rc_error != 0) {
				c++;
				ASSERT3S(c, <, rm->rm_cols);
			}

			tgts[i] = c++;
		}

		/*
		 * Setting tgts[n] simplifies the other edge condition.
		 */
		tgts[n] = rm->rm_cols;

		/*
		 * These buffers were allocated in previous iterations.
		 */
		for (i = 0; i < n - 1; i++) {
			ASSERT(orig[i] != NULL);
		}

		orig[n - 1] = zio_buf_alloc(rm->rm_col[0].rc_size);

		current = 0;
		next = tgts[current];

		while (current != n) {
			tgts[current] = next;
			current = 0;

			/*
			 * Save off the original data that we're going to
			 * attempt to reconstruct.
			 */
			for (i = 0; i < n; i++) {
				ASSERT(orig[i] != NULL);
				c = tgts[i];
				ASSERT3S(c, >=, 0);
				ASSERT3S(c, <, rm->rm_cols);
				rc = &rm->rm_col[c];
				abd_copy_to_buf(orig[i], rc->rc_abd,
				    rc->rc_size);
			}

			/*
			 * Attempt a reconstruction and exit the outer loop on
			 * success.
			 */
			code = vdev_raidz_reconstruct(rm, tgts, n);
			if (raidz_checksum_verify(zio) == 0) {
				atomic_inc_64(&raidz_corrected[code]);

				for (i = 0; i < n; i++) {
					c = tgts[i];
					rc = &rm->rm_col[c];
					ASSERT(rc->rc_error == 0);
					if (rc->rc_tried)
						raidz_checksum_error(zio, rc,
						    orig[i]);
					rc->rc_error = SET_ERROR(ECKSUM);
				}

				ret = code;
				goto done;
			}

			/*
			 * Restore the original data.
			 */
			for (i = 0; i < n; i++) {
				c = tgts[i];
				rc = &rm->rm_col[c];
				abd_copy_from_buf(rc->rc_abd, orig[i],
				    rc->rc_size);
			}

			do {
				/*
				 * Find the next valid column after the current
				 * position..
				 */
				for (next = tgts[current] + 1;
				    next < rm->rm_cols &&
				    rm->rm_col[next].rc_error != 0; next++)
					continue;

				ASSERT(next <= tgts[current + 1]);

				/*
				 * If that spot is available, we're done here.
				 */
				if (next != tgts[current + 1])
					break;

				/*
				 * Otherwise, find the next valid column after
				 * the previous position.
				 */
				for (c = tgts[current - 1] + 1;
				    rm->rm_col[c].rc_error != 0; c++)
					continue;

				tgts[current] = c;
				current++;

			} while (current != n);
		}
	}
	n--;
done:
	for (i = 0; i < n; i++) {
		zio_buf_free(orig[i], rm->rm_col[0].rc_size);
	}

	return (ret);
}

/*
 * Complete an IO operation on a RAIDZ VDev
 *
 * Outline:
 * - For write operations:
 *   1. Check for errors on the child IOs.
 *   2. Return, setting an error code if too few child VDevs were written
 *      to reconstruct the data later.  Note that partial writes are
 *      considered successful if they can be reconstructed at all.
 * - For read operations:
 *   1. Check for errors on the child IOs.
 *   2. If data errors occurred:
 *      a. Try to reassemble the data from the parity available.
 *      b. If we haven't yet read the parity drives, read them now.
 *      c. If all parity drives have been read but the data still doesn't
 *         reassemble with a correct checksum, then try combinatorial
 *         reconstruction.
 *      d. If that doesn't work, return an error.
 *   3. If there were unexpected errors or this is a resilver operation,
 *      rewrite the vdevs that had errors.
 */
static void
vdev_raidz_io_done(zio_t *zio)
{
	vdev_t *vd = zio->io_vd;
	vdev_t *cvd;
	raidz_map_t *rm = zio->io_vsd;
	raidz_col_t *rc;
	int unexpected_errors = 0;
	int parity_errors = 0;
	int parity_untried = 0;
	int data_errors = 0;
	int total_errors = 0;
	int n, c;
	int tgts[VDEV_RAIDZ_MAXPARITY];
	int code;

	ASSERT(zio->io_bp != NULL);  /* XXX need to add code to enforce this */

	ASSERT(rm->rm_missingparity <= rm->rm_firstdatacol);
	ASSERT(rm->rm_missingdata <= rm->rm_cols - rm->rm_firstdatacol);

	for (c = 0; c < rm->rm_cols; c++) {
		rc = &rm->rm_col[c];

		if (rc->rc_error) {
			ASSERT(rc->rc_error != ECKSUM);	/* child has no bp */

			if (c < rm->rm_firstdatacol)
				parity_errors++;
			else
				data_errors++;

			if (!rc->rc_skipped)
				unexpected_errors++;

			total_errors++;
		} else if (c < rm->rm_firstdatacol && !rc->rc_tried) {
			parity_untried++;
		}
	}

	if (zio->io_type == ZIO_TYPE_WRITE) {
		/*
		 * XXX -- for now, treat partial writes as a success.
		 * (If we couldn't write enough columns to reconstruct
		 * the data, the I/O failed.  Otherwise, good enough.)
		 *
		 * Now that we support write reallocation, it would be better
		 * to treat partial failure as real failure unless there are
		 * no non-degraded top-level vdevs left, and not update DTLs
		 * if we intend to reallocate.
		 */
		/* XXPOLICY */
		if (total_errors > rm->rm_firstdatacol)
			zio->io_error = vdev_raidz_worst_error(rm);

		return;
	} else if (zio->io_type == ZIO_TYPE_FREE) {
		return;
	}

	ASSERT(zio->io_type == ZIO_TYPE_READ);
	/*
	 * There are three potential phases for a read:
	 *	1. produce valid data from the columns read
	 *	2. read all disks and try again
	 *	3. perform combinatorial reconstruction
	 *
	 * Each phase is progressively both more expensive and less likely to
	 * occur. If we encounter more errors than we can repair or all phases
	 * fail, we have no choice but to return an error.
	 */

	/*
	 * If the number of errors we saw was correctable -- less than or equal
	 * to the number of parity disks read -- attempt to produce data that
	 * has a valid checksum. Naturally, this case applies in the absence of
	 * any errors.
	 */
	if (total_errors <= rm->rm_firstdatacol - parity_untried) {
		if (data_errors == 0) {
			if (raidz_checksum_verify(zio) == 0) {
				/*
				 * If we read parity information (unnecessarily
				 * as it happens since no reconstruction was
				 * needed) regenerate and verify the parity.
				 * We also regenerate parity when resilvering
				 * so we can write it out to the failed device
				 * later.
				 */
				if (parity_errors + parity_untried <
				    rm->rm_firstdatacol ||
				    (zio->io_flags & ZIO_FLAG_RESILVER)) {
					n = raidz_parity_verify(zio, rm);
					unexpected_errors += n;
					ASSERT(parity_errors + n <=
					    rm->rm_firstdatacol);
				}
				goto done;
			}
		} else {
			/*
			 * We either attempt to read all the parity columns or
			 * none of them. If we didn't try to read parity, we
			 * wouldn't be here in the correctable case. There must
			 * also have been fewer parity errors than parity
			 * columns or, again, we wouldn't be in this code path.
			 */
			ASSERT(parity_untried == 0);
			ASSERT(parity_errors < rm->rm_firstdatacol);

			/*
			 * Identify the data columns that reported an error.
			 */
			n = 0;
			for (c = rm->rm_firstdatacol; c < rm->rm_cols; c++) {
				rc = &rm->rm_col[c];
				if (rc->rc_error != 0) {
					ASSERT(n < VDEV_RAIDZ_MAXPARITY);
					tgts[n++] = c;
				}
			}

			ASSERT(rm->rm_firstdatacol >= n);

			code = vdev_raidz_reconstruct(rm, tgts, n);

			if (raidz_checksum_verify(zio) == 0) {
				atomic_inc_64(&raidz_corrected[code]);

				/*
				 * If we read more parity disks than were used
				 * for reconstruction, confirm that the other
				 * parity disks produced correct data. This
				 * routine is suboptimal in that it regenerates
				 * the parity that we already used in addition
				 * to the parity that we're attempting to
				 * verify, but this should be a relatively
				 * uncommon case, and can be optimized if it
				 * becomes a problem. Note that we regenerate
				 * parity when resilvering so we can write it
				 * out to failed devices later.
				 */
				if (parity_errors < rm->rm_firstdatacol - n ||
				    (zio->io_flags & ZIO_FLAG_RESILVER)) {
					n = raidz_parity_verify(zio, rm);
					unexpected_errors += n;
					ASSERT(parity_errors + n <=
					    rm->rm_firstdatacol);
				}

				goto done;
			}
		}
	}

	/*
	 * This isn't a typical situation -- either we got a read error or
	 * a child silently returned bad data. Read every block so we can
	 * try again with as much data and parity as we can track down. If
	 * we've already been through once before, all children will be marked
	 * as tried so we'll proceed to combinatorial reconstruction.
	 */
	unexpected_errors = 1;
	rm->rm_missingdata = 0;
	rm->rm_missingparity = 0;

	for (c = 0; c < rm->rm_cols; c++) {
		if (rm->rm_col[c].rc_tried)
			continue;

		zio_vdev_io_redone(zio);
		do {
			rc = &rm->rm_col[c];
			if (rc->rc_tried)
				continue;
			zio_nowait(zio_vdev_child_io(zio, NULL,
			    vd->vdev_child[rc->rc_devidx],
			    rc->rc_offset, rc->rc_abd, rc->rc_size,
			    zio->io_type, zio->io_priority, 0,
			    vdev_raidz_child_done, rc));
		} while (++c < rm->rm_cols);

		return;
	}

	/*
	 * At this point we've attempted to reconstruct the data given the
	 * errors we detected, and we've attempted to read all columns. There
	 * must, therefore, be one or more additional problems -- silent errors
	 * resulting in invalid data rather than explicit I/O errors resulting
	 * in absent data. We check if there is enough additional data to
	 * possibly reconstruct the data and then perform combinatorial
	 * reconstruction over all possible combinations. If that fails,
	 * we're cooked.
	 */
	if (total_errors > rm->rm_firstdatacol) {
		zio->io_error = vdev_raidz_worst_error(rm);

	} else if (total_errors < rm->rm_firstdatacol &&
	    (code = vdev_raidz_combrec(zio, total_errors, data_errors)) != 0) {
		/*
		 * If we didn't use all the available parity for the
		 * combinatorial reconstruction, verify that the remaining
		 * parity is correct.
		 */
		if (code != (1 << rm->rm_firstdatacol) - 1)
			(void) raidz_parity_verify(zio, rm);
	} else {
		/*
		 * We're here because either:
		 *
		 *	total_errors == rm_firstdatacol, or
		 *	vdev_raidz_combrec() failed
		 *
		 * In either case, there is enough bad data to prevent
		 * reconstruction.
		 *
		 * Start checksum ereports for all children which haven't
		 * failed, and the IO wasn't speculative.
		 */
		zio->io_error = SET_ERROR(ECKSUM);

		if (!(zio->io_flags & ZIO_FLAG_SPECULATIVE)) {
			for (c = 0; c < rm->rm_cols; c++) {
				rc = &rm->rm_col[c];
				if (rc->rc_error == 0) {
					zio_bad_cksum_t zbc;
					zbc.zbc_has_cksum = 0;
					zbc.zbc_injected =
					    rm->rm_ecksuminjected;

					zfs_ereport_start_checksum(
					    zio->io_spa,
					    vd->vdev_child[rc->rc_devidx],
					    zio, rc->rc_offset, rc->rc_size,
					    (void *)(uintptr_t)c, &zbc);
				}
			}
		}
	}

done:
	zio_checksum_verified(zio);

	if (zio->io_error == 0 && spa_writeable(zio->io_spa) &&
	    (unexpected_errors || (zio->io_flags & ZIO_FLAG_RESILVER))) {
		/*
		 * Use the good data we have in hand to repair damaged children.
		 */
		for (c = 0; c < rm->rm_cols; c++) {
			rc = &rm->rm_col[c];
			cvd = vd->vdev_child[rc->rc_devidx];

			if (rc->rc_error == 0)
				continue;

			zio_nowait(zio_vdev_child_io(zio, NULL, cvd,
			    rc->rc_offset, rc->rc_abd, rc->rc_size,
			    ZIO_TYPE_WRITE, ZIO_PRIORITY_ASYNC_WRITE,
			    ZIO_FLAG_IO_REPAIR | (unexpected_errors ?
			    ZIO_FLAG_SELF_HEAL : 0), NULL, NULL));
		}
	}
}

static void
vdev_raidz_state_change(vdev_t *vd, int faulted, int degraded)
{
	if (faulted > vd->vdev_nparity)
		vdev_set_state(vd, B_FALSE, VDEV_STATE_CANT_OPEN,
		    VDEV_AUX_NO_REPLICAS);
	else if (degraded + faulted != 0)
		vdev_set_state(vd, B_FALSE, VDEV_STATE_DEGRADED, VDEV_AUX_NONE);
	else
		vdev_set_state(vd, B_FALSE, VDEV_STATE_HEALTHY, VDEV_AUX_NONE);
}

/*
 * Determine if any portion of the provided block resides on a child vdev
 * with a dirty DTL and therefore needs to be resilvered.  The function
 * assumes that at least one DTL is dirty which imples that full stripe
 * width blocks must be resilvered.
 */
static boolean_t
vdev_raidz_need_resilver(vdev_t *vd, uint64_t offset, size_t psize)
{
	uint64_t dcols = vd->vdev_children;
	uint64_t nparity = vd->vdev_nparity;
	uint64_t ashift = vd->vdev_top->vdev_ashift;
	/* The starting RAIDZ (parent) vdev sector of the block. */
	uint64_t b = offset >> ashift;
	/* The zio's size in units of the vdev's minimum sector size. */
	uint64_t s = ((psize - 1) >> ashift) + 1;
	/* The first column for this stripe. */
	uint64_t f = b % dcols;

	if (s + nparity >= dcols)
		return (B_TRUE);

	for (uint64_t c = 0; c < s + nparity; c++) {
		uint64_t devidx = (f + c) % dcols;
		vdev_t *cvd = vd->vdev_child[devidx];

		/*
		 * dsl_scan_need_resilver() already checked vd with
		 * vdev_dtl_contains(). So here just check cvd with
		 * vdev_dtl_empty(), cheaper and a good approximation.
		 */
		if (!vdev_dtl_empty(cvd, DTL_PARTIAL))
			return (B_TRUE);
	}

	return (B_FALSE);
}

static void
vdev_raidz_xlate(vdev_t *cvd, const range_seg_t *in, range_seg_t *res)
{
	vdev_t *raidvd = cvd->vdev_parent;
	ASSERT(raidvd->vdev_ops == &vdev_raidz_ops);

	uint64_t width = raidvd->vdev_children;
	uint64_t tgt_col = cvd->vdev_id;
	uint64_t ashift = raidvd->vdev_top->vdev_ashift;

	/* make sure the offsets are block-aligned */
	ASSERT0(in->rs_start % (1 << ashift));
	ASSERT0(in->rs_end % (1 << ashift));
	uint64_t b_start = in->rs_start >> ashift;
	uint64_t b_end = in->rs_end >> ashift;

	uint64_t start_row = 0;
	if (b_start > tgt_col) /* avoid underflow */
		start_row = ((b_start - tgt_col - 1) / width) + 1;

	uint64_t end_row = 0;
	if (b_end > tgt_col)
		end_row = ((b_end - tgt_col - 1) / width) + 1;

	res->rs_start = start_row << ashift;
	res->rs_end = end_row << ashift;

	ASSERT3U(res->rs_start, <=, in->rs_start);
	ASSERT3U(res->rs_end - res->rs_start, <=, in->rs_end - in->rs_start);
}

vdev_ops_t vdev_raidz_ops = {
	vdev_raidz_open,
	vdev_raidz_close,
	vdev_raidz_asize,
	vdev_raidz_io_start,
	vdev_raidz_io_done,
	vdev_raidz_state_change,
	vdev_raidz_need_resilver,
	NULL,
	NULL,
	NULL,
	vdev_raidz_xlate,
	VDEV_TYPE_RAIDZ,	/* name of this vdev type */
	B_FALSE			/* not a leaf vdev */
};
