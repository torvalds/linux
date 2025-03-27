/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _BCACHEFS_DATA_UPDATE_H
#define _BCACHEFS_DATA_UPDATE_H

#include "bkey_buf.h"
#include "io_read.h"
#include "io_write_types.h"

struct moving_context;

struct data_update_opts {
	unsigned	rewrite_ptrs;
	unsigned	kill_ptrs;
	u16		target;
	u8		extra_replicas;
	unsigned	btree_insert_flags;
	unsigned	write_flags;

	int		read_dev;
	bool		scrub;
};

void bch2_data_update_opts_to_text(struct printbuf *, struct bch_fs *,
				   struct bch_io_opts *, struct data_update_opts *);

struct data_update {
	/* extent being updated: */
	bool			read_done;
	enum btree_id		btree_id;
	struct bkey_buf		k;
	struct data_update_opts	data_opts;
	struct moving_context	*ctxt;
	struct bch_move_stats	*stats;

	struct bch_read_bio	rbio;
	struct bch_write_op	op;
	struct bio_vec		*bvecs;
};

void bch2_data_update_to_text(struct printbuf *, struct data_update *);
void bch2_data_update_inflight_to_text(struct printbuf *, struct data_update *);

int bch2_data_update_index_update(struct bch_write_op *);

void bch2_data_update_read_done(struct data_update *);

int bch2_extent_drop_ptrs(struct btree_trans *,
			  struct btree_iter *,
			  struct bkey_s_c,
			  struct bch_io_opts *,
			  struct data_update_opts *);

int bch2_data_update_bios_init(struct data_update *, struct bch_fs *,
			       struct bch_io_opts *);

void bch2_data_update_exit(struct data_update *);
int bch2_data_update_init(struct btree_trans *, struct btree_iter *,
			  struct moving_context *,
			  struct data_update *,
			  struct write_point_specifier,
			  struct bch_io_opts *, struct data_update_opts,
			  enum btree_id, struct bkey_s_c);
void bch2_data_update_opts_normalize(struct bkey_s_c, struct data_update_opts *);

#endif /* _BCACHEFS_DATA_UPDATE_H */
