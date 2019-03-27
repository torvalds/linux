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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

static uint64_t zfs_crc64_table[256];

#define	ECKSUM	666

#define	ASSERT3S(x, y, z)	((void)0)
#define	ASSERT3U(x, y, z)	((void)0)
#define	ASSERT3P(x, y, z)	((void)0)
#define	ASSERT0(x)		((void)0)
#define	ASSERT(x)		((void)0)

#define	panic(...)	do {						\
	printf(__VA_ARGS__);						\
	for (;;) ;							\
} while (0)

#define	kmem_alloc(size, flag)	zfs_alloc((size))
#define	kmem_free(ptr, size)	zfs_free((ptr), (size))

static void
zfs_init_crc(void)
{
	int i, j;
	uint64_t *ct;

	/*
	 * Calculate the crc64 table (used for the zap hash
	 * function).
	 */
	if (zfs_crc64_table[128] != ZFS_CRC64_POLY) {
		memset(zfs_crc64_table, 0, sizeof(zfs_crc64_table));
		for (i = 0; i < 256; i++)
			for (ct = zfs_crc64_table + i, *ct = i, j = 8; j > 0; j--)
				*ct = (*ct >> 1) ^ (-(*ct & 1) & ZFS_CRC64_POLY);
	}
}

static void
zio_checksum_off(const void *buf, uint64_t size,
    const void *ctx_template, zio_cksum_t *zcp)
{
	ZIO_SET_CHECKSUM(zcp, 0, 0, 0, 0);
}

/*
 * Signature for checksum functions.
 */
typedef void zio_checksum_t(const void *data, uint64_t size,
    const void *ctx_template, zio_cksum_t *zcp);
typedef void *zio_checksum_tmpl_init_t(const zio_cksum_salt_t *salt);
typedef void zio_checksum_tmpl_free_t(void *ctx_template);

typedef enum zio_checksum_flags {
	/* Strong enough for metadata? */
	ZCHECKSUM_FLAG_METADATA = (1 << 1),
	/* ZIO embedded checksum */
	ZCHECKSUM_FLAG_EMBEDDED = (1 << 2),
	/* Strong enough for dedup (without verification)? */
	ZCHECKSUM_FLAG_DEDUP = (1 << 3),
	/* Uses salt value */
	ZCHECKSUM_FLAG_SALTED = (1 << 4),
	/* Strong enough for nopwrite? */
	ZCHECKSUM_FLAG_NOPWRITE = (1 << 5)
} zio_checksum_flags_t;

/*
 * Information about each checksum function.
 */
typedef struct zio_checksum_info {
	/* checksum function for each byteorder */
	zio_checksum_t			*ci_func[2];
	zio_checksum_tmpl_init_t	*ci_tmpl_init;
	zio_checksum_tmpl_free_t	*ci_tmpl_free;
	zio_checksum_flags_t		ci_flags;
	const char			*ci_name;	/* descriptive name */
} zio_checksum_info_t;

#include "blkptr.c"

#include "fletcher.c"
#include "sha256.c"
#include "skein_zfs.c"

