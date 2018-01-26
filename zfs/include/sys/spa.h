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
 * Copyright (c) 2011, 2014 by Delphix. All rights reserved.
 * Copyright 2011 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2014 Spectra Logic Corporation, All rights reserved.
 * Copyright 2013 Saso Kiselkov. All rights reserved.
 * Copyright (c) 2014 Integros [integros.com]
 * Copyright 2017 Joyent, Inc.
 * Copyright (c) 2017 Datto Inc.
 */

#ifndef _SYS_SPA_H
#define	_SYS_SPA_H

#include <sys/avl.h>
#include <sys/zfs_context.h>
#include <sys/kstat.h>
#include <sys/nvpair.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/fs/zfs.h>
#include <sys/spa_checksum.h>
#include <sys/dmu.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Forward references that lots of things need.
 */
typedef struct spa spa_t;
typedef struct vdev vdev_t;
typedef struct metaslab metaslab_t;
typedef struct metaslab_group metaslab_group_t;
typedef struct metaslab_class metaslab_class_t;
typedef struct zio zio_t;
typedef struct zilog zilog_t;
typedef struct spa_aux_vdev spa_aux_vdev_t;
typedef struct ddt ddt_t;
typedef struct ddt_entry ddt_entry_t;
typedef struct zbookmark_phys zbookmark_phys_t;

struct dsl_pool;
struct dsl_dataset;

/*
 * General-purpose 32-bit and 64-bit bitfield encodings.
 */
#define	BF32_DECODE(x, low, len)	P2PHASE((x) >> (low), 1U << (len))
#define	BF64_DECODE(x, low, len)	P2PHASE((x) >> (low), 1ULL << (len))
#define	BF32_ENCODE(x, low, len)	(P2PHASE((x), 1U << (len)) << (low))
#define	BF64_ENCODE(x, low, len)	(P2PHASE((x), 1ULL << (len)) << (low))

#define	BF32_GET(x, low, len)		BF32_DECODE(x, low, len)
#define	BF64_GET(x, low, len)		BF64_DECODE(x, low, len)

#define	BF32_SET(x, low, len, val) do { \
	ASSERT3U(val, <, 1U << (len)); \
	ASSERT3U(low + len, <=, 32); \
	(x) ^= BF32_ENCODE((x >> low) ^ (val), low, len); \
_NOTE(CONSTCOND) } while (0)

#define	BF64_SET(x, low, len, val) do { \
	ASSERT3U(val, <, 1ULL << (len)); \
	ASSERT3U(low + len, <=, 64); \
	((x) ^= BF64_ENCODE((x >> low) ^ (val), low, len)); \
_NOTE(CONSTCOND) } while (0)

#define	BF32_GET_SB(x, low, len, shift, bias)	\
	((BF32_GET(x, low, len) + (bias)) << (shift))
#define	BF64_GET_SB(x, low, len, shift, bias)	\
	((BF64_GET(x, low, len) + (bias)) << (shift))

#define	BF32_SET_SB(x, low, len, shift, bias, val) do { \
	ASSERT(IS_P2ALIGNED(val, 1U << shift)); \
	ASSERT3S((val) >> (shift), >=, bias); \
	BF32_SET(x, low, len, ((val) >> (shift)) - (bias)); \
_NOTE(CONSTCOND) } while (0)
#define	BF64_SET_SB(x, low, len, shift, bias, val) do { \
	ASSERT(IS_P2ALIGNED(val, 1ULL << shift)); \
	ASSERT3S((val) >> (shift), >=, bias); \
	BF64_SET(x, low, len, ((val) >> (shift)) - (bias)); \
_NOTE(CONSTCOND) } while (0)

/*
 * We currently support block sizes from 512 bytes to 16MB.
 * The benefits of larger blocks, and thus larger IO, need to be weighed
 * against the cost of COWing a giant block to modify one byte, and the
 * large latency of reading or writing a large block.
 *
 * Note that although blocks up to 16MB are supported, the recordsize
 * property can not be set larger than zfs_max_recordsize (default 1MB).
 * See the comment near zfs_max_recordsize in dsl_dataset.c for details.
 *
 * Note that although the LSIZE field of the blkptr_t can store sizes up
 * to 32MB, the dnode's dn_datablkszsec can only store sizes up to
 * 32MB - 512 bytes.  Therefore, we limit SPA_MAXBLOCKSIZE to 16MB.
 */
#define	SPA_MINBLOCKSHIFT	9
#define	SPA_OLD_MAXBLOCKSHIFT	17
#define	SPA_MAXBLOCKSHIFT	24
#define	SPA_MINBLOCKSIZE	(1ULL << SPA_MINBLOCKSHIFT)
#define	SPA_OLD_MAXBLOCKSIZE	(1ULL << SPA_OLD_MAXBLOCKSHIFT)
#define	SPA_MAXBLOCKSIZE	(1ULL << SPA_MAXBLOCKSHIFT)

/*
 * Alignment Shift (ashift) is an immutable, internal top-level vdev property
 * which can only be set at vdev creation time. Physical writes are always done
 * according to it, which makes 2^ashift the smallest possible IO on a vdev.
 *
 * We currently allow values ranging from 512 bytes (2^9 = 512) to 64 KiB
 * (2^16 = 65,536).
 */
#define	ASHIFT_MIN		9
#define	ASHIFT_MAX		16

/*
 * Size of block to hold the configuration data (a packed nvlist)
 */
#define	SPA_CONFIG_BLOCKSIZE	(1ULL << 14)

/*
 * The DVA size encodings for LSIZE and PSIZE support blocks up to 32MB.
 * The ASIZE encoding should be at least 64 times larger (6 more bits)
 * to support up to 4-way RAID-Z mirror mode with worst-case gang block
 * overhead, three DVAs per bp, plus one more bit in case we do anything
 * else that expands the ASIZE.
 */
