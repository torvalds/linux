/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_REPLICAS_FORMAT_H
#define _BCACHEFS_REPLICAS_FORMAT_H

struct bch_replicas_entry_v0 {
	__u8			data_type;
	__u8			nr_devs;
	__u8			devs[];
} __packed;

struct bch_sb_field_replicas_v0 {
	struct bch_sb_field	field;
	struct bch_replicas_entry_v0 entries[];
} __packed __aligned(8);

struct bch_replicas_entry_v1 {
	__u8			data_type;
	__u8			nr_devs;
	__u8			nr_required;
	__u8			devs[];
} __packed;

struct bch_sb_field_replicas {
	struct bch_sb_field	field;
	struct bch_replicas_entry_v1 entries[];
} __packed __aligned(8);

#define replicas_entry_bytes(_i)					\
	(offsetof(typeof(*(_i)), devs) + (_i)->nr_devs)

#endif /* _BCACHEFS_REPLICAS_FORMAT_H */