static zio_checksum_info_t zio_checksum_table[ZIO_CHECKSUM_FUNCTIONS] = {
	{{NULL, NULL}, NULL, NULL, 0, "inherit"},
	{{NULL, NULL}, NULL, NULL, 0, "on"},
	{{zio_checksum_off,	zio_checksum_off}, NULL, NULL, 0, "off"},
	{{zio_checksum_SHA256,	zio_checksum_SHA256}, NULL, NULL,
	    ZCHECKSUM_FLAG_METADATA | ZCHECKSUM_FLAG_EMBEDDED, "label"},
	{{zio_checksum_SHA256,	zio_checksum_SHA256}, NULL, NULL,
	    ZCHECKSUM_FLAG_METADATA | ZCHECKSUM_FLAG_EMBEDDED, "gang_header"},
	{{fletcher_2_native,	fletcher_2_byteswap}, NULL, NULL,
	    ZCHECKSUM_FLAG_EMBEDDED, "zilog"},
	{{fletcher_2_native,	fletcher_2_byteswap}, NULL, NULL,
	    0, "fletcher2"},
	{{fletcher_4_native,	fletcher_4_byteswap}, NULL, NULL,
	    ZCHECKSUM_FLAG_METADATA, "fletcher4"},
	{{zio_checksum_SHA256,	zio_checksum_SHA256}, NULL, NULL,
	    ZCHECKSUM_FLAG_METADATA | ZCHECKSUM_FLAG_DEDUP |
	    ZCHECKSUM_FLAG_NOPWRITE, "SHA256"},
	{{fletcher_4_native,	fletcher_4_byteswap}, NULL, NULL,
	    ZCHECKSUM_FLAG_EMBEDDED, "zillog2"},
	{{zio_checksum_off,	zio_checksum_off}, NULL, NULL,
	    0, "noparity"},
	{{zio_checksum_SHA512_native,	zio_checksum_SHA512_byteswap},
	    NULL, NULL, ZCHECKSUM_FLAG_METADATA | ZCHECKSUM_FLAG_DEDUP |
	    ZCHECKSUM_FLAG_NOPWRITE, "SHA512"},
	{{zio_checksum_skein_native, zio_checksum_skein_byteswap},
	    zio_checksum_skein_tmpl_init, zio_checksum_skein_tmpl_free,
	    ZCHECKSUM_FLAG_METADATA | ZCHECKSUM_FLAG_DEDUP |
	    ZCHECKSUM_FLAG_SALTED | ZCHECKSUM_FLAG_NOPWRITE, "skein"},
	/* no edonr for now */
	{{NULL, NULL}, NULL, NULL, ZCHECKSUM_FLAG_METADATA |
	    ZCHECKSUM_FLAG_SALTED | ZCHECKSUM_FLAG_NOPWRITE, "edonr"}
};

/*
 * Common signature for all zio compress/decompress functions.
 */
typedef size_t zio_compress_func_t(void *src, void *dst,
    size_t s_len, size_t d_len, int);
typedef int zio_decompress_func_t(void *src, void *dst,
    size_t s_len, size_t d_len, int);

/*
 * Information about each compression function.
 */
typedef struct zio_compress_info {
	zio_compress_func_t	*ci_compress;	/* compression function */
	zio_decompress_func_t	*ci_decompress;	/* decompression function */
	int			ci_level;	/* level parameter */
	const char		*ci_name;	/* algorithm name */
} zio_compress_info_t;

#include "lzjb.c"
#include "zle.c"
#include "lz4.c"

/*
 * Compression vectors.
 */
static zio_compress_info_t zio_compress_table[ZIO_COMPRESS_FUNCTIONS] = {
	{NULL,			NULL,			0,	"inherit"},
	{NULL,			NULL,			0,	"on"},
	{NULL,			NULL,			0,	"uncompressed"},
	{NULL,			lzjb_decompress,	0,	"lzjb"},
	{NULL,			NULL,			0,	"empty"},
	{NULL,			NULL,			1,	"gzip-1"},
	{NULL,			NULL,			2,	"gzip-2"},
	{NULL,			NULL,			3,	"gzip-3"},
	{NULL,			NULL,			4,	"gzip-4"},
	{NULL,			NULL,			5,	"gzip-5"},
	{NULL,			NULL,			6,	"gzip-6"},
	{NULL,			NULL,			7,	"gzip-7"},
	{NULL,			NULL,			8,	"gzip-8"},
	{NULL,			NULL,			9,	"gzip-9"},
	{NULL,			zle_decompress,		64,	"zle"},
	{NULL,			lz4_decompress,		0,	"lz4"},
};

static void
byteswap_uint64_array(void *vbuf, size_t size)
{
	uint64_t *buf = vbuf;
	size_t count = size >> 3;
	int i;

	ASSERT((size & 7) == 0);

	for (i = 0; i < count; i++)
		buf[i] = BSWAP_64(buf[i]);
}

/*
 * Set the external verifier for a gang block based on <vdev, offset, txg>,
 * a tuple which is guaranteed to be unique for the life of the pool.
 */
