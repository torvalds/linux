/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_DISK_GROUPS_TYPES_H
#define _BCACHEFS_DISK_GROUPS_TYPES_H

struct bch_disk_group_cpu {
	bool				deleted;
	u16				parent;
	u8				label[BCH_SB_LABEL_SIZE];
	struct bch_devs_mask		devs;
};

struct bch_disk_groups_cpu {
	struct rcu_head			rcu;
	unsigned			nr;
	struct bch_disk_group_cpu	entries[] __counted_by(nr);
};

#endif /* _BCACHEFS_DISK_GROUPS_TYPES_H */
