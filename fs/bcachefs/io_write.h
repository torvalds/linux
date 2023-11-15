/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_IO_WRITE_H
#define _BCACHEFS_IO_WRITE_H

#include "checksum.h"
#include "io_write_types.h"

#define to_wbio(_bio)			\
	container_of((_bio), struct bch_write_bio, bio)

void bch2_bio_free_pages_pool(struct bch_fs *, struct bio *);
void bch2_bio_alloc_pages_pool(struct bch_fs *, struct bio *, size_t);

#ifndef CONFIG_BCACHEFS_NO_LATENCY_ACCT
void bch2_latency_acct(struct bch_dev *, u64, int);
#else
static inline void bch2_latency_acct(struct bch_dev *ca, u64 submit_time, int rw) {}
#endif

void bch2_submit_wbio_replicas(struct bch_write_bio *, struct bch_fs *,
			       enum bch_data_type, const struct bkey_i *, bool);

#define BCH_WRITE_FLAGS()		\
	x(ALLOC_NOWAIT)			\
	x(CACHED)			\
	x(DATA_ENCODED)			\
	x(PAGES_STABLE)			\
	x(PAGES_OWNED)			\
	x(ONLY_SPECIFIED_DEVS)		\
	x(WROTE_DATA_INLINE)		\
	x(FROM_INTERNAL)		\
	x(CHECK_ENOSPC)			\
	x(SYNC)				\
	x(MOVE)				\
	x(IN_WORKER)			\
	x(DONE)				\
	x(IO_ERROR)			\
	x(CONVERT_UNWRITTEN)

enum __bch_write_flags {
#define x(f)	__BCH_WRITE_##f,
	BCH_WRITE_FLAGS()
#undef x
};

enum bch_write_flags {
#define x(f)	BCH_WRITE_##f = BIT(__BCH_WRITE_##f),
	BCH_WRITE_FLAGS()
#undef x
};

static inline struct workqueue_struct *index_update_wq(struct bch_write_op *op)
{
	return op->watermark == BCH_WATERMARK_copygc
		? op->c->copygc_wq
		: op->c->btree_update_wq;
}

int bch2_sum_sector_overwrites(struct btree_trans *, struct btree_iter *,
			       struct bkey_i *, bool *, s64 *, s64 *);
int bch2_extent_update(struct btree_trans *, subvol_inum,
		       struct btree_iter *, struct bkey_i *,
		       struct disk_reservation *, u64, s64 *, bool);

static inline void bch2_write_op_init(struct bch_write_op *op, struct bch_fs *c,
				      struct bch_io_opts opts)
{
	op->c			= c;
	op->end_io		= NULL;
	op->flags		= 0;
	op->written		= 0;
	op->error		= 0;
	op->csum_type		= bch2_data_checksum_type(c, opts);
	op->compression_opt	= opts.compression;
	op->nr_replicas		= 0;
	op->nr_replicas_required = c->opts.data_replicas_required;
	op->watermark		= BCH_WATERMARK_normal;
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
	op->new_i_size		= U64_MAX;
	op->i_sectors_delta	= 0;
	op->devs_need_flush	= NULL;
}

void bch2_write(struct closure *);

void bch2_write_point_do_index_updates(struct work_struct *);

static inline struct bch_write_bio *wbio_init(struct bio *bio)
{
	struct bch_write_bio *wbio = to_wbio(bio);

	memset(&wbio->wbio, 0, sizeof(wbio->wbio));
	return wbio;
}

void bch2_write_op_to_text(struct printbuf *, struct bch_write_op *);

void bch2_fs_io_write_exit(struct bch_fs *);
int bch2_fs_io_write_init(struct bch_fs *);

#endif /* _BCACHEFS_IO_WRITE_H */