static void
zio_checksum_gang_verifier(zio_cksum_t *zcp, const blkptr_t *bp)
{
	const dva_t *dva = BP_IDENTITY(bp);
	uint64_t txg = BP_PHYSICAL_BIRTH(bp);

	ASSERT(BP_IS_GANG(bp));

	ZIO_SET_CHECKSUM(zcp, DVA_GET_VDEV(dva), DVA_GET_OFFSET(dva), txg, 0);
}

/*
 * Set the external verifier for a label block based on its offset.
 * The vdev is implicit, and the txg is unknowable at pool open time --
 * hence the logic in vdev_uberblock_load() to find the most recent copy.
 */
static void
zio_checksum_label_verifier(zio_cksum_t *zcp, uint64_t offset)
{
	ZIO_SET_CHECKSUM(zcp, offset, 0, 0, 0);
}

/*
 * Calls the template init function of a checksum which supports context
 * templates and installs the template into the spa_t.
 */
static void
zio_checksum_template_init(enum zio_checksum checksum, spa_t *spa)
{
	zio_checksum_info_t *ci = &zio_checksum_table[checksum];

	if (ci->ci_tmpl_init == NULL)
		return;

	if (spa->spa_cksum_tmpls[checksum] != NULL)
		return;

	if (spa->spa_cksum_tmpls[checksum] == NULL) {
		spa->spa_cksum_tmpls[checksum] =
		    ci->ci_tmpl_init(&spa->spa_cksum_salt);
	}
}

/*
 * Called by a spa_t that's about to be deallocated. This steps through
 * all of the checksum context templates and deallocates any that were
 * initialized using the algorithm-specific template init function.
 */
static void __unused
zio_checksum_templates_free(spa_t *spa)
{
	for (enum zio_checksum checksum = 0;
	    checksum < ZIO_CHECKSUM_FUNCTIONS; checksum++) {
		if (spa->spa_cksum_tmpls[checksum] != NULL) {
			zio_checksum_info_t *ci = &zio_checksum_table[checksum];

			ci->ci_tmpl_free(spa->spa_cksum_tmpls[checksum]);
			spa->spa_cksum_tmpls[checksum] = NULL;
		}
	}
}

static int
zio_checksum_verify(const spa_t *spa, const blkptr_t *bp, void *data)
{
	uint64_t size;
	unsigned int checksum;
	zio_checksum_info_t *ci;
	void *ctx = NULL;
	zio_cksum_t actual_cksum, expected_cksum, verifier;
	int byteswap;

	checksum = BP_GET_CHECKSUM(bp);
	size = BP_GET_PSIZE(bp);

	if (checksum >= ZIO_CHECKSUM_FUNCTIONS)
		return (EINVAL);
	ci = &zio_checksum_table[checksum];
	if (ci->ci_func[0] == NULL || ci->ci_func[1] == NULL)
		return (EINVAL);

	if (spa != NULL) {
		zio_checksum_template_init(checksum, __DECONST(spa_t *,spa));
		ctx = spa->spa_cksum_tmpls[checksum];
	}

	if (ci->ci_flags & ZCHECKSUM_FLAG_EMBEDDED) {
		zio_eck_t *eck;

		ASSERT(checksum == ZIO_CHECKSUM_GANG_HEADER ||
		    checksum == ZIO_CHECKSUM_LABEL);

		eck = (zio_eck_t *)((char *)data + size) - 1;

		if (checksum == ZIO_CHECKSUM_GANG_HEADER)
			zio_checksum_gang_verifier(&verifier, bp);
		else if (checksum == ZIO_CHECKSUM_LABEL)
			zio_checksum_label_verifier(&verifier,
			    DVA_GET_OFFSET(BP_IDENTITY(bp)));
		else
			verifier = bp->blk_cksum;

		byteswap = (eck->zec_magic == BSWAP_64(ZEC_MAGIC));

		if (byteswap)
			byteswap_uint64_array(&verifier, sizeof (zio_cksum_t));

		expected_cksum = eck->zec_cksum;
		eck->zec_cksum = verifier;
		ci->ci_func[byteswap](data, size, ctx, &actual_cksum);
		eck->zec_cksum = expected_cksum;

		if (byteswap)
			byteswap_uint64_array(&expected_cksum,
			    sizeof (zio_cksum_t));
	} else {
		expected_cksum = bp->blk_cksum;
		ci->ci_func[0](data, size, ctx, &actual_cksum);
	}

	if (!ZIO_CHECKSUM_EQUAL(actual_cksum, expected_cksum)) {
		/*printf("ZFS: read checksum %s failed\n", ci->ci_name);*/
		return (EIO);
	}

	return (0);
}

