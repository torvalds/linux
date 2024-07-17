/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_IO_WRITE_TYPES_H
#define _BCACHEFS_IO_WRITE_TYPES_H

#include "alloc_types.h"
#include "btree_types.h"
#include "buckets_types.h"
#include "extents_types.h"
#include "keylist_types.h"
#include "opts.h"
#include "super_types.h"

#include <linux/llist.h>
#include <linux/workqueue.h>

struct bch_write_bio {
	struct_group(wbio,
	struct bch_fs		*c;
	struct bch_write_bio	*parent;

	u64			submit_time;
	u64			inode_offset;
	u64			nocow_bucket;

	struct bch_devs_list	failed;
	u8			dev;

	unsigned		split:1,
				bounce:1,
				put_bio:1,
				have_ioref:1,
				nocow:1,
				used_mempool:1,
				first_btree_write:1;
	);

	struct bio		bio;
};

struct bch_write_op {
	struct closure		cl;
	struct bch_fs		*c;
	void			(*end_io)(struct bch_write_op *);
	u64			start_time;

	unsigned		written; /* sectors */
	u16			flags;
	s16			error; /* dio write path expects it to hold -ERESTARTSYS... */

	unsigned		compression_opt:8;
	unsigned		csum_type:4;
	unsigned		nr_replicas:4;
	unsigned		nr_replicas_required:4;
	unsigned		watermark:3;
	unsigned		incompressible:1;
	unsigned		stripe_waited:1;

	struct bch_devs_list	devs_have;
	u16			target;
	u16			nonce;
	struct bch_io_opts	opts;

	u32			subvol;
	struct bpos		pos;
	struct bversion		version;

	/* For BCH_WRITE_DATA_ENCODED: */
	struct bch_extent_crc_unpacked crc;

	struct write_point_specifier write_point;

	struct write_point	*wp;
	struct list_head	wp_list;

	struct disk_reservation	res;

	struct open_buckets	open_buckets;

	u64			new_i_size;
	s64			i_sectors_delta;

	struct bch_devs_mask	failed;

	struct keylist		insert_keys;
	u64			inline_keys[BKEY_EXTENT_U64s_MAX * 2];

	/*
	 * Bitmask of devices that have had nocow writes issued to them since
	 * last flush:
	 */
	struct bch_devs_mask	*devs_need_flush;

	/* Must be last: */
	struct bch_write_bio	wbio;
};

#endif /* _BCACHEFS_IO_WRITE_TYPES_H */