#define	SPA_LSIZEBITS		16	/* LSIZE up to 32M (2^16 * 512)	*/
#define	SPA_PSIZEBITS		16	/* PSIZE up to 32M (2^16 * 512)	*/
#define	SPA_ASIZEBITS		24	/* ASIZE up to 64 times larger	*/

#define	SPA_COMPRESSBITS	7

/*
 * All SPA data is represented by 128-bit data virtual addresses (DVAs).
 * The members of the dva_t should be considered opaque outside the SPA.
 */
typedef struct dva {
	uint64_t	dva_word[2];
} dva_t;


/*
 * Some checksums/hashes need a 256-bit initialization salt. This salt is kept
 * secret and is suitable for use in MAC algorithms as the key.
 */
typedef struct zio_cksum_salt {
	uint8_t		zcs_bytes[32];
} zio_cksum_salt_t;

/*
 * Each block is described by its DVAs, time of birth, checksum, etc.
 * The word-by-word, bit-by-bit layout of the blkptr is as follows:
 *
 *	64	56	48	40	32	24	16	8	0
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * 0	|		vdev1		| GRID  |	  ASIZE		|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * 1	|G|			 offset1				|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * 2	|		vdev2		| GRID  |	  ASIZE		|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * 3	|G|			 offset2				|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * 4	|		vdev3		| GRID  |	  ASIZE		|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * 5	|G|			 offset3				|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * 6	|BDX|lvl| type	| cksum |E| comp|    PSIZE	|     LSIZE	|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * 7	|			padding					|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * 8	|			padding					|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * 9	|			physical birth txg			|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * a	|			logical birth txg			|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * b	|			fill count				|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * c	|			checksum[0]				|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * d	|			checksum[1]				|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * e	|			checksum[2]				|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * f	|			checksum[3]				|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 *
 * Legend:
 *
 * vdev		virtual device ID
 * offset	offset into virtual device
 * LSIZE	logical size
 * PSIZE	physical size (after compression)
 * ASIZE	allocated size (including RAID-Z parity and gang block headers)
 * GRID		RAID-Z layout information (reserved for future use)
 * cksum	checksum function
 * comp		compression function
 * G		gang block indicator
 * B		byteorder (endianness)
 * D		dedup
 * X		encryption (on version 30, which is not supported)
 * E		blkptr_t contains embedded data (see below)
 * lvl		level of indirection
 * type		DMU object type
 * phys birth	txg of block allocation; zero if same as logical birth txg
 * log. birth	transaction group in which the block was logically born
 * fill count	number of non-zero blocks under this bp
 * checksum[4]	256-bit checksum of the data this bp describes
 */

/*
 * "Embedded" blkptr_t's don't actually point to a block, instead they
 * have a data payload embedded in the blkptr_t itself.  See the comment
 * in blkptr.c for more details.
 *
 * The blkptr_t is laid out as follows:
 *
 *	64	56	48	40	32	24	16	8	0
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * 0	|      payload                                                  |
 * 1	|      payload                                                  |
 * 2	|      payload                                                  |
 * 3	|      payload                                                  |
 * 4	|      payload                                                  |
 * 5	|      payload                                                  |
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * 6	|BDX|lvl| type	| etype |E| comp| PSIZE|              LSIZE	|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * 7	|      payload                                                  |
 * 8	|      payload                                                  |
 * 9	|      payload                                                  |
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * a	|			logical birth txg			|
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 * b	|      payload                                                  |
 * c	|      payload                                                  |
 * d	|      payload                                                  |
 * e	|      payload                                                  |
 * f	|      payload                                                  |
 *	+-------+-------+-------+-------+-------+-------+-------+-------+
 *
 * Legend:
 *
 * payload		contains the embedded data
 * B (byteorder)	byteorder (endianness)
 * D (dedup)		padding (set to zero)
 * X			encryption (set to zero; see above)
 * E (embedded)		set to one
 * lvl			indirection level
 * type			DMU object type
 * etype		how to interpret embedded data (BP_EMBEDDED_TYPE_*)
 * comp			compression function of payload
 * PSIZE		size of payload after compression, in bytes
 * LSIZE		logical size of payload, in bytes
 *			note that 25 bits is enough to store the largest
 *			"normal" BP's LSIZE (2^16 * 2^9) in bytes
 * log. birth		transaction group in which the block was logically born
 *
 * Note that LSIZE and PSIZE are stored in bytes, whereas for non-embedded
 * bp's they are stored in units of SPA_MINBLOCKSHIFT.
 * Generally, the generic BP_GET_*() macros can be used on embedded BP's.
 * The B, D, X, lvl, type, and comp fields are stored the same as with normal
 * BP's so the BP_SET_* macros can be used with them.  etype, PSIZE, LSIZE must
 * be set with the BPE_SET_* macros.  BP_SET_EMBEDDED() should be called before
 * other macros, as they assert that they are only used on BP's of the correct
 * "embedded-ness".
 */

#define	BPE_GET_ETYPE(bp)	\
	(ASSERT(BP_IS_EMBEDDED(bp)), \
	BF64_GET((bp)->blk_prop, 40, 8))
#define	BPE_SET_ETYPE(bp, t)	do { \
	ASSERT(BP_IS_EMBEDDED(bp)); \
	BF64_SET((bp)->blk_prop, 40, 8, t); \
_NOTE(CONSTCOND) } while (0)

#define	BPE_GET_LSIZE(bp)	\
	(ASSERT(BP_IS_EMBEDDED(bp)), \
	BF64_GET_SB((bp)->blk_prop, 0, 25, 0, 1))