static int
zio_decompress_data(int cpfunc, void *src, uint64_t srcsize,
	void *dest, uint64_t destsize)
{
	zio_compress_info_t *ci;

	if (cpfunc >= ZIO_COMPRESS_FUNCTIONS) {
		printf("ZFS: unsupported compression algorithm %u\n", cpfunc);
		return (EIO);
	}

	ci = &zio_compress_table[cpfunc];
	if (!ci->ci_decompress) {
		printf("ZFS: unsupported compression algorithm %s\n",
		    ci->ci_name);
		return (EIO);
	}

	return (ci->ci_decompress(src, dest, srcsize, destsize, ci->ci_level));
}

static uint64_t
zap_hash(uint64_t salt, const char *name)
{
	const uint8_t *cp;
	uint8_t c;
	uint64_t crc = salt;

	ASSERT(crc != 0);
	ASSERT(zfs_crc64_table[128] == ZFS_CRC64_POLY);
	for (cp = (const uint8_t *)name; (c = *cp) != '\0'; cp++)
		crc = (crc >> 8) ^ zfs_crc64_table[(crc ^ c) & 0xFF];

	/*
	 * Only use 28 bits, since we need 4 bits in the cookie for the
	 * collision differentiator.  We MUST use the high bits, since
	 * those are the onces that we first pay attention to when
	 * chosing the bucket.
	 */
	crc &= ~((1ULL << (64 - ZAP_HASHBITS)) - 1);

	return (crc);
}

static void *zfs_alloc(size_t size);
static void zfs_free(void *ptr, size_t size);

typedef struct raidz_col {
	uint64_t rc_devidx;		/* child device index for I/O */
	uint64_t rc_offset;		/* device offset */
	uint64_t rc_size;		/* I/O size */
	void *rc_data;			/* I/O data */
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
	    ((mask) & 0x1d1d1d1d1d1d1d1dULL); \
}

#define	VDEV_RAIDZ_64MUL_4(x, mask) \
{ \
	VDEV_RAIDZ_64MUL_2((x), mask); \
	VDEV_RAIDZ_64MUL_2((x), mask); \
}

/*
 * These two tables represent powers and logs of 2 in the Galois field defined
 * above. These values were computed by repeatedly multiplying by 2 as above.
 */
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

/*
 * Multiply a given number by 2 raised to the given power.
 */
static uint8_t
vdev_raidz_exp2(uint8_t a, int exp)
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
vdev_raidz_generate_parity_p(raidz_map_t *rm)
{
	uint64_t *p, *src, pcount, ccount, i;
	int c;

	pcount = rm->rm_col[VDEV_RAIDZ_P].rc_size / sizeof (src[0]);

	for (c = rm->rm_firstdatacol; c < rm->rm_cols; c++) {
		src = rm->rm_col[c].rc_data;
		p = rm->rm_col[VDEV_RAIDZ_P].rc_data;
		ccount = rm->rm_col[c].rc_size / sizeof (src[0]);

		if (c == rm->rm_firstdatacol) {
			ASSERT(ccount == pcount);
			for (i = 0; i < ccount; i++, src++, p++) {
				*p = *src;
			}
		} else {
			ASSERT(ccount <= pcount);
			for (i = 0; i < ccount; i++, src++, p++) {
				*p ^= *src;
			}
		}
	}
}

