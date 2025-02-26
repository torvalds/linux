/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_REBALANCE_FORMAT_H
#define _BCACHEFS_REBALANCE_FORMAT_H

struct bch_extent_rebalance {
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u64			type:6,
				unused:3,

				promote_target_from_inode:1,
				erasure_code_from_inode:1,
				data_checksum_from_inode:1,
				background_compression_from_inode:1,
				data_replicas_from_inode:1,
				background_target_from_inode:1,

				promote_target:16,
				erasure_code:1,
				data_checksum:4,
				data_replicas:4,
				background_compression:8, /* enum bch_compression_opt */
				background_target:16;
#elif defined (__BIG_ENDIAN_BITFIELD)
	__u64			background_target:16,
				background_compression:8,
				data_replicas:4,
				data_checksum:4,
				erasure_code:1,
				promote_target:16,

				background_target_from_inode:1,
				data_replicas_from_inode:1,
				background_compression_from_inode:1,
				data_checksum_from_inode:1,
				erasure_code_from_inode:1,
				promote_target_from_inode:1,

				unused:3,
				type:6;
#endif
};

/* subset of BCH_INODE_OPTS */
#define BCH_REBALANCE_OPTS()			\
	x(data_checksum)			\
	x(background_compression)		\
	x(data_replicas)			\
	x(promote_target)			\
	x(background_target)			\
	x(erasure_code)

#endif /* _BCACHEFS_REBALANCE_FORMAT_H */