#define	BPE_SET_LSIZE(bp, x)	do { \
	ASSERT(BP_IS_EMBEDDED(bp)); \
	BF64_SET_SB((bp)->blk_prop, 0, 25, 0, 1, x); \
_NOTE(CONSTCOND) } while (0)

#define	BPE_GET_PSIZE(bp)	\
	(ASSERT(BP_IS_EMBEDDED(bp)), \
	BF64_GET_SB((bp)->blk_prop, 25, 7, 0, 1))
#define	BPE_SET_PSIZE(bp, x)	do { \
	ASSERT(BP_IS_EMBEDDED(bp)); \
	BF64_SET_SB((bp)->blk_prop, 25, 7, 0, 1, x); \
_NOTE(CONSTCOND) } while (0)

typedef enum bp_embedded_type {
	BP_EMBEDDED_TYPE_DATA,
	BP_EMBEDDED_TYPE_RESERVED, /* Reserved for an unintegrated feature. */
	NUM_BP_EMBEDDED_TYPES = BP_EMBEDDED_TYPE_RESERVED
} bp_embedded_type_t;

#define	BPE_NUM_WORDS 14
#define	BPE_PAYLOAD_SIZE (BPE_NUM_WORDS * sizeof (uint64_t))
#define	BPE_IS_PAYLOADWORD(bp, wp) \
	((wp) != &(bp)->blk_prop && (wp) != &(bp)->blk_birth)

#define	SPA_BLKPTRSHIFT	7		/* blkptr_t is 128 bytes	*/
#define	SPA_DVAS_PER_BP	3		/* Number of DVAs in a bp	*/

/*
 * A block is a hole when it has either 1) never been written to, or
 * 2) is zero-filled. In both cases, ZFS can return all zeroes for all reads
 * without physically allocating disk space. Holes are represented in the
 * blkptr_t structure by zeroed blk_dva. Correct checking for holes is
 * done through the BP_IS_HOLE macro. For holes, the logical size, level,
 * DMU object type, and birth times are all also stored for holes that
 * were written to at some point (i.e. were punched after having been filled).
 */
typedef struct blkptr {
	dva_t		blk_dva[SPA_DVAS_PER_BP]; /* Data Virtual Addresses */
	uint64_t	blk_prop;	/* size, compression, type, etc	    */
	uint64_t	blk_pad[2];	/* Extra space for the future	    */
	uint64_t	blk_phys_birth;	/* txg when block was allocated	    */
	uint64_t	blk_birth;	/* transaction group at birth	    */
	uint64_t	blk_fill;	/* fill count			    */
	zio_cksum_t	blk_cksum;	/* 256-bit checksum		    */
} blkptr_t;

/*
 * Macros to get and set fields in a bp or DVA.
 */
#define	DVA_GET_ASIZE(dva)	\
	BF64_GET_SB((dva)->dva_word[0], 0, SPA_ASIZEBITS, SPA_MINBLOCKSHIFT, 0)
#define	DVA_SET_ASIZE(dva, x)	\
	BF64_SET_SB((dva)->dva_word[0], 0, SPA_ASIZEBITS, \
	SPA_MINBLOCKSHIFT, 0, x)

#define	DVA_GET_GRID(dva)	BF64_GET((dva)->dva_word[0], 24, 8)
#define	DVA_SET_GRID(dva, x)	BF64_SET((dva)->dva_word[0], 24, 8, x)

#define	DVA_GET_VDEV(dva)	BF64_GET((dva)->dva_word[0], 32, 32)
#define	DVA_SET_VDEV(dva, x)	BF64_SET((dva)->dva_word[0], 32, 32, x)

#define	DVA_GET_OFFSET(dva)	\
	BF64_GET_SB((dva)->dva_word[1], 0, 63, SPA_MINBLOCKSHIFT, 0)
#define	DVA_SET_OFFSET(dva, x)	\
	BF64_SET_SB((dva)->dva_word[1], 0, 63, SPA_MINBLOCKSHIFT, 0, x)

#define	DVA_GET_GANG(dva)	BF64_GET((dva)->dva_word[1], 63, 1)
#define	DVA_SET_GANG(dva, x)	BF64_SET((dva)->dva_word[1], 63, 1, x)

#define	BP_GET_LSIZE(bp)	\
	(BP_IS_EMBEDDED(bp) ?	\
	(BPE_GET_ETYPE(bp) == BP_EMBEDDED_TYPE_DATA ? BPE_GET_LSIZE(bp) : 0): \
	BF64_GET_SB((bp)->blk_prop, 0, SPA_LSIZEBITS, SPA_MINBLOCKSHIFT, 1))
#define	BP_SET_LSIZE(bp, x)	do { \
	ASSERT(!BP_IS_EMBEDDED(bp)); \
	BF64_SET_SB((bp)->blk_prop, \
	    0, SPA_LSIZEBITS, SPA_MINBLOCKSHIFT, 1, x); \
_NOTE(CONSTCOND) } while (0)

#define	BP_GET_PSIZE(bp)	\
	(BP_IS_EMBEDDED(bp) ? 0 : \
	BF64_GET_SB((bp)->blk_prop, 16, SPA_PSIZEBITS, SPA_MINBLOCKSHIFT, 1))
#define	BP_SET_PSIZE(bp, x)	do { \
	ASSERT(!BP_IS_EMBEDDED(bp)); \
	BF64_SET_SB((bp)->blk_prop, \
	    16, SPA_PSIZEBITS, SPA_MINBLOCKSHIFT, 1, x); \
_NOTE(CONSTCOND) } while (0)

#define	BP_GET_COMPRESS(bp)		\
	BF64_GET((bp)->blk_prop, 32, SPA_COMPRESSBITS)
#define	BP_SET_COMPRESS(bp, x)		\
	BF64_SET((bp)->blk_prop, 32, SPA_COMPRESSBITS, x)

