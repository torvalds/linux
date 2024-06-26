/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_SB_DOWNGRADE_FORMAT_H
#define _BCACHEFS_SB_DOWNGRADE_FORMAT_H

struct bch_sb_field_downgrade_entry {
	__le16			version;
	__le64			recovery_passes[2];
	__le16			nr_errors;
	__le16			errors[] __counted_by(nr_errors);
} __packed __aligned(2);

struct bch_sb_field_downgrade {
	struct bch_sb_field	field;
	struct bch_sb_field_downgrade_entry entries[];
};

#endif /* _BCACHEFS_SB_DOWNGRADE_FORMAT_H */