static void
vdev_raidz_generate_parity_pq(raidz_map_t *rm)
{
	uint64_t *p, *q, *src, pcnt, ccnt, mask, i;
	int c;

	pcnt = rm->rm_col[VDEV_RAIDZ_P].rc_size / sizeof (src[0]);
	ASSERT(rm->rm_col[VDEV_RAIDZ_P].rc_size ==
	    rm->rm_col[VDEV_RAIDZ_Q].rc_size);

	for (c = rm->rm_firstdatacol; c < rm->rm_cols; c++) {
		src = rm->rm_col[c].rc_data;
		p = rm->rm_col[VDEV_RAIDZ_P].rc_data;
		q = rm->rm_col[VDEV_RAIDZ_Q].rc_data;

		ccnt = rm->rm_col[c].rc_size / sizeof (src[0]);

		if (c == rm->rm_firstdatacol) {
			ASSERT(ccnt == pcnt || ccnt == 0);
			for (i = 0; i < ccnt; i++, src++, p++, q++) {
				*p = *src;
				*q = *src;
			}
			for (; i < pcnt; i++, src++, p++, q++) {
				*p = 0;
				*q = 0;
			}
		} else {
			ASSERT(ccnt <= pcnt);

			/*
			 * Apply the algorithm described above by multiplying
			 * the previous result and adding in the new value.
			 */
			for (i = 0; i < ccnt; i++, src++, p++, q++) {
				*p ^= *src;

				VDEV_RAIDZ_64MUL_2(*q, mask);
				*q ^= *src;
			}

			/*
			 * Treat short columns as though they are full of 0s.
			 * Note that there's therefore nothing needed for P.
			 */
			for (; i < pcnt; i++, q++) {
				VDEV_RAIDZ_64MUL_2(*q, mask);
			}
		}
	}
}

