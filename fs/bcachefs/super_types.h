/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_SUPER_TYPES_H
#define _BCACHEFS_SUPER_TYPES_H

struct bch_sb_handle {
	struct bch_sb		*sb;
	struct block_device	*bdev;
	struct bio		*bio;
	void			*holder;
	size_t			buffer_size;
	blk_mode_t		mode;
	unsigned		have_layout:1;
	unsigned		have_bio:1;
	unsigned		fs_sb:1;
	u64			seq;
};

struct bch_devs_mask {
	unsigned long d[BITS_TO_LONGS(BCH_SB_MEMBERS_MAX)];
};

struct bch_devs_list {
	u8			nr;
	u8			devs[BCH_BKEY_PTRS_MAX];
};

struct bch_member_cpu {
	u64			nbuckets;	/* device size */
	u16			first_bucket;   /* index of first bucket used */
	u16			bucket_size;	/* sectors */
	u16			group;
	u8			state;
	u8			discard;
	u8			data_allowed;
	u8			durability;
	u8			freespace_initialized;
	u8			valid;
};

struct bch_disk_group_cpu {
	bool				deleted;
	u16				parent;
	struct bch_devs_mask		devs;
};

struct bch_disk_groups_cpu {
	struct rcu_head			rcu;
	unsigned			nr;
	struct bch_disk_group_cpu	entries[];
};

#endif /* _BCACHEFS_SUPER_TYPES_H */