#define	BP_IS_EMBEDDED(bp)		BF64_GET((bp)->blk_prop, 39, 1)
#define	BP_SET_EMBEDDED(bp, x)		BF64_SET((bp)->blk_prop, 39, 1, x)

#define	BP_GET_CHECKSUM(bp)		\
	(BP_IS_EMBEDDED(bp) ? ZIO_CHECKSUM_OFF : \
	BF64_GET((bp)->blk_prop, 40, 8))
#define	BP_SET_CHECKSUM(bp, x)		do { \
	ASSERT(!BP_IS_EMBEDDED(bp)); \
	BF64_SET((bp)->blk_prop, 40, 8, x); \
_NOTE(CONSTCOND) } while (0)

#define	BP_GET_TYPE(bp)			BF64_GET((bp)->blk_prop, 48, 8)
#define	BP_SET_TYPE(bp, x)		BF64_SET((bp)->blk_prop, 48, 8, x)

#define	BP_GET_LEVEL(bp)		BF64_GET((bp)->blk_prop, 56, 5)
#define	BP_SET_LEVEL(bp, x)		BF64_SET((bp)->blk_prop, 56, 5, x)

#define	BP_GET_DEDUP(bp)		BF64_GET((bp)->blk_prop, 62, 1)
#define	BP_SET_DEDUP(bp, x)		BF64_SET((bp)->blk_prop, 62, 1, x)

#define	BP_GET_BYTEORDER(bp)		BF64_GET((bp)->blk_prop, 63, 1)
#define	BP_SET_BYTEORDER(bp, x)		BF64_SET((bp)->blk_prop, 63, 1, x)

#define	BP_PHYSICAL_BIRTH(bp)		\
	(BP_IS_EMBEDDED(bp) ? 0 : \
	(bp)->blk_phys_birth ? (bp)->blk_phys_birth : (bp)->blk_birth)

#define	BP_SET_BIRTH(bp, logical, physical)	\
{						\
	ASSERT(!BP_IS_EMBEDDED(bp));		\
	(bp)->blk_birth = (logical);		\
	(bp)->blk_phys_birth = ((logical) == (physical) ? 0 : (physical)); \
}

#define	BP_GET_FILL(bp) (BP_IS_EMBEDDED(bp) ? 1 : (bp)->blk_fill)

#define	BP_IS_METADATA(bp)	\
	(BP_GET_LEVEL(bp) > 0 || DMU_OT_IS_METADATA(BP_GET_TYPE(bp)))

#define	BP_GET_ASIZE(bp)	\
	(BP_IS_EMBEDDED(bp) ? 0 : \
	DVA_GET_ASIZE(&(bp)->blk_dva[0]) + \
	DVA_GET_ASIZE(&(bp)->blk_dva[1]) + \
	DVA_GET_ASIZE(&(bp)->blk_dva[2]))

#define	BP_GET_UCSIZE(bp)	\
	(BP_IS_METADATA(bp) ? BP_GET_PSIZE(bp) : BP_GET_LSIZE(bp))

#define	BP_GET_NDVAS(bp)	\
	(BP_IS_EMBEDDED(bp) ? 0 : \
	!!DVA_GET_ASIZE(&(bp)->blk_dva[0]) + \
	!!DVA_GET_ASIZE(&(bp)->blk_dva[1]) + \
	!!DVA_GET_ASIZE(&(bp)->blk_dva[2]))

#define	BP_COUNT_GANG(bp)	\
	(BP_IS_EMBEDDED(bp) ? 0 : \
	(DVA_GET_GANG(&(bp)->blk_dva[0]) + \
	DVA_GET_GANG(&(bp)->blk_dva[1]) + \
	DVA_GET_GANG(&(bp)->blk_dva[2])))

#define	DVA_EQUAL(dva1, dva2)	\
	((dva1)->dva_word[1] == (dva2)->dva_word[1] && \
	(dva1)->dva_word[0] == (dva2)->dva_word[0])

#define	BP_EQUAL(bp1, bp2)	\
	(BP_PHYSICAL_BIRTH(bp1) == BP_PHYSICAL_BIRTH(bp2) &&	\
	(bp1)->blk_birth == (bp2)->blk_birth &&			\
	DVA_EQUAL(&(bp1)->blk_dva[0], &(bp2)->blk_dva[0]) &&	\
	DVA_EQUAL(&(bp1)->blk_dva[1], &(bp2)->blk_dva[1]) &&	\
	DVA_EQUAL(&(bp1)->blk_dva[2], &(bp2)->blk_dva[2]))


#define	DVA_IS_VALID(dva)	(DVA_GET_ASIZE(dva) != 0)

#define	BP_IDENTITY(bp)		(ASSERT(!BP_IS_EMBEDDED(bp)), &(bp)->blk_dva[0])
#define	BP_IS_GANG(bp)		\
	(BP_IS_EMBEDDED(bp) ? B_FALSE : DVA_GET_GANG(BP_IDENTITY(bp)))
#define	DVA_IS_EMPTY(dva)	((dva)->dva_word[0] == 0ULL &&	\
				(dva)->dva_word[1] == 0ULL)
#define	BP_IS_HOLE(bp) \
	(!BP_IS_EMBEDDED(bp) && DVA_IS_EMPTY(BP_IDENTITY(bp)))

/* BP_IS_RAIDZ(bp) assumes no block compression */
#define	BP_IS_RAIDZ(bp)		(DVA_GET_ASIZE(&(bp)->blk_dva[0]) > \
				BP_GET_PSIZE(bp))

