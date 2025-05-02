/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_IO_READ_H
#define _BCACHEFS_IO_READ_H

#include "bkey_buf.h"
#include "btree_iter.h"
#include "reflink.h"

struct bch_read_bio {
	struct bch_fs		*c;
	u64			start_time;
	u64			submit_time;

	/*
	 * Reads will often have to be split, and if the extent being read from
	 * was checksummed or compressed we'll also have to allocate bounce
	 * buffers and copy the data back into the original bio.
	 *
	 * If we didn't have to split, we have to save and restore the original
	 * bi_end_io - @split below indicates which:
	 */
	union {
	struct bch_read_bio	*parent;
	bio_end_io_t		*end_io;
	};

	/*
	 * Saved copy of bio->bi_iter, from submission time - allows us to
	 * resubmit on IO error, and also to copy data back to the original bio
	 * when we're bouncing:
	 */
	struct bvec_iter	bvec_iter;

	unsigned		offset_into_extent;

	u16			flags;
	union {
	struct {
	u16			data_update:1,
				promote:1,
				bounce:1,
				split:1,
				have_ioref:1,
				narrow_crcs:1,
				saw_error:1,
				context:2;
	};
	u16			_state;
	};
	s16			ret;

	struct extent_ptr_decoded pick;

	/*
	 * pos we read from - different from data_pos for indirect extents:
	 */
	u32			subvol;
	struct bpos		read_pos;

	/*
	 * start pos of data we read (may not be pos of data we want) - for
	 * promote, narrow extents paths:
	 */
	enum btree_id		data_btree;
	struct bpos		data_pos;
	struct bversion		version;

	struct bch_io_opts	opts;

	struct work_struct	work;

	struct bio		bio;
};

#define to_rbio(_bio)		container_of((_bio), struct bch_read_bio, bio)

struct bch_devs_mask;
struct cache_promote_op;
struct extent_ptr_decoded;

static inline int bch2_read_indirect_extent(struct btree_trans *trans,
					    enum btree_id *data_btree,
					    s64 *offset_into_extent,
					    struct bkey_buf *extent)
{
	if (extent->k->k.type != KEY_TYPE_reflink_p)
		return 0;

	*data_btree = BTREE_ID_reflink;
	struct btree_iter iter;
	struct bkey_s_c k = bch2_lookup_indirect_extent(trans, &iter,
						offset_into_extent,
						bkey_i_to_s_c_reflink_p(extent->k),
						true, 0);
	int ret = bkey_err(k);
	if (ret)
		return ret;

	if (bkey_deleted(k.k)) {
		bch2_trans_iter_exit(trans, &iter);
		return -BCH_ERR_missing_indirect_extent;
	}

	bch2_bkey_buf_reassemble(extent, trans->c, k);
	bch2_trans_iter_exit(trans, &iter);
	return 0;
}

#define BCH_READ_FLAGS()		\
	x(retry_if_stale)		\
	x(may_promote)			\
	x(user_mapped)			\
	x(last_fragment)		\
	x(must_bounce)			\
	x(must_clone)			\
	x(in_retry)

enum __bch_read_flags {
#define x(n)	__BCH_READ_##n,
	BCH_READ_FLAGS()
#undef x
};

enum bch_read_flags {
#define x(n)	BCH_READ_##n = BIT(__BCH_READ_##n),
	BCH_READ_FLAGS()
#undef x
};

int __bch2_read_extent(struct btree_trans *, struct bch_read_bio *,
		       struct bvec_iter, struct bpos, enum btree_id,
		       struct bkey_s_c, unsigned,
		       struct bch_io_failures *, unsigned, int);

static inline void bch2_read_extent(struct btree_trans *trans,
			struct bch_read_bio *rbio, struct bpos read_pos,
			enum btree_id data_btree, struct bkey_s_c k,
			unsigned offset_into_extent, unsigned flags)
{
	int ret = __bch2_read_extent(trans, rbio, rbio->bio.bi_iter, read_pos,
				     data_btree, k, offset_into_extent, NULL, flags, -1);
	/* __bch2_read_extent only returns errors if BCH_READ_in_retry is set */
	WARN(ret, "unhandled error from __bch2_read_extent()");
}

int __bch2_read(struct btree_trans *, struct bch_read_bio *, struct bvec_iter,
		subvol_inum, struct bch_io_failures *, unsigned flags);

static inline void bch2_read(struct bch_fs *c, struct bch_read_bio *rbio,
			     subvol_inum inum)
{
	BUG_ON(rbio->_state);

	rbio->subvol = inum.subvol;

	bch2_trans_run(c,
		__bch2_read(trans, rbio, rbio->bio.bi_iter, inum, NULL,
			    BCH_READ_retry_if_stale|
			    BCH_READ_may_promote|
			    BCH_READ_user_mapped));
}

static inline struct bch_read_bio *rbio_init_fragment(struct bio *bio,
						      struct bch_read_bio *orig)
{
	struct bch_read_bio *rbio = to_rbio(bio);

	rbio->c			= orig->c;
	rbio->_state		= 0;
	rbio->flags		= 0;
	rbio->ret		= 0;
	rbio->split		= true;
	rbio->parent		= orig;
	rbio->opts		= orig->opts;
	return rbio;
}

static inline struct bch_read_bio *rbio_init(struct bio *bio,
					     struct bch_fs *c,
					     struct bch_io_opts opts,
					     bio_end_io_t end_io)
{
	struct bch_read_bio *rbio = to_rbio(bio);

	rbio->start_time	= local_clock();
	rbio->c			= c;
	rbio->_state		= 0;
	rbio->flags		= 0;
	rbio->ret		= 0;
	rbio->opts		= opts;
	rbio->bio.bi_end_io	= end_io;
	return rbio;
}

void bch2_fs_io_read_exit(struct bch_fs *);
int bch2_fs_io_read_init(struct bch_fs *);

#endif /* _BCACHEFS_IO_READ_H */
