/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_MOVE_H
#define _BCACHEFS_MOVE_H

#include "btree_iter.h"
#include "buckets.h"
#include "io_types.h"
#include "move_types.h"

struct bch_read_bio;
struct moving_context;

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

struct migrate_write {
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

int bch2_migrate_index_update(struct bch_write_op *);
void bch2_migrate_read_done(struct migrate_write *, struct bch_read_bio *);
int bch2_migrate_write_init(struct bch_fs *, struct migrate_write *,
			    struct write_point_specifier,
			    struct bch_io_opts,
			    enum data_cmd, struct data_opts,
			    enum btree_id, struct bkey_s_c);

typedef enum data_cmd (*move_pred_fn)(struct bch_fs *, void *,
				struct bkey_s_c,
				struct bch_io_opts *, struct data_opts *);

int bch2_scan_old_btree_nodes(struct bch_fs *, struct bch_move_stats *);

int bch2_move_data(struct bch_fs *,
		   enum btree_id, struct bpos,
		   enum btree_id, struct bpos,
		   struct bch_ratelimit *,
		   struct write_point_specifier,
		   move_pred_fn, void *,
		   struct bch_move_stats *);

int bch2_data_job(struct bch_fs *,
		  struct bch_move_stats *,
		  struct bch_ioctl_data);

#endif /* _BCACHEFS_MOVE_H */