#define	BP_ZERO(bp)				\
{						\
	(bp)->blk_dva[0].dva_word[0] = 0;	\
	(bp)->blk_dva[0].dva_word[1] = 0;	\
	(bp)->blk_dva[1].dva_word[0] = 0;	\
	(bp)->blk_dva[1].dva_word[1] = 0;	\
	(bp)->blk_dva[2].dva_word[0] = 0;	\
	(bp)->blk_dva[2].dva_word[1] = 0;	\
	(bp)->blk_prop = 0;			\
	(bp)->blk_pad[0] = 0;			\
	(bp)->blk_pad[1] = 0;			\
	(bp)->blk_phys_birth = 0;		\
	(bp)->blk_birth = 0;			\
	(bp)->blk_fill = 0;			\
	ZIO_SET_CHECKSUM(&(bp)->blk_cksum, 0, 0, 0, 0);	\
}

#ifdef _BIG_ENDIAN
#define	ZFS_HOST_BYTEORDER	(0ULL)
#else
#define	ZFS_HOST_BYTEORDER	(1ULL)
#endif

#define	BP_SHOULD_BYTESWAP(bp)	(BP_GET_BYTEORDER(bp) != ZFS_HOST_BYTEORDER)

#define	BP_SPRINTF_LEN	320

/*
 * This macro allows code sharing between zfs, libzpool, and mdb.
 * 'func' is either snprintf() or mdb_snprintf().
 * 'ws' (whitespace) can be ' ' for single-line format, '\n' for multi-line.
 */
#define	SNPRINTF_BLKPTR(func, ws, buf, size, bp, type, checksum, compress) \
{									\
	static const char *copyname[] =					\
	    { "zero", "single", "double", "triple" };			\
	int len = 0;							\
	int copies = 0;							\
	int d;								\
									\
	if (bp == NULL) {						\
		len += func(buf + len, size - len, "<NULL>");		\
	} else if (BP_IS_HOLE(bp)) {					\
		len += func(buf + len, size - len,			\
		    "HOLE [L%llu %s] "					\
		    "size=%llxL birth=%lluL",				\
		    (u_longlong_t)BP_GET_LEVEL(bp),			\
		    type,						\
		    (u_longlong_t)BP_GET_LSIZE(bp),			\
		    (u_longlong_t)bp->blk_birth);			\
	} else if (BP_IS_EMBEDDED(bp)) {				\
		len = func(buf + len, size - len,			\
		    "EMBEDDED [L%llu %s] et=%u %s "			\
		    "size=%llxL/%llxP birth=%lluL",			\
		    (u_longlong_t)BP_GET_LEVEL(bp),			\
		    type,						\
		    (int)BPE_GET_ETYPE(bp),				\
		    compress,						\
		    (u_longlong_t)BPE_GET_LSIZE(bp),			\
		    (u_longlong_t)BPE_GET_PSIZE(bp),			\
		    (u_longlong_t)bp->blk_birth);			\
	} else {							\
		for (d = 0; d < BP_GET_NDVAS(bp); d++) {		\
			const dva_t *dva = &bp->blk_dva[d];		\
			if (DVA_IS_VALID(dva))				\
				copies++;				\
			len += func(buf + len, size - len,		\
			    "DVA[%d]=<%llu:%llx:%llx>%c", d,		\
			    (u_longlong_t)DVA_GET_VDEV(dva),		\
			    (u_longlong_t)DVA_GET_OFFSET(dva),		\
			    (u_longlong_t)DVA_GET_ASIZE(dva),		\
			    ws);					\
		}							\
		if (BP_IS_GANG(bp) &&					\
		    DVA_GET_ASIZE(&bp->blk_dva[2]) <=			\
		    DVA_GET_ASIZE(&bp->blk_dva[1]) / 2)			\
			copies--;					\
		len += func(buf + len, size - len,			\
		    "[L%llu %s] %s %s %s %s %s %s%c"			\
		    "size=%llxL/%llxP birth=%lluL/%lluP fill=%llu%c"	\
		    "cksum=%llx:%llx:%llx:%llx",			\
		    (u_longlong_t)BP_GET_LEVEL(bp),			\
		    type,						\
		    checksum,						\
		    compress,						\
		    BP_GET_BYTEORDER(bp) == 0 ? "BE" : "LE",		\
		    BP_IS_GANG(bp) ? "gang" : "contiguous",		\
		    BP_GET_DEDUP(bp) ? "dedup" : "unique",		\
		    copyname[copies],					\
		    ws,							\
		    (u_longlong_t)BP_GET_LSIZE(bp),			\
		    (u_longlong_t)BP_GET_PSIZE(bp),			\
		    (u_longlong_t)bp->blk_birth,			\
		    (u_longlong_t)BP_PHYSICAL_BIRTH(bp),		\
		    (u_longlong_t)BP_GET_FILL(bp),			\
		    ws,							\
		    (u_longlong_t)bp->blk_cksum.zc_word[0],		\
		    (u_longlong_t)bp->blk_cksum.zc_word[1],		\
		    (u_longlong_t)bp->blk_cksum.zc_word[2],		\
		    (u_longlong_t)bp->blk_cksum.zc_word[3]);		\
	}								\
	ASSERT(len < size);						\
}

#define	BP_GET_BUFC_TYPE(bp)						\
	(BP_IS_METADATA(bp) ? ARC_BUFC_METADATA : ARC_BUFC_DATA)

typedef enum spa_import_type {
	SPA_IMPORT_EXISTING,
	SPA_IMPORT_ASSEMBLE
} spa_import_type_t;

/* state manipulation functions */
extern int spa_open(const char *pool, spa_t **, void *tag);
extern int spa_open_rewind(const char *pool, spa_t **, void *tag,
    nvlist_t *policy, nvlist_t **config);
extern int spa_get_stats(const char *pool, nvlist_t **config, char *altroot,
    size_t buflen);
extern int spa_create(const char *pool, nvlist_t *config, nvlist_t *props,
    nvlist_t *zplprops);