static void
vdev_raidz_generate_parity_pqr(raidz_map_t *rm)
{
	uint64_t *p, *q, *r, *src, pcnt, ccnt, mask, i;
	int c;

	pcnt = rm->rm_col[VDEV_RAIDZ_P].rc_size / sizeof (src[0]);
	ASSERT(rm->rm_col[VDEV_RAIDZ_P].rc_size ==
	    rm->rm_col[VDEV_RAIDZ_Q].rc_size);
	ASSERT(rm->rm_col[VDEV_RAIDZ_P].rc_size ==
	    rm->rm_col[VDEV_RAIDZ_R].rc_size);

	for (c = rm->rm_firstdatacol; c < rm->rm_cols; c++) {
		src = rm->rm_col[c].rc_data;
		p = rm->rm_col[VDEV_RAIDZ_P].rc_data;
		q = rm->rm_col[VDEV_RAIDZ_Q].rc_data;
		r = rm->rm_col[VDEV_RAIDZ_R].rc_data;

		ccnt = rm->rm_col[c].rc_size / sizeof (src[0]);

		if (c == rm->rm_firstdatacol) {
			ASSERT(ccnt == pcnt || ccnt == 0);
			for (i = 0; i < ccnt; i++, src++, p++, q++, r++) {
				*p = *src;
				*q = *src;
				*r = *src;
			}
			for (; i < pcnt; i++, src++, p++, q++, r++) {
				*p = 0;
				*q = 0;
				*r = 0;
			}
		} else {
			ASSERT(ccnt <= pcnt);

			/*
			 * Apply the algorithm described above by multiplying
			 * the previous result and adding in the new value.
			 */
			for (i = 0; i < ccnt; i++, src++, p++, q++, r++) {
				*p ^= *src;

				VDEV_RAIDZ_64MUL_2(*q, mask);
				*q ^= *src;

				VDEV_RAIDZ_64MUL_4(*r, mask);
				*r ^= *src;
			}

			/*
			 * Treat short columns as though they are full of 0s.
			 * Note that there's therefore nothing needed for P.
			 */
			for (; i < pcnt; i++, q++, r++) {
				VDEV_RAIDZ_64MUL_2(*q, mask);
				VDEV_RAIDZ_64MUL_4(*r, mask);
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
		panic("invalid RAID-Z configuration");
	}
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
 *           | 128  64  32  16  8   4   2   1  |
 *           |  19 205 116  29  64  16  4   1  |
 *           |  1   0   0   0   0   0   0   0  |
 *           |  0   1   0   0   0   0   0   0  |
 *  (V|I)' = |  0   0   1   0   0   0   0   0  |
 *           |  0   0   0   1   0   0   0   0  |
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
			ASSERT3U(rows[i][j], ==, 0);
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
				ASSERT3U(rows[i][j], ==, 0);
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
	uint8_t log, val;
	int ll;
	uint8_t *invlog[VDEV_RAIDZ_MAXPARITY];
	uint8_t *p, *pp;
	size_t psize;

	log = 0;	/* gcc */
	psize = sizeof (invlog[0][0]) * n * nmissing;
	p = zfs_alloc(psize);

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

		src = rm->rm_col[c].rc_data;
		ccount = rm->rm_col[c].rc_size;
		for (j = 0; j < nmissing; j++) {
			cc = missing[j] + rm->rm_firstdatacol;
			ASSERT3U(cc, >=, rm->rm_firstdatacol);
			ASSERT3U(cc, <, rm->rm_cols);
			ASSERT3U(cc, !=, c);

			dst[j] = rm->rm_col[cc].rc_data;
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

	zfs_free(p, psize);
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

	int code = 0;


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

	return (code);
}

static int
vdev_raidz_reconstruct(raidz_map_t *rm, int *t, int nt)
{
	int tgts[VDEV_RAIDZ_MAXPARITY];
	int ntgts;
	int i, c;
	int code;
	int nbadparity, nbaddata;

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
		if (i < nt && c == t[i]) {
			tgts[ntgts++] = c;
			i++;
		} else if (rm->rm_col[c].rc_error != 0) {
			tgts[ntgts++] = c;
		} else if (c >= rm->rm_firstdatacol) {
			nbaddata--;
		} else {
			nbadparity--;
		}
	}

	ASSERT(ntgts >= nt);
	ASSERT(nbaddata >= 0);
	ASSERT(nbaddata + nbadparity == ntgts);

	code = vdev_raidz_reconstruct_general(rm, tgts, ntgts);
	ASSERT(code < (1 << VDEV_RAIDZ_MAXPARITY));
	ASSERT(code > 0);
	return (code);
}

static raidz_map_t *
vdev_raidz_map_alloc(void *data, off_t offset, size_t size, uint64_t unit_shift,
    uint64_t dcols, uint64_t nparity)
{
	raidz_map_t *rm;
	uint64_t b = offset >> unit_shift;
	uint64_t s = size >> unit_shift;
	uint64_t f = b % dcols;
	uint64_t o = (b / dcols) << unit_shift;
	uint64_t q, r, c, bc, col, acols, scols, coff, devidx, asize, tot;

	q = s / (dcols - nparity);
	r = s - q * (dcols - nparity);
	bc = (r == 0 ? 0 : r + nparity);
	tot = s + nparity * (q + (r == 0 ? 0 : 1));

	if (q == 0) {
		acols = bc;
		scols = MIN(dcols, roundup(bc, nparity + 1));
	} else {
		acols = dcols;
		scols = dcols;
	}

	ASSERT3U(acols, <=, scols);

	rm = zfs_alloc(offsetof(raidz_map_t, rm_col[scols]));

	rm->rm_cols = acols;
	rm->rm_scols = scols;
	rm->rm_bigcols = bc;
	rm->rm_skipstart = bc;
	rm->rm_missingdata = 0;
	rm->rm_missingparity = 0;
	rm->rm_firstdatacol = nparity;
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
		rm->rm_col[c].rc_data = NULL;
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

	for (c = 0; c < rm->rm_firstdatacol; c++)
		rm->rm_col[c].rc_data = zfs_alloc(rm->rm_col[c].rc_size);

	rm->rm_col[c].rc_data = data;

	for (c = c + 1; c < acols; c++)
		rm->rm_col[c].rc_data = (char *)rm->rm_col[c - 1].rc_data +
		    rm->rm_col[c - 1].rc_size;

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

static void
vdev_raidz_map_free(raidz_map_t *rm)
{
	int c;

	for (c = rm->rm_firstdatacol - 1; c >= 0; c--)
		zfs_free(rm->rm_col[c].rc_data, rm->rm_col[c].rc_size);

	zfs_free(rm, offsetof(raidz_map_t, rm_col[rm->rm_scols]));
}

static vdev_t *
vdev_child(vdev_t *pvd, uint64_t devidx)
{
	vdev_t *cvd;

	STAILQ_FOREACH(cvd, &pvd->v_children, v_childlink) {
		if (cvd->v_id == devidx)
			break;
	}

	return (cvd);
}

/*
 * We keep track of whether or not there were any injected errors, so that
 * any ereports we generate can note it.
 */
static int
raidz_checksum_verify(const spa_t *spa, const blkptr_t *bp, void *data,
    uint64_t size)
{
	return (zio_checksum_verify(spa, bp, data));
}

/*
 * Generate the parity from the data columns. If we tried and were able to
 * read the parity without error, verify that the generated parity matches the
 * data we read. If it doesn't, we fire off a checksum error. Return the
 * number such failures.
 */
static int
raidz_parity_verify(raidz_map_t *rm)
{
	void *orig[VDEV_RAIDZ_MAXPARITY];
	int c, ret = 0;
	raidz_col_t *rc;

	for (c = 0; c < rm->rm_firstdatacol; c++) {
		rc = &rm->rm_col[c];
		if (!rc->rc_tried || rc->rc_error != 0)
			continue;
		orig[c] = zfs_alloc(rc->rc_size);
		bcopy(rc->rc_data, orig[c], rc->rc_size);
	}

	vdev_raidz_generate_parity(rm);

	for (c = rm->rm_firstdatacol - 1; c >= 0; c--) {
		rc = &rm->rm_col[c];
		if (!rc->rc_tried || rc->rc_error != 0)
			continue;
		if (bcmp(orig[c], rc->rc_data, rc->rc_size) != 0) {
			rc->rc_error = ECKSUM;
			ret++;
		}
		zfs_free(orig[c], rc->rc_size);
	}

	return (ret);
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
vdev_raidz_combrec(const spa_t *spa, raidz_map_t *rm, const blkptr_t *bp,
    void *data, off_t offset, uint64_t bytes, int total_errors, int data_errors)
{
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

		orig[n - 1] = zfs_alloc(rm->rm_col[0].rc_size);

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
				bcopy(rc->rc_data, orig[i], rc->rc_size);
			}

			/*
			 * Attempt a reconstruction and exit the outer loop on
			 * success.
			 */
			code = vdev_raidz_reconstruct(rm, tgts, n);
			if (raidz_checksum_verify(spa, bp, data, bytes) == 0) {
				for (i = 0; i < n; i++) {
					c = tgts[i];
					rc = &rm->rm_col[c];
					ASSERT(rc->rc_error == 0);
					rc->rc_error = ECKSUM;
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
				bcopy(orig[i], rc->rc_data, rc->rc_size);
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
	for (i = n - 1; i >= 0; i--) {
		zfs_free(orig[i], rm->rm_col[0].rc_size);
	}

	return (ret);
}

static int
vdev_raidz_read(vdev_t *vd, const blkptr_t *bp, void *data,
    off_t offset, size_t bytes)
{
	vdev_t *tvd = vd->v_top;
	vdev_t *cvd;
	raidz_map_t *rm;
	raidz_col_t *rc;
	int c, error;
	int unexpected_errors;
	int parity_errors;
	int parity_untried;
	int data_errors;
	int total_errors;
	int n;
	int tgts[VDEV_RAIDZ_MAXPARITY];
	int code;

	rc = NULL;	/* gcc */
	error = 0;

	rm = vdev_raidz_map_alloc(data, offset, bytes, tvd->v_ashift,
	    vd->v_nchildren, vd->v_nparity);

	/*
	 * Iterate over the columns in reverse order so that we hit the parity
	 * last -- any errors along the way will force us to read the parity.
	 */
	for (c = rm->rm_cols - 1; c >= 0; c--) {
		rc = &rm->rm_col[c];
		cvd = vdev_child(vd, rc->rc_devidx);
		if (cvd == NULL || cvd->v_state != VDEV_STATE_HEALTHY) {
			if (c >= rm->rm_firstdatacol)
				rm->rm_missingdata++;
			else
				rm->rm_missingparity++;
			rc->rc_error = ENXIO;
			rc->rc_tried = 1;	/* don't even try */
			rc->rc_skipped = 1;
			continue;
		}
#if 0		/* XXX: Too hard for the boot code. */
		if (vdev_dtl_contains(cvd, DTL_MISSING, zio->io_txg, 1)) {
			if (c >= rm->rm_firstdatacol)
				rm->rm_missingdata++;
			else
				rm->rm_missingparity++;
			rc->rc_error = ESTALE;
			rc->rc_skipped = 1;
			continue;
		}
#endif
		if (c >= rm->rm_firstdatacol || rm->rm_missingdata > 0) {
			rc->rc_error = cvd->v_read(cvd, NULL, rc->rc_data,
			    rc->rc_offset, rc->rc_size);
			rc->rc_tried = 1;
			rc->rc_skipped = 0;
		}
	}

reconstruct:
	unexpected_errors = 0;
	parity_errors = 0;
	parity_untried = 0;
	data_errors = 0;
	total_errors = 0;

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
			if (raidz_checksum_verify(vd->spa, bp, data, bytes) == 0) {
				/*
				 * If we read parity information (unnecessarily
				 * as it happens since no reconstruction was
				 * needed) regenerate and verify the parity.
				 * We also regenerate parity when resilvering
				 * so we can write it out to the failed device
				 * later.
				 */
				if (parity_errors + parity_untried <
				    rm->rm_firstdatacol) {
					n = raidz_parity_verify(rm);
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

			if (raidz_checksum_verify(vd->spa, bp, data, bytes) == 0) {
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
				if (parity_errors < rm->rm_firstdatacol - n) {
					n = raidz_parity_verify(rm);
					unexpected_errors += n;
					ASSERT(parity_errors + n <=
					    rm->rm_firstdatacol);
				}

				goto done;
			}
		}
	}

	/*
	 * This isn't a typical situation -- either we got a read
	 * error or a child silently returned bad data. Read every
	 * block so we can try again with as much data and parity as
	 * we can track down. If we've already been through once
	 * before, all children will be marked as tried so we'll
	 * proceed to combinatorial reconstruction.
	 */
	unexpected_errors = 1;
	rm->rm_missingdata = 0;
	rm->rm_missingparity = 0;

	n = 0;
	for (c = 0; c < rm->rm_cols; c++) {
		rc = &rm->rm_col[c];

		if (rc->rc_tried)
			continue;

		cvd = vdev_child(vd, rc->rc_devidx);
		ASSERT(cvd != NULL);
		rc->rc_error = cvd->v_read(cvd, NULL,
		    rc->rc_data, rc->rc_offset, rc->rc_size);
		if (rc->rc_error == 0)
			n++;
		rc->rc_tried = 1;
		rc->rc_skipped = 0;
	}
	/*
	 * If we managed to read anything more, retry the
	 * reconstruction.
	 */
	if (n > 0)
		goto reconstruct;

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
		error = EIO;
	} else if (total_errors < rm->rm_firstdatacol &&
	    (code = vdev_raidz_combrec(vd->spa, rm, bp, data, offset, bytes,
	     total_errors, data_errors)) != 0) {
		/*
		 * If we didn't use all the available parity for the
		 * combinatorial reconstruction, verify that the remaining
		 * parity is correct.
		 */
		if (code != (1 << rm->rm_firstdatacol) - 1)
			(void) raidz_parity_verify(rm);
	} else {
		/*
		 * We're here because either:
		 *
		 *	total_errors == rm_first_datacol, or
		 *	vdev_raidz_combrec() failed
		 *
		 * In either case, there is enough bad data to prevent
		 * reconstruction.
		 *
		 * Start checksum ereports for all children which haven't
		 * failed, and the IO wasn't speculative.
		 */
		error = ECKSUM;
	}

done:
	vdev_raidz_map_free(rm);

	return (error);
}
