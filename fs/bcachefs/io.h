/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_IO_H
#define _BCACHEFS_IO_H

#include "checksum.h"
#include "bkey_buf.h"
#include "io_types.h"

#define to_wbio(_bio)			\
	container_of((_bio), struct bch_write_bio, bio)

#define to_rbio(_bio)			\
	container_of((_bio), struct bch_read_bio, bio)

void bch2_bio_free_pages_pool(struct bch_fs *, struct bio *);
void bch2_bio_alloc_pages_pool(struct bch_fs *, struct bio *, size_t);

#ifndef CONFIG_BCACHEFS_NO_LATENCY_ACCT
void bch2_latency_acct(struct bch_dev *, u64, int);
#else
static inline void bch2_latency_acct(struct bch_dev *ca, u64 submit_time, int rw) {}
#endif

void bch2_submit_wbio_replicas(struct bch_write_bio *, struct bch_fs *,
			       enum bch_data_type, const struct bkey_i *);

#define BLK_STS_REMOVED		((__force blk_status_t)128)

const char *bch2_blk_status_to_str(blk_status_t);

enum bch_write_flags {
	__BCH_WRITE_ALLOC_NOWAIT,
	__BCH_WRITE_CACHED,
	__BCH_WRITE_FLUSH,
	__BCH_WRITE_DATA_ENCODED,
	__BCH_WRITE_PAGES_STABLE,
	__BCH_WRITE_PAGES_OWNED,
	__BCH_WRITE_ONLY_SPECIFIED_DEVS,
	__BCH_WRITE_WROTE_DATA_INLINE,
	__BCH_WRITE_FROM_INTERNAL,
	__BCH_WRITE_CHECK_ENOSPC,
	__BCH_WRITE_SYNC,
	__BCH_WRITE_MOVE,
	__BCH_WRITE_IN_WORKER,
	__BCH_WRITE_DONE,
	__BCH_WRITE_IO_ERROR,
};

#define BCH_WRITE_ALLOC_NOWAIT		(1U << __BCH_WRITE_ALLOC_NOWAIT)
#define BCH_WRITE_CACHED		(1U << __BCH_WRITE_CACHED)
#define BCH_WRITE_FLUSH			(1U << __BCH_WRITE_FLUSH)
#define BCH_WRITE_DATA_ENCODED		(1U << __BCH_WRITE_DATA_ENCODED)
#define BCH_WRITE_PAGES_STABLE		(1U << __BCH_WRITE_PAGES_STABLE)
#define BCH_WRITE_PAGES_OWNED		(1U << __BCH_WRITE_PAGES_OWNED)
#define BCH_WRITE_ONLY_SPECIFIED_DEVS	(1U << __BCH_WRITE_ONLY_SPECIFIED_DEVS)
#define BCH_WRITE_WROTE_DATA_INLINE	(1U << __BCH_WRITE_WROTE_DATA_INLINE)
#define BCH_WRITE_FROM_INTERNAL		(1U << __BCH_WRITE_FROM_INTERNAL)
#define BCH_WRITE_CHECK_ENOSPC		(1U << __BCH_WRITE_CHECK_ENOSPC)
#define BCH_WRITE_SYNC			(1U << __BCH_WRITE_SYNC)
#define BCH_WRITE_MOVE			(1U << __BCH_WRITE_MOVE)

/* Internal: */
#define BCH_WRITE_IN_WORKER		(1U << __BCH_WRITE_IN_WORKER)
#define BCH_WRITE_DONE			(1U << __BCH_WRITE_DONE)
#define BCH_WRITE_IO_ERROR		(1U << __BCH_WRITE_IO_ERROR)

static inline struct workqueue_struct *index_update_wq(struct bch_write_op *op)
{
	return op->alloc_reserve == RESERVE_movinggc
		? op->c->copygc_wq
		: op->c->btree_update_wq;
}

int bch2_sum_sector_overwrites(struct btree_trans *, struct btree_iter *,
			       struct bkey_i *, bool *, bool *, s64 *, s64 *);
int bch2_extent_update(struct btree_trans *, subvol_inum,
		       struct btree_iter *, struct bkey_i *,
		       struct disk_reservation *, u64 *, u64, s64 *, bool);

int bch2_fpunch_at(struct btree_trans *, struct btree_iter *,
		   subvol_inum, u64, s64 *);
int bch2_fpunch(struct bch_fs *c, subvol_inum, u64, u64, s64 *);

static inline void bch2_write_op_init(struct bch_write_op *op, struct bch_fs *c,
				      struct bch_io_opts opts)
{
	op->c			= c;
	op->end_io		= NULL;
	op->flags		= 0;
	op->written		= 0;
	op->error		= 0;
	op->csum_type		= bch2_data_checksum_type(c, opts.data_checksum);
	op->compression_type	= bch2_compression_opt_to_type[opts.compression];
	op->nr_replicas		= 0;
	op->nr_replicas_required = c->opts.data_replicas_required;
	op->alloc_reserve	= RESERVE_none;
	op->incompressible	= 0;
	op->open_buckets.nr	= 0;
	op->devs_have.nr	= 0;
	op->target		= 0;
	op->opts		= opts;
	op->subvol		= 0;
	op->pos			= POS_MAX;
	op->version		= ZERO_VERSION;
	op->write_point		= (struct write_point_specifier) { 0 };
	op->res			= (struct disk_reservation) { 0 };
	op->journal_seq		= 0;
	op->new_i_size		= U64_MAX;
	op->i_sectors_delta	= 0;
}

void bch2_write(struct closure *);

void bch2_write_point_do_index_updates(struct work_struct *);

static inline struct bch_write_bio *wbio_init(struct bio *bio)
{
	struct bch_write_bio *wbio = to_wbio(bio);

	memset(&wbio->wbio, 0, sizeof(wbio->wbio));
	return wbio;
}

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

void bch2_fs_io_exit(struct bch_fs *);
int bch2_fs_io_init(struct bch_fs *);

#endif /* _BCACHEFS_IO_H */