extern int spa_import(char *pool, nvlist_t *config, nvlist_t *props,
    uint64_t flags);
extern nvlist_t *spa_tryimport(nvlist_t *tryconfig);
extern int spa_destroy(char *pool);
extern int spa_export(char *pool, nvlist_t **oldconfig, boolean_t force,
    boolean_t hardforce);
extern int spa_reset(char *pool);
extern void spa_async_request(spa_t *spa, int flag);
extern void spa_async_unrequest(spa_t *spa, int flag);
extern void spa_async_suspend(spa_t *spa);
extern void spa_async_resume(spa_t *spa);
extern spa_t *spa_inject_addref(char *pool);
extern void spa_inject_delref(spa_t *spa);
extern void spa_scan_stat_init(spa_t *spa);
extern int spa_scan_get_stats(spa_t *spa, pool_scan_stat_t *ps);

#define	SPA_ASYNC_CONFIG_UPDATE	0x01
#define	SPA_ASYNC_REMOVE	0x02
#define	SPA_ASYNC_PROBE		0x04
#define	SPA_ASYNC_RESILVER_DONE	0x08
#define	SPA_ASYNC_RESILVER	0x10
#define	SPA_ASYNC_AUTOEXPAND	0x20
#define	SPA_ASYNC_REMOVE_DONE	0x40
#define	SPA_ASYNC_REMOVE_STOP	0x80

/*
 * Controls the behavior of spa_vdev_remove().
 */
#define	SPA_REMOVE_UNSPARE	0x01
#define	SPA_REMOVE_DONE		0x02

/* device manipulation */
extern int spa_vdev_add(spa_t *spa, nvlist_t *nvroot);
extern int spa_vdev_attach(spa_t *spa, uint64_t guid, nvlist_t *nvroot,
    int replacing);
extern int spa_vdev_detach(spa_t *spa, uint64_t guid, uint64_t pguid,
    int replace_done);
extern int spa_vdev_remove(spa_t *spa, uint64_t guid, boolean_t unspare);
extern boolean_t spa_vdev_remove_active(spa_t *spa);
extern int spa_vdev_setpath(spa_t *spa, uint64_t guid, const char *newpath);
extern int spa_vdev_setfru(spa_t *spa, uint64_t guid, const char *newfru);
extern int spa_vdev_split_mirror(spa_t *spa, char *newname, nvlist_t *config,
    nvlist_t *props, boolean_t exp);

/* spare state (which is global across all pools) */
extern void spa_spare_add(vdev_t *vd);
extern void spa_spare_remove(vdev_t *vd);
extern boolean_t spa_spare_exists(uint64_t guid, uint64_t *pool, int *refcnt);
extern void spa_spare_activate(vdev_t *vd);

/* L2ARC state (which is global across all pools) */
extern void spa_l2cache_add(vdev_t *vd);
extern void spa_l2cache_remove(vdev_t *vd);
extern boolean_t spa_l2cache_exists(uint64_t guid, uint64_t *pool);
extern void spa_l2cache_activate(vdev_t *vd);
extern void spa_l2cache_drop(spa_t *spa);

/* scanning */
extern int spa_scan(spa_t *spa, pool_scan_func_t func);
extern int spa_scan_stop(spa_t *spa);
extern int spa_scrub_pause_resume(spa_t *spa, pool_scrub_cmd_t flag);

/* spa syncing */
extern void spa_sync(spa_t *spa, uint64_t txg); /* only for DMU use */
extern void spa_sync_allpools(void);

extern int zfs_sync_pass_deferred_free;

/* spa namespace global mutex */
extern kmutex_t spa_namespace_lock;

/*
 * SPA configuration functions in spa_config.c
 */

#define	SPA_CONFIG_UPDATE_POOL	0
#define	SPA_CONFIG_UPDATE_VDEVS	1

extern void spa_config_sync(spa_t *, boolean_t, boolean_t);
extern void spa_config_load(void);
extern nvlist_t *spa_all_configs(uint64_t *);
extern void spa_config_set(spa_t *spa, nvlist_t *config);
extern nvlist_t *spa_config_generate(spa_t *spa, vdev_t *vd, uint64_t txg,
    int getstats);
extern void spa_config_update(spa_t *spa, int what);

/*
 * Miscellaneous SPA routines in spa_misc.c
 */

/* Namespace manipulation */
extern spa_t *spa_lookup(const char *name);
extern spa_t *spa_add(const char *name, nvlist_t *config, const char *altroot);
extern void spa_remove(spa_t *spa);
extern spa_t *spa_next(spa_t *prev);

/* Refcount functions */
extern void spa_open_ref(spa_t *spa, void *tag);
extern void spa_close(spa_t *spa, void *tag);
extern void spa_async_close(spa_t *spa, void *tag);
extern boolean_t spa_refcount_zero(spa_t *spa);

#define	SCL_NONE	0x00
#define	SCL_CONFIG	0x01
#define	SCL_STATE	0x02
#define	SCL_L2ARC	0x04		/* hack until L2ARC 2.0 */
#define	SCL_ALLOC	0x08
#define	SCL_ZIO		0x10
#define	SCL_FREE	0x20
#define	SCL_VDEV	0x40
#define	SCL_LOCKS	7
#define	SCL_ALL		((1 << SCL_LOCKS) - 1)
#define	SCL_STATE_ALL	(SCL_STATE | SCL_L2ARC | SCL_ZIO)

/* Historical pool statistics */
typedef struct spa_stats_history {
	kmutex_t		lock;
	uint64_t		count;
	uint64_t		size;
	kstat_t			*kstat;
	void			*private;
	list_t			list;
} spa_stats_history_t;

typedef struct spa_stats {
	spa_stats_history_t	read_history;
	spa_stats_history_t	txg_history;
	spa_stats_history_t	tx_assign_histogram;
	spa_stats_history_t	io_history;
	spa_stats_history_t	mmp_history;
} spa_stats_t;

