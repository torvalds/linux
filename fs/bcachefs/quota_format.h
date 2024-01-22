/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_QUOTA_FORMAT_H
#define _BCACHEFS_QUOTA_FORMAT_H

/* KEY_TYPE_quota: */

enum quota_types {
	QTYP_USR		= 0,
	QTYP_GRP		= 1,
	QTYP_PRJ		= 2,
	QTYP_NR			= 3,
};

enum quota_counters {
	Q_SPC			= 0,
	Q_INO			= 1,
	Q_COUNTERS		= 2,
};

struct bch_quota_counter {
	__le64			hardlimit;
	__le64			softlimit;
};

struct bch_quota {
	struct bch_val		v;
	struct bch_quota_counter c[Q_COUNTERS];
} __packed __aligned(8);

/* BCH_SB_FIELD_quota: */

struct bch_sb_quota_counter {
	__le32				timelimit;
	__le32				warnlimit;
};

struct bch_sb_quota_type {
	__le64				flags;
	struct bch_sb_quota_counter	c[Q_COUNTERS];
};

struct bch_sb_field_quota {
	struct bch_sb_field		field;
	struct bch_sb_quota_type	q[QTYP_NR];
} __packed __aligned(8);

#endif /* _BCACHEFS_QUOTA_FORMAT_H */
