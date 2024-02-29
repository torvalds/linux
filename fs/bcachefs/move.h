/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_MOVE_H
#define _BCACHEFS_MOVE_H

#include "bbpos.h"
#include "bcachefs_ioctl.h"
#include "btree_iter.h"
#include "buckets.h"
#include "data_update.h"
#include "move_types.h"

struct bch_read_bio;

struct moving_context {
	struct btree_trans	*trans;
	struct list_head	list;
	void			*fn;

	struct bch_ratelimit	*rate;
	struct bch_move_stats	*stats;
	struct write_point_specifier wp;
	bool			wait_on_copygc;
	bool			write_error;

	/* For waiting on outstanding reads and writes: */
	struct closure		cl;

	struct mutex		lock;
	struct list_head	reads;
	struct list_head	ios;

	/* in flight sectors: */
	atomic_t		read_sectors;
	atomic_t		write_sectors;
	atomic_t		read_ios;
	atomic_t		write_ios;

	wait_queue_head_t	wait;
};

#define move_ctxt_wait_event_timeout(_ctxt, _cond, _timeout)			\
({										\
	int _ret = 0;								\
	while (true) {								\
		bool cond_finished = false;					\
		bch2_moving_ctxt_do_pending_writes(_ctxt);			\
										\
		if (_cond)							\
			break;							\
		bch2_trans_unlock_long((_ctxt)->trans);				\
		_ret = __wait_event_timeout((_ctxt)->wait,			\
			     bch2_moving_ctxt_next_pending_write(_ctxt) ||	\
			     (cond_finished = (_cond)), _timeout);		\
		if (_ret || ( cond_finished))					\
			break;							\
	}									\
	_ret;									\
})

#define move_ctxt_wait_event(_ctxt, _cond)				\
do {									\
	bool cond_finished = false;					\
	bch2_moving_ctxt_do_pending_writes(_ctxt);			\
									\
	if (_cond)							\
		break;							\
	bch2_trans_unlock_long((_ctxt)->trans);				\
	__wait_event((_ctxt)->wait,					\
		     bch2_moving_ctxt_next_pending_write(_ctxt) ||	\
		     (cond_finished = (_cond)));			\
	if (cond_finished)						\
		break;							\
} while (1)

typedef bool (*move_pred_fn)(struct bch_fs *, void *, struct bkey_s_c,
			     struct bch_io_opts *, struct data_update_opts *);

extern const char * const bch2_data_ops_strs[];

void bch2_moving_ctxt_exit(struct moving_context *);
void bch2_moving_ctxt_init(struct moving_context *, struct bch_fs *,
			   struct bch_ratelimit *, struct bch_move_stats *,
			   struct write_point_specifier, bool);
struct moving_io *bch2_moving_ctxt_next_pending_write(struct moving_context *);
void bch2_moving_ctxt_do_pending_writes(struct moving_context *);
void bch2_moving_ctxt_flush_all(struct moving_context *);
void bch2_move_ctxt_wait_for_io(struct moving_context *);
int bch2_move_ratelimit(struct moving_context *);

/* Inodes in different snapshots may have different IO options: */
struct snapshot_io_opts_entry {
	u32			snapshot;
	struct bch_io_opts	io_opts;
};

struct per_snapshot_io_opts {
	u64			cur_inum;
	struct bch_io_opts	fs_io_opts;
	DARRAY(struct snapshot_io_opts_entry) d;
};

static inline void per_snapshot_io_opts_init(struct per_snapshot_io_opts *io_opts, struct bch_fs *c)
{
	memset(io_opts, 0, sizeof(*io_opts));
	io_opts->fs_io_opts = bch2_opts_to_inode_opts(c->opts);
}

static inline void per_snapshot_io_opts_exit(struct per_snapshot_io_opts *io_opts)
{
	darray_exit(&io_opts->d);
}

struct bch_io_opts *bch2_move_get_io_opts(struct btree_trans *,
				struct per_snapshot_io_opts *, struct bkey_s_c);
int bch2_move_get_io_opts_one(struct btree_trans *, struct bch_io_opts *, struct bkey_s_c);

int bch2_scan_old_btree_nodes(struct bch_fs *, struct bch_move_stats *);

int bch2_move_extent(struct moving_context *,
		     struct move_bucket_in_flight *,
		     struct btree_iter *,
		     struct bkey_s_c,
		     struct bch_io_opts,
		     struct data_update_opts);

int __bch2_move_data(struct moving_context *,
		     struct bbpos,
		     struct bbpos,
		     move_pred_fn, void *);
int bch2_move_data(struct bch_fs *,
		   struct bbpos start,
		   struct bbpos end,
		   struct bch_ratelimit *,
		   struct bch_move_stats *,
		   struct write_point_specifier,
		   bool,
		   move_pred_fn, void *);

int bch2_evacuate_bucket(struct moving_context *,
			   struct move_bucket_in_flight *,
			   struct bpos, int,
			   struct data_update_opts);
int bch2_data_job(struct bch_fs *,
		  struct bch_move_stats *,
		  struct bch_ioctl_data);

void bch2_move_stats_to_text(struct printbuf *, struct bch_move_stats *);
void bch2_move_stats_exit(struct bch_move_stats *, struct bch_fs *);
void bch2_move_stats_init(struct bch_move_stats *, const char *);

void bch2_fs_moving_ctxts_to_text(struct printbuf *, struct bch_fs *);

void bch2_fs_move_init(struct bch_fs *);

#endif /* _BCACHEFS_MOVE_H */