typedef enum txg_state {
	TXG_STATE_BIRTH		= 0,
	TXG_STATE_OPEN		= 1,
	TXG_STATE_QUIESCED	= 2,
	TXG_STATE_WAIT_FOR_SYNC	= 3,
	TXG_STATE_SYNCED	= 4,
	TXG_STATE_COMMITTED	= 5,
} txg_state_t;

typedef struct txg_stat {
	vdev_stat_t		vs1;
	vdev_stat_t		vs2;
	uint64_t		txg;
	uint64_t		ndirty;
} txg_stat_t;

extern void spa_stats_init(spa_t *spa);
extern void spa_stats_destroy(spa_t *spa);
extern void spa_read_history_add(spa_t *spa, const zbookmark_phys_t *zb,
    uint32_t aflags);
extern void spa_txg_history_add(spa_t *spa, uint64_t txg, hrtime_t birth_time);
extern int spa_txg_history_set(spa_t *spa,  uint64_t txg,
    txg_state_t completed_state, hrtime_t completed_time);
extern txg_stat_t *spa_txg_history_init_io(spa_t *, uint64_t,
    struct dsl_pool *);
extern void spa_txg_history_fini_io(spa_t *, txg_stat_t *);
extern void spa_tx_assign_add_nsecs(spa_t *spa, uint64_t nsecs);
extern void spa_mmp_history_add(uint64_t txg, uint64_t timestamp,
    uint64_t mmp_delay, vdev_t *vd, int label);

/* Pool configuration locks */
extern int spa_config_tryenter(spa_t *spa, int locks, void *tag, krw_t rw);
extern void spa_config_enter(spa_t *spa, int locks, void *tag, krw_t rw);
extern void spa_config_exit(spa_t *spa, int locks, void *tag);
extern int spa_config_held(spa_t *spa, int locks, krw_t rw);

/* Pool vdev add/remove lock */
extern uint64_t spa_vdev_enter(spa_t *spa);
extern uint64_t spa_vdev_config_enter(spa_t *spa);
extern void spa_vdev_config_exit(spa_t *spa, vdev_t *vd, uint64_t txg,
    int error, char *tag);
extern int spa_vdev_exit(spa_t *spa, vdev_t *vd, uint64_t txg, int error);

/* Pool vdev state change lock */
extern void spa_vdev_state_enter(spa_t *spa, int oplock);
extern int spa_vdev_state_exit(spa_t *spa, vdev_t *vd, int error);

/* Log state */
typedef enum spa_log_state {
	SPA_LOG_UNKNOWN = 0,	/* unknown log state */
	SPA_LOG_MISSING,	/* missing log(s) */
	SPA_LOG_CLEAR,		/* clear the log(s) */
	SPA_LOG_GOOD,		/* log(s) are good */
} spa_log_state_t;

extern spa_log_state_t spa_get_log_state(spa_t *spa);
extern void spa_set_log_state(spa_t *spa, spa_log_state_t state);
extern int spa_offline_log(spa_t *spa);

/* Log claim callback */
extern void spa_claim_notify(zio_t *zio);
extern void spa_deadman(void *);

/* Accessor functions */
extern boolean_t spa_shutting_down(spa_t *spa);
extern struct dsl_pool *spa_get_dsl(spa_t *spa);
extern boolean_t spa_is_initializing(spa_t *spa);
extern blkptr_t *spa_get_rootblkptr(spa_t *spa);
extern void spa_set_rootblkptr(spa_t *spa, const blkptr_t *bp);
extern void spa_altroot(spa_t *, char *, size_t);
extern int spa_sync_pass(spa_t *spa);
extern char *spa_name(spa_t *spa);
extern uint64_t spa_guid(spa_t *spa);
extern uint64_t spa_load_guid(spa_t *spa);
extern uint64_t spa_last_synced_txg(spa_t *spa);
extern uint64_t spa_first_txg(spa_t *spa);
extern uint64_t spa_syncing_txg(spa_t *spa);
extern uint64_t spa_final_dirty_txg(spa_t *spa);
extern uint64_t spa_version(spa_t *spa);
extern pool_state_t spa_state(spa_t *spa);
extern spa_load_state_t spa_load_state(spa_t *spa);
extern uint64_t spa_freeze_txg(spa_t *spa);
extern uint64_t spa_get_worst_case_asize(spa_t *spa, uint64_t lsize);
extern uint64_t spa_get_dspace(spa_t *spa);
extern uint64_t spa_get_slop_space(spa_t *spa);
extern void spa_update_dspace(spa_t *spa);
extern uint64_t spa_version(spa_t *spa);
extern boolean_t spa_deflate(spa_t *spa);
extern metaslab_class_t *spa_normal_class(spa_t *spa);
extern metaslab_class_t *spa_log_class(spa_t *spa);
extern void spa_evicting_os_register(spa_t *, objset_t *os);
extern void spa_evicting_os_deregister(spa_t *, objset_t *os);
extern void spa_evicting_os_wait(spa_t *spa);
extern int spa_max_replication(spa_t *spa);
extern int spa_prev_software_version(spa_t *spa);
extern uint8_t spa_get_failmode(spa_t *spa);
extern boolean_t spa_suspended(spa_t *spa);
extern uint64_t spa_bootfs(spa_t *spa);
extern uint64_t spa_delegation(spa_t *spa);
extern objset_t *spa_meta_objset(spa_t *spa);
extern uint64_t spa_deadman_synctime(spa_t *spa);

/* Miscellaneous support routines */
extern void spa_activate_mos_feature(spa_t *spa, const char *feature,
    dmu_tx_t *tx);
