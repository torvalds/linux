/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_DISK_GROUPS_FORMAT_H
#define _BCACHEFS_DISK_GROUPS_FORMAT_H

#define BCH_SB_LABEL_SIZE		32

struct bch_disk_group {
	__u8			label[BCH_SB_LABEL_SIZE];
	__le64			flags[2];
} __packed __aligned(8);

LE64_BITMASK(BCH_GROUP_DELETED,		struct bch_disk_group, flags[0], 0,  1)
LE64_BITMASK(BCH_GROUP_DATA_ALLOWED,	struct bch_disk_group, flags[0], 1,  6)
LE64_BITMASK(BCH_GROUP_PARENT,		struct bch_disk_group, flags[0], 6, 24)

struct bch_sb_field_disk_groups {
	struct bch_sb_field	field;
	struct bch_disk_group	entries[];
} __packed __aligned(8);

#endif /* _BCACHEFS_DISK_GROUPS_FORMAT_H */
