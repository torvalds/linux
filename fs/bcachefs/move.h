/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_MOVE_H
#define _BCACHEFS_MOVE_H

#include "btree_iter.h"
#include "buckets.h"
#include "data_update.h"
#include "move_types.h"

struct bch_read_bio;

struct moving_context {
	/* Closure for waiting on all reads and writes to complete */
	struct closure		cl;

	struct bch_move_stats	*stats;

	struct list_head	reads;

	/* in flight sectors: */
	atomic_t		read_sectors;
	atomic_t		write_sectors;

	wait_queue_head_t	wait;
};

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

inline void bch_move_stats_init(struct bch_move_stats *stats,
				char *name);


#endif /* _BCACHEFS_MOVE_H */