extern void spa_deactivate_mos_feature(spa_t *spa, const char *feature);
extern int spa_rename(const char *oldname, const char *newname);
extern spa_t *spa_by_guid(uint64_t pool_guid, uint64_t device_guid);
extern boolean_t spa_guid_exists(uint64_t pool_guid, uint64_t device_guid);
extern char *spa_strdup(const char *);
extern void spa_strfree(char *);
extern uint64_t spa_get_random(uint64_t range);
extern uint64_t spa_generate_guid(spa_t *spa);
extern void snprintf_blkptr(char *buf, size_t buflen, const blkptr_t *bp);
extern void spa_freeze(spa_t *spa);
extern int spa_change_guid(spa_t *spa);
extern void spa_upgrade(spa_t *spa, uint64_t version);
extern void spa_evict_all(void);
extern vdev_t *spa_lookup_by_guid(spa_t *spa, uint64_t guid,
    boolean_t l2cache);
extern boolean_t spa_has_spare(spa_t *, uint64_t guid);
extern uint64_t dva_get_dsize_sync(spa_t *spa, const dva_t *dva);
extern uint64_t bp_get_dsize_sync(spa_t *spa, const blkptr_t *bp);
extern uint64_t bp_get_dsize(spa_t *spa, const blkptr_t *bp);
extern boolean_t spa_has_slogs(spa_t *spa);
extern boolean_t spa_is_root(spa_t *spa);
extern boolean_t spa_writeable(spa_t *spa);
extern boolean_t spa_has_pending_synctask(spa_t *spa);
extern int spa_maxblocksize(spa_t *spa);
extern int spa_maxdnodesize(spa_t *spa);
extern void zfs_blkptr_verify(spa_t *spa, const blkptr_t *bp);
extern boolean_t spa_multihost(spa_t *spa);
extern unsigned long spa_get_hostid(void);

extern int spa_mode(spa_t *spa);
extern uint64_t zfs_strtonum(const char *str, char **nptr);

extern char *spa_his_ievent_table[];

extern void spa_history_create_obj(spa_t *spa, dmu_tx_t *tx);
extern int spa_history_get(spa_t *spa, uint64_t *offset, uint64_t *len_read,
    char *his_buf);
extern int spa_history_log(spa_t *spa, const char *his_buf);
extern int spa_history_log_nvl(spa_t *spa, nvlist_t *nvl);
extern void spa_history_log_version(spa_t *spa, const char *operation,
    dmu_tx_t *tx);
extern void spa_history_log_internal(spa_t *spa, const char *operation,
    dmu_tx_t *tx, const char *fmt, ...);
extern void spa_history_log_internal_ds(struct dsl_dataset *ds, const char *op,
    dmu_tx_t *tx, const char *fmt, ...);
extern void spa_history_log_internal_dd(dsl_dir_t *dd, const char *operation,
    dmu_tx_t *tx, const char *fmt, ...);

/* error handling */
struct zbookmark_phys;
extern void spa_log_error(spa_t *spa, zio_t *zio);
extern void zfs_ereport_post(const char *class, spa_t *spa, vdev_t *vd,
    zio_t *zio, uint64_t stateoroffset, uint64_t length);
extern nvlist_t *zfs_event_create(spa_t *spa, vdev_t *vd, const char *type,
    const char *name, nvlist_t *aux);
extern void zfs_post_remove(spa_t *spa, vdev_t *vd);
extern void zfs_post_state_change(spa_t *spa, vdev_t *vd, uint64_t laststate);
extern void zfs_post_autoreplace(spa_t *spa, vdev_t *vd);
extern uint64_t spa_get_errlog_size(spa_t *spa);
extern int spa_get_errlog(spa_t *spa, void *uaddr, size_t *count);
extern void spa_errlog_rotate(spa_t *spa);
extern void spa_errlog_drain(spa_t *spa);
extern void spa_errlog_sync(spa_t *spa, uint64_t txg);
extern void spa_get_errlists(spa_t *spa, avl_tree_t *last, avl_tree_t *scrub);

/* vdev cache */
extern void vdev_cache_stat_init(void);
extern void vdev_cache_stat_fini(void);

/* Initialization and termination */
extern void spa_init(int flags);
extern void spa_fini(void);
extern void spa_boot_init(void);

/* properties */
extern int spa_prop_set(spa_t *spa, nvlist_t *nvp);
extern int spa_prop_get(spa_t *spa, nvlist_t **nvp);
extern void spa_prop_clear_bootfs(spa_t *spa, uint64_t obj, dmu_tx_t *tx);
extern void spa_configfile_set(spa_t *, nvlist_t *, boolean_t);

/* asynchronous event notification */
extern void spa_event_notify(spa_t *spa, vdev_t *vdev, nvlist_t *hist_nvl,
    const char *name);

#ifdef ZFS_DEBUG
#define	dprintf_bp(bp, fmt, ...) do {				\
	if (zfs_flags & ZFS_DEBUG_DPRINTF) {			\
	char *__blkbuf = kmem_alloc(BP_SPRINTF_LEN, KM_SLEEP);	\
	snprintf_blkptr(__blkbuf, BP_SPRINTF_LEN, (bp));	\
	dprintf(fmt " %s\n", __VA_ARGS__, __blkbuf);		\
	kmem_free(__blkbuf, BP_SPRINTF_LEN);			\
	} \
_NOTE(CONSTCOND) } while (0)
#else
#define	dprintf_bp(bp, fmt, ...)
#endif

extern boolean_t spa_debug_enabled(spa_t *spa);
#define	spa_dbgmsg(spa, ...)			\
{						\
	if (spa_debug_enabled(spa))		\
		zfs_dbgmsg(__VA_ARGS__);	\
}

extern int spa_mode_global;			/* mode, e.g. FREAD | FWRITE */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SPA_H */
