/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_T10_PI_H
#define _LINUX_T10_PI_H

#include <linux/types.h>
#include <linux/blk-mq.h>

/*
 * A T10 PI-capable target device can be formatted with different
 * protection schemes.	Currently 0 through 3 are defined:
 *
 * Type 0 is regular (unprotected) I/O
 *
 * Type 1 defines the contents of the guard and reference tags
 *
 * Type 2 defines the contents of the guard and reference tags and
 * uses 32-byte commands to seed the latter
 *
 * Type 3 defines the contents of the guard tag only
 */
enum t10_dif_type {
	T10_PI_TYPE0_PROTECTION = 0x0,
	T10_PI_TYPE1_PROTECTION = 0x1,
	T10_PI_TYPE2_PROTECTION = 0x2,
	T10_PI_TYPE3_PROTECTION = 0x3,
};

/*
 * T10 Protection Information tuple.
 */
struct t10_pi_tuple {
	__be16 guard_tag;	/* Checksum */
	__be16 app_tag;		/* Opaque storage */
	__be32 ref_tag;		/* Target LBA or indirect LBA */
};

#define T10_PI_APP_ESCAPE cpu_to_be16(0xffff)
#define T10_PI_REF_ESCAPE cpu_to_be32(0xffffffff)

static inline u32 t10_pi_ref_tag(struct request *rq)
{
	unsigned int shift = ilog2(queue_logical_block_size(rq->q));

	if (IS_ENABLED(CONFIG_BLK_DEV_INTEGRITY) &&
	    rq->q->limits.integrity.interval_exp)
		shift = rq->q->limits.integrity.interval_exp;
	return blk_rq_pos(rq) >> (shift - SECTOR_SHIFT) & 0xffffffff;
}

struct crc64_pi_tuple {
	__be64 guard_tag;
	__be16 app_tag;
	__u8   ref_tag[6];
};

/**
 * lower_48_bits() - return bits 0-47 of a number
 * @n: the number we're accessing
 */
static inline u64 lower_48_bits(u64 n)
{
	return n & ((1ull << 48) - 1);
}

static inline u64 ext_pi_ref_tag(struct request *rq)
{
	unsigned int shift = ilog2(queue_logical_block_size(rq->q));

	if (IS_ENABLED(CONFIG_BLK_DEV_INTEGRITY) &&
	    rq->q->limits.integrity.interval_exp)
		shift = rq->q->limits.integrity.interval_exp;
	return lower_48_bits(blk_rq_pos(rq) >> (shift - SECTOR_SHIFT));
}

#endif
