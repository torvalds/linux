/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _BCACHEFS_DATA_UPDATE_H
#define _BCACHEFS_DATA_UPDATE_H

#include "bkey_buf.h"
#include "io_types.h"

struct moving_context;

struct data_update_opts {
	unsigned	rewrite_ptrs;
	unsigned	kill_ptrs;
	u16		target;
	u8		extra_replicas;
	unsigned	btree_insert_flags;
	unsigned	write_flags;
};

struct data_update {
	/* extent being updated: */
	enum btree_id		btree_id;
	struct bkey_buf		k;
	struct data_update_opts	data_opts;
	struct moving_context	*ctxt;
	struct bch_write_op	op;
};

int bch2_data_update_index_update(struct bch_write_op *);

void bch2_data_update_read_done(struct data_update *,
				struct bch_extent_crc_unpacked);

void bch2_data_update_exit(struct data_update *);
void bch2_update_unwritten_extent(struct btree_trans *, struct data_update *);
int bch2_data_update_init(struct bch_fs *, struct data_update *,
			  struct write_point_specifier,
			  struct bch_io_opts, struct data_update_opts,
			  enum btree_id, struct bkey_s_c);
void bch2_data_update_opts_normalize(struct bkey_s_c, struct data_update_opts *);

#endif /* _BCACHEFS_DATA_UPDATE_H */
