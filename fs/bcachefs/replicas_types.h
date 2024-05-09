/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_REPLICAS_TYPES_H
#define _BCACHEFS_REPLICAS_TYPES_H

struct bch_replicas_cpu {
	unsigned		nr;
	unsigned		entry_size;
	struct bch_replicas_entry *entries;
};

struct replicas_delta {
	s64			delta;
	struct bch_replicas_entry r;
} __packed;

struct replicas_delta_list {
	unsigned		size;
	unsigned		used;

	struct			{} memset_start;
	u64			nr_inodes;
	u64			persistent_reserved[BCH_REPLICAS_MAX];
	struct			{} memset_end;
	struct replicas_delta	d[0];
};

#endif /* _BCACHEFS_REPLICAS_TYPES_H */
