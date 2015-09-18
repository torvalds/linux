#ifndef _LINUX_T10_PI_H
#define _LINUX_T10_PI_H

#include <linux/types.h>
#include <linux/blkdev.h>

/*
 * T10 Protection Information tuple.
 */
struct t10_pi_tuple {
	__be16 guard_tag;	/* Checksum */
	__be16 app_tag;		/* Opaque storage */
	__be32 ref_tag;		/* Target LBA or indirect LBA */
};


extern struct blk_integrity t10_pi_type1_crc;
extern struct blk_integrity t10_pi_type1_ip;
extern struct blk_integrity t10_pi_type3_crc;
extern struct blk_integrity t10_pi_type3_ip;

#endif
