/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _BCACHEFS_DATA_UPDATE_H
#define _BCACHEFS_DATA_UPDATE_H

#include "io_types.h"

enum data_cmd {
	DATA_SKIP,
	DATA_SCRUB,
	DATA_ADD_REPLICAS,
	DATA_REWRITE,
	DATA_PROMOTE,
};

struct data_opts {
	u16		target;
	u8		rewrite_dev;
	u8		nr_replicas;
	int		btree_insert_flags;
};

struct data_update {
	enum btree_id		btree_id;
	enum data_cmd		data_cmd;
	struct data_opts	data_opts;

	unsigned		nr_ptrs_reserved;

	struct moving_context	*ctxt;

	/* what we read: */
	struct bch_extent_ptr	ptr;
	u64			offset;

	struct bch_write_op	op;
};

int bch2_data_update_index_update(struct bch_write_op *);

void bch2_data_update_read_done(struct data_update *, struct bch_read_bio *);
int bch2_data_update_init(struct bch_fs *, struct data_update *,
			  struct write_point_specifier,
			  struct bch_io_opts,
			  enum data_cmd, struct data_opts,
			  enum btree_id, struct bkey_s_c);

#endif /* _BCACHEFS_DATA_UPDATE_H */
