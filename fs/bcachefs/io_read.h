/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_IO_READ_H
#define _BCACHEFS_IO_READ_H

#include "bkey_buf.h"

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
	u16			bounce:1,
				split:1,
				kmalloc:1,
				have_ioref:1,
				narrow_crcs:1,
				hole:1,
				retry:2,
				context:2;
	};
	u16			_state;
	};

	struct bch_devs_list	devs_have;

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

	struct promote_op	*promote;

	struct bch_io_opts	opts;

	struct work_struct	work;

	struct bio		bio;
};

#define to_rbio(_bio)		container_of((_bio), struct bch_read_bio, bio)

struct bch_devs_mask;
struct cache_promote_op;
struct extent_ptr_decoded;

int __bch2_read_indirect_extent(struct btree_trans *, unsigned *,
				struct bkey_buf *);

static inline int bch2_read_indirect_extent(struct btree_trans *trans,
					    enum btree_id *data_btree,
					    unsigned *offset_into_extent,
					    struct bkey_buf *k)
{
	if (k->k->k.type != KEY_TYPE_reflink_p)
		return 0;

	*data_btree = BTREE_ID_reflink;
	return __bch2_read_indirect_extent(trans, offset_into_extent, k);
}

enum bch_read_flags {
	BCH_READ_RETRY_IF_STALE		= 1 << 0,
	BCH_READ_MAY_PROMOTE		= 1 << 1,
	BCH_READ_USER_MAPPED		= 1 << 2,
	BCH_READ_NODECODE		= 1 << 3,
	BCH_READ_LAST_FRAGMENT		= 1 << 4,

	/* internal: */
	BCH_READ_MUST_BOUNCE		= 1 << 5,
	BCH_READ_MUST_CLONE		= 1 << 6,
	BCH_READ_IN_RETRY		= 1 << 7,
};

int __bch2_read_extent(struct btree_trans *, struct bch_read_bio *,
		       struct bvec_iter, struct bpos, enum btree_id,
		       struct bkey_s_c, unsigned,
		       struct bch_io_failures *, unsigned);

static inline void bch2_read_extent(struct btree_trans *trans,
			struct bch_read_bio *rbio, struct bpos read_pos,
			enum btree_id data_btree, struct bkey_s_c k,
			unsigned offset_into_extent, unsigned flags)
{
	__bch2_read_extent(trans, rbio, rbio->bio.bi_iter, read_pos,
			   data_btree, k, offset_into_extent, NULL, flags);
}

void __bch2_read(struct bch_fs *, struct bch_read_bio *, struct bvec_iter,
		 subvol_inum, struct bch_io_failures *, unsigned flags);

static inline void bch2_read(struct bch_fs *c, struct bch_read_bio *rbio,
			     subvol_inum inum)
{
	struct bch_io_failures failed = { .nr = 0 };

	BUG_ON(rbio->_state);

	rbio->c = c;
	rbio->start_time = local_clock();
	rbio->subvol = inum.subvol;

	__bch2_read(c, rbio, rbio->bio.bi_iter, inum, &failed,
		    BCH_READ_RETRY_IF_STALE|
		    BCH_READ_MAY_PROMOTE|
		    BCH_READ_USER_MAPPED);
}

static inline struct bch_read_bio *rbio_init(struct bio *bio,
					     struct bch_io_opts opts)
{
	struct bch_read_bio *rbio = to_rbio(bio);

	rbio->_state	= 0;
	rbio->promote	= NULL;
	rbio->opts	= opts;
	return rbio;
}

void bch2_fs_io_read_exit(struct bch_fs *);
int bch2_fs_io_read_init(struct bch_fs *);

#endif /* _BCACHEFS_IO_READ_H */
