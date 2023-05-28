// SPDX-License-Identifier: GPL-2.0
#ifndef NO_BCACHEFS_FS

#include "bcachefs.h"
#include "alloc_foreground.h"
#include "bkey_buf.h"
#include "btree_update.h"
#include "buckets.h"
#include "clock.h"
#include "error.h"
#include "extents.h"
#include "extent_update.h"
#include "fs.h"
#include "fs-io.h"
#include "fsck.h"
#include "inode.h"
#include "journal.h"
#include "io.h"
#include "keylist.h"
#include "quota.h"
#include "reflink.h"
#include "trace.h"

#include <linux/aio.h>
#include <linux/backing-dev.h>
#include <linux/falloc.h>
#include <linux/migrate.h>
#include <linux/mmu_context.h>
#include <linux/pagevec.h>
#include <linux/rmap.h>
#include <linux/sched/signal.h>
#include <linux/task_io_accounting_ops.h>
#include <linux/uio.h>
#include <linux/writeback.h>

#include <trace/events/writeback.h>

struct folio_vec {
	struct folio	*fv_folio;
	size_t		fv_offset;
	size_t		fv_len;
};

static inline struct folio_vec biovec_to_foliovec(struct bio_vec bv)
{

	struct folio *folio	= page_folio(bv.bv_page);
	size_t offset		= (folio_page_idx(folio, bv.bv_page) << PAGE_SHIFT) +
		bv.bv_offset;
	size_t len = min_t(size_t, folio_size(folio) - offset, bv.bv_len);

	return (struct folio_vec) {
		.fv_folio	= folio,
		.fv_offset	= offset,
		.fv_len		= len,
	};
}

static inline struct folio_vec bio_iter_iovec_folio(struct bio *bio,
						    struct bvec_iter iter)
{
	return biovec_to_foliovec(bio_iter_iovec(bio, iter));
}

#define __bio_for_each_folio(bvl, bio, iter, start)			\
	for (iter = (start);						\
	     (iter).bi_size &&						\
		((bvl = bio_iter_iovec_folio((bio), (iter))), 1);	\
	     bio_advance_iter_single((bio), &(iter), (bvl).fv_len))

/**
 * bio_for_each_folio - iterate over folios within a bio
 *
 * Like other non-_all versions, this iterates over what bio->bi_iter currently
 * points to. This version is for drivers, where the bio may have previously
 * been split or cloned.
 */
#define bio_for_each_folio(bvl, bio, iter)				\
	__bio_for_each_folio(bvl, bio, iter, (bio)->bi_iter)

/*
 * Use u64 for the end pos and sector helpers because if the folio covers the
 * max supported range of the mapping, the start offset of the next folio
 * overflows loff_t. This breaks much of the range based processing in the
 * buffered write path.
 */
static inline u64 folio_end_pos(struct folio *folio)
{
	return folio_pos(folio) + folio_size(folio);
}

static inline size_t folio_sectors(struct folio *folio)
{
	return PAGE_SECTORS << folio_order(folio);
}

static inline loff_t folio_sector(struct folio *folio)
{
	return folio_pos(folio) >> 9;
}

static inline u64 folio_end_sector(struct folio *folio)
{
	return folio_end_pos(folio) >> 9;
}

typedef DARRAY(struct folio *) folios;

static int filemap_get_contig_folios_d(struct address_space *mapping,
				       loff_t start, u64 end,
				       int fgp_flags, gfp_t gfp,
				       folios *folios)
{
	struct folio *f;
	u64 pos = start;
	int ret = 0;

	while (pos < end) {
		if ((u64) pos >= (u64) start + (1ULL << 20))
			fgp_flags &= ~FGP_CREAT;

		ret = darray_make_room_gfp(folios, 1, gfp & GFP_KERNEL);
		if (ret)
			break;

		f = __filemap_get_folio(mapping, pos >> PAGE_SHIFT, fgp_flags, gfp);
		if (!f)
			break;

		BUG_ON(folios->nr && folio_pos(f) != pos);

		pos = folio_end_pos(f);
		darray_push(folios, f);
	}

	if (!folios->nr && !ret && (fgp_flags & FGP_CREAT))
		ret = -ENOMEM;

	return folios->nr ? 0 : ret;
}

struct nocow_flush {
	struct closure	*cl;
	struct bch_dev	*ca;
	struct bio	bio;
};

static void nocow_flush_endio(struct bio *_bio)
{

	struct nocow_flush *bio = container_of(_bio, struct nocow_flush, bio);

	closure_put(bio->cl);
	percpu_ref_put(&bio->ca->io_ref);
	bio_put(&bio->bio);
}

static void bch2_inode_flush_nocow_writes_async(struct bch_fs *c,
						struct bch_inode_info *inode,
						struct closure *cl)
{
	struct nocow_flush *bio;
	struct bch_dev *ca;
	struct bch_devs_mask devs;
	unsigned dev;

	dev = find_first_bit(inode->ei_devs_need_flush.d, BCH_SB_MEMBERS_MAX);
	if (dev == BCH_SB_MEMBERS_MAX)
		return;

	devs = inode->ei_devs_need_flush;
	memset(&inode->ei_devs_need_flush, 0, sizeof(inode->ei_devs_need_flush));

	for_each_set_bit(dev, devs.d, BCH_SB_MEMBERS_MAX) {
		rcu_read_lock();
		ca = rcu_dereference(c->devs[dev]);
		if (ca && !percpu_ref_tryget(&ca->io_ref))
			ca = NULL;
		rcu_read_unlock();

		if (!ca)
			continue;

		bio = container_of(bio_alloc_bioset(ca->disk_sb.bdev, 0,
						    REQ_OP_FLUSH,
						    GFP_KERNEL,
						    &c->nocow_flush_bioset),
				   struct nocow_flush, bio);
		bio->cl			= cl;
		bio->ca			= ca;
		bio->bio.bi_end_io	= nocow_flush_endio;
		closure_bio_submit(&bio->bio, cl);
	}
}

static int bch2_inode_flush_nocow_writes(struct bch_fs *c,
					 struct bch_inode_info *inode)
{
	struct closure cl;

	closure_init_stack(&cl);
	bch2_inode_flush_nocow_writes_async(c, inode, &cl);
	closure_sync(&cl);

	return 0;
}

static inline bool bio_full(struct bio *bio, unsigned len)
{
	if (bio->bi_vcnt >= bio->bi_max_vecs)
		return true;
	if (bio->bi_iter.bi_size > UINT_MAX - len)
		return true;
	return false;
}

static inline struct address_space *faults_disabled_mapping(void)
{
	return (void *) (((unsigned long) current->faults_disabled_mapping) & ~1UL);
}

static inline void set_fdm_dropped_locks(void)
{
	current->faults_disabled_mapping =
		(void *) (((unsigned long) current->faults_disabled_mapping)|1);
}

static inline bool fdm_dropped_locks(void)
{
	return ((unsigned long) current->faults_disabled_mapping) & 1;
}

struct quota_res {
	u64				sectors;
};

struct bch_writepage_io {
	struct bch_inode_info		*inode;

	/* must be last: */
	struct bch_write_op		op;
};

struct dio_write {
	struct kiocb			*req;
	struct address_space		*mapping;
	struct bch_inode_info		*inode;
	struct mm_struct		*mm;
	unsigned			loop:1,
					extending:1,
					sync:1,
					flush:1,
					free_iov:1;
	struct quota_res		quota_res;
	u64				written;

	struct iov_iter			iter;
	struct iovec			inline_vecs[2];

	/* must be last: */
	struct bch_write_op		op;
};

struct dio_read {
	struct closure			cl;
	struct kiocb			*req;
	long				ret;
	bool				should_dirty;
	struct bch_read_bio		rbio;
};

/* pagecache_block must be held */
static noinline int write_invalidate_inode_pages_range(struct address_space *mapping,
					      loff_t start, loff_t end)
{
	int ret;

	/*
	 * XXX: the way this is currently implemented, we can spin if a process
	 * is continually redirtying a specific page
	 */
	do {
		if (!mapping->nrpages)
			return 0;

		ret = filemap_write_and_wait_range(mapping, start, end);
		if (ret)
			break;

		if (!mapping->nrpages)
			return 0;

		ret = invalidate_inode_pages2_range(mapping,
				start >> PAGE_SHIFT,
				end >> PAGE_SHIFT);
	} while (ret == -EBUSY);

	return ret;
}

/* quotas */

#ifdef CONFIG_BCACHEFS_QUOTA

static void __bch2_quota_reservation_put(struct bch_fs *c,
					 struct bch_inode_info *inode,
					 struct quota_res *res)
{
	BUG_ON(res->sectors > inode->ei_quota_reserved);

	bch2_quota_acct(c, inode->ei_qid, Q_SPC,
			-((s64) res->sectors), KEY_TYPE_QUOTA_PREALLOC);
	inode->ei_quota_reserved -= res->sectors;
	res->sectors = 0;
}

static void bch2_quota_reservation_put(struct bch_fs *c,
				       struct bch_inode_info *inode,
				       struct quota_res *res)
{
	if (res->sectors) {
		mutex_lock(&inode->ei_quota_lock);
		__bch2_quota_reservation_put(c, inode, res);
		mutex_unlock(&inode->ei_quota_lock);
	}
}

static int bch2_quota_reservation_add(struct bch_fs *c,
				      struct bch_inode_info *inode,
				      struct quota_res *res,
				      u64 sectors,
				      bool check_enospc)
{
	int ret;

	if (test_bit(EI_INODE_SNAPSHOT, &inode->ei_flags))
		return 0;

	mutex_lock(&inode->ei_quota_lock);
	ret = bch2_quota_acct(c, inode->ei_qid, Q_SPC, sectors,
			      check_enospc ? KEY_TYPE_QUOTA_PREALLOC : KEY_TYPE_QUOTA_NOCHECK);
	if (likely(!ret)) {
		inode->ei_quota_reserved += sectors;
		res->sectors += sectors;
	}
	mutex_unlock(&inode->ei_quota_lock);

	return ret;
}

#else

static void __bch2_quota_reservation_put(struct bch_fs *c,
					 struct bch_inode_info *inode,
					 struct quota_res *res) {}

static void bch2_quota_reservation_put(struct bch_fs *c,
				       struct bch_inode_info *inode,
				       struct quota_res *res) {}

static int bch2_quota_reservation_add(struct bch_fs *c,
				      struct bch_inode_info *inode,
				      struct quota_res *res,
				      unsigned sectors,
				      bool check_enospc)
{
	return 0;
}

#endif

/* i_size updates: */

struct inode_new_size {
	loff_t		new_size;
	u64		now;
	unsigned	fields;
};

static int inode_set_size(struct bch_inode_info *inode,
			  struct bch_inode_unpacked *bi,
			  void *p)
{
	struct inode_new_size *s = p;

	bi->bi_size = s->new_size;
	if (s->fields & ATTR_ATIME)
		bi->bi_atime = s->now;
	if (s->fields & ATTR_MTIME)
		bi->bi_mtime = s->now;
	if (s->fields & ATTR_CTIME)
		bi->bi_ctime = s->now;

	return 0;
}

int __must_check bch2_write_inode_size(struct bch_fs *c,
				       struct bch_inode_info *inode,
				       loff_t new_size, unsigned fields)
{
	struct inode_new_size s = {
		.new_size	= new_size,
		.now		= bch2_current_time(c),
		.fields		= fields,
	};

	return bch2_write_inode(c, inode, inode_set_size, &s, fields);
}

static void __i_sectors_acct(struct bch_fs *c, struct bch_inode_info *inode,
			   struct quota_res *quota_res, s64 sectors)
{
	bch2_fs_inconsistent_on((s64) inode->v.i_blocks + sectors < 0, c,
				"inode %lu i_blocks underflow: %llu + %lli < 0 (ondisk %lli)",
				inode->v.i_ino, (u64) inode->v.i_blocks, sectors,
				inode->ei_inode.bi_sectors);
	inode->v.i_blocks += sectors;

#ifdef CONFIG_BCACHEFS_QUOTA
	if (quota_res &&
	    !test_bit(EI_INODE_SNAPSHOT, &inode->ei_flags) &&
	    sectors > 0) {
		BUG_ON(sectors > quota_res->sectors);
		BUG_ON(sectors > inode->ei_quota_reserved);

		quota_res->sectors -= sectors;
		inode->ei_quota_reserved -= sectors;
	} else {
		bch2_quota_acct(c, inode->ei_qid, Q_SPC, sectors, KEY_TYPE_QUOTA_WARN);
	}
#endif
}

static void i_sectors_acct(struct bch_fs *c, struct bch_inode_info *inode,
			   struct quota_res *quota_res, s64 sectors)
{
	if (sectors) {
		mutex_lock(&inode->ei_quota_lock);
		__i_sectors_acct(c, inode, quota_res, sectors);
		mutex_unlock(&inode->ei_quota_lock);
	}
}

/* page state: */

/* stored in page->private: */

#define BCH_FOLIO_SECTOR_STATE()	\
	x(unallocated)			\
	x(reserved)			\
	x(dirty)			\
	x(dirty_reserved)		\
	x(allocated)

enum bch_folio_sector_state {
#define x(n)	SECTOR_##n,
	BCH_FOLIO_SECTOR_STATE()
#undef x
};

const char * const bch2_folio_sector_states[] = {
#define x(n)	#n,
	BCH_FOLIO_SECTOR_STATE()
#undef x
	NULL
};

static inline enum bch_folio_sector_state
folio_sector_dirty(enum bch_folio_sector_state state)
{
	switch (state) {
	case SECTOR_unallocated:
		return SECTOR_dirty;
	case SECTOR_reserved:
		return SECTOR_dirty_reserved;
	default:
		return state;
	}
}

static inline enum bch_folio_sector_state
folio_sector_undirty(enum bch_folio_sector_state state)
{
	switch (state) {
	case SECTOR_dirty:
		return SECTOR_unallocated;
	case SECTOR_dirty_reserved:
		return SECTOR_reserved;
	default:
		return state;
	}
}

static inline enum bch_folio_sector_state
folio_sector_reserve(enum bch_folio_sector_state state)
{
	switch (state) {
	case SECTOR_unallocated:
		return SECTOR_reserved;
	case SECTOR_dirty:
		return SECTOR_dirty_reserved;
	default:
		return state;
	}
}

struct bch_folio_sector {
	/* Uncompressed, fully allocated replicas (or on disk reservation): */
	unsigned		nr_replicas:4;

	/* Owns PAGE_SECTORS * replicas_reserved sized in memory reservation: */
	unsigned		replicas_reserved:4;

	/* i_sectors: */
	enum bch_folio_sector_state state:8;
};

struct bch_folio {
	spinlock_t		lock;
	atomic_t		write_count;
	/*
	 * Is the sector state up to date with the btree?
	 * (Not the data itself)
	 */
	bool			uptodate;
	struct bch_folio_sector	s[];
};

static inline void folio_sector_set(struct folio *folio,
			     struct bch_folio *s,
			     unsigned i, unsigned n)
{
	s->s[i].state = n;
}

/* file offset (to folio offset) to bch_folio_sector index */
static inline int folio_pos_to_s(struct folio *folio, loff_t pos)
{
	u64 f_offset = pos - folio_pos(folio);
	BUG_ON(pos < folio_pos(folio) || pos >= folio_end_pos(folio));
	return f_offset >> SECTOR_SHIFT;
}

static inline struct bch_folio *__bch2_folio(struct folio *folio)
{
	return folio_has_private(folio)
		? (struct bch_folio *) folio_get_private(folio)
		: NULL;
}

static inline struct bch_folio *bch2_folio(struct folio *folio)
{
	EBUG_ON(!folio_test_locked(folio));

	return __bch2_folio(folio);
}

/* for newly allocated folios: */
static void __bch2_folio_release(struct folio *folio)
{
	kfree(folio_detach_private(folio));
}

static void bch2_folio_release(struct folio *folio)
{
	EBUG_ON(!folio_test_locked(folio));
	__bch2_folio_release(folio);
}

/* for newly allocated folios: */
static struct bch_folio *__bch2_folio_create(struct folio *folio, gfp_t gfp)
{
	struct bch_folio *s;

	s = kzalloc(sizeof(*s) +
		    sizeof(struct bch_folio_sector) *
		    folio_sectors(folio), gfp);
	if (!s)
		return NULL;

	spin_lock_init(&s->lock);
	folio_attach_private(folio, s);
	return s;
}

static struct bch_folio *bch2_folio_create(struct folio *folio, gfp_t gfp)
{
	return bch2_folio(folio) ?: __bch2_folio_create(folio, gfp);
}

static unsigned bkey_to_sector_state(struct bkey_s_c k)
{
	if (bkey_extent_is_reservation(k))
		return SECTOR_reserved;
	if (bkey_extent_is_allocation(k.k))
		return SECTOR_allocated;
	return SECTOR_unallocated;
}

static void __bch2_folio_set(struct folio *folio,
			     unsigned pg_offset, unsigned pg_len,
			     unsigned nr_ptrs, unsigned state)
{
	struct bch_folio *s = bch2_folio(folio);
	unsigned i, sectors = folio_sectors(folio);

	BUG_ON(pg_offset >= sectors);
	BUG_ON(pg_offset + pg_len > sectors);

	spin_lock(&s->lock);

	for (i = pg_offset; i < pg_offset + pg_len; i++) {
		s->s[i].nr_replicas	= nr_ptrs;
		folio_sector_set(folio, s, i, state);
	}

	if (i == sectors)
		s->uptodate = true;

	spin_unlock(&s->lock);
}

/*
 * Initialize bch_folio state (allocated/unallocated, nr_replicas) from the
 * extents btree:
 */
static int bch2_folio_set(struct bch_fs *c, subvol_inum inum,
			  struct folio **folios, unsigned nr_folios)
{
	struct btree_trans trans;
	struct btree_iter iter;
	struct bkey_s_c k;
	struct bch_folio *s;
	u64 offset = folio_sector(folios[0]);
	unsigned folio_idx;
	u32 snapshot;
	bool need_set = false;
	int ret;

	for (folio_idx = 0; folio_idx < nr_folios; folio_idx++) {
		s = bch2_folio_create(folios[folio_idx], GFP_KERNEL);
		if (!s)
			return -ENOMEM;

		need_set |= !s->uptodate;
	}

	if (!need_set)
		return 0;

	folio_idx = 0;
	bch2_trans_init(&trans, c, 0, 0);
retry:
	bch2_trans_begin(&trans);

	ret = bch2_subvolume_get_snapshot(&trans, inum.subvol, &snapshot);
	if (ret)
		goto err;

	for_each_btree_key_norestart(&trans, iter, BTREE_ID_extents,
			   SPOS(inum.inum, offset, snapshot),
			   BTREE_ITER_SLOTS, k, ret) {
		unsigned nr_ptrs = bch2_bkey_nr_ptrs_fully_allocated(k);
		unsigned state = bkey_to_sector_state(k);

		while (folio_idx < nr_folios) {
			struct folio *folio = folios[folio_idx];
			u64 folio_start	= folio_sector(folio);
			u64 folio_end	= folio_end_sector(folio);
			unsigned folio_offset = max(bkey_start_offset(k.k), folio_start) - folio_start;
			unsigned folio_len = min(k.k->p.offset, folio_end) - folio_offset - folio_start;

			BUG_ON(k.k->p.offset < folio_start);
			BUG_ON(bkey_start_offset(k.k) > folio_end);

			if (!bch2_folio(folio)->uptodate)
				__bch2_folio_set(folio, folio_offset, folio_len, nr_ptrs, state);

			if (k.k->p.offset < folio_end)
				break;
			folio_idx++;
		}

		if (folio_idx == nr_folios)
			break;
	}

	offset = iter.pos.offset;
	bch2_trans_iter_exit(&trans, &iter);
err:
	if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
		goto retry;
	bch2_trans_exit(&trans);

	return ret;
}

static void bch2_bio_page_state_set(struct bio *bio, struct bkey_s_c k)
{
	struct bvec_iter iter;
	struct folio_vec fv;
	unsigned nr_ptrs = k.k->type == KEY_TYPE_reflink_v
		? 0 : bch2_bkey_nr_ptrs_fully_allocated(k);
	unsigned state = bkey_to_sector_state(k);

	bio_for_each_folio(fv, bio, iter)
		__bch2_folio_set(fv.fv_folio,
				 fv.fv_offset >> 9,
				 fv.fv_len >> 9,
				 nr_ptrs, state);
}

static void mark_pagecache_unallocated(struct bch_inode_info *inode,
				       u64 start, u64 end)
{
	pgoff_t index = start >> PAGE_SECTORS_SHIFT;
	pgoff_t end_index = (end - 1) >> PAGE_SECTORS_SHIFT;
	struct folio_batch fbatch;
	unsigned i, j;

	if (end <= start)
		return;

	folio_batch_init(&fbatch);

	while (filemap_get_folios(inode->v.i_mapping,
				  &index, end_index, &fbatch)) {
		for (i = 0; i < folio_batch_count(&fbatch); i++) {
			struct folio *folio = fbatch.folios[i];
			u64 folio_start = folio_sector(folio);
			u64 folio_end = folio_end_sector(folio);
			unsigned folio_offset = max(start, folio_start) - folio_start;
			unsigned folio_len = min(end, folio_end) - folio_offset - folio_start;
			struct bch_folio *s;

			BUG_ON(end <= folio_start);

			folio_lock(folio);
			s = bch2_folio(folio);

			if (s) {
				spin_lock(&s->lock);
				for (j = folio_offset; j < folio_offset + folio_len; j++)
					s->s[j].nr_replicas = 0;
				spin_unlock(&s->lock);
			}

			folio_unlock(folio);
		}
		folio_batch_release(&fbatch);
		cond_resched();
	}
}

static void mark_pagecache_reserved(struct bch_inode_info *inode,
				    u64 start, u64 end)
{
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	pgoff_t index = start >> PAGE_SECTORS_SHIFT;
	pgoff_t end_index = (end - 1) >> PAGE_SECTORS_SHIFT;
	struct folio_batch fbatch;
	s64 i_sectors_delta = 0;
	unsigned i, j;

	if (end <= start)
		return;

	folio_batch_init(&fbatch);

	while (filemap_get_folios(inode->v.i_mapping,
				  &index, end_index, &fbatch)) {
		for (i = 0; i < folio_batch_count(&fbatch); i++) {
			struct folio *folio = fbatch.folios[i];
			u64 folio_start = folio_sector(folio);
			u64 folio_end = folio_end_sector(folio);
			unsigned folio_offset = max(start, folio_start) - folio_start;
			unsigned folio_len = min(end, folio_end) - folio_offset - folio_start;
			struct bch_folio *s;

			BUG_ON(end <= folio_start);

			folio_lock(folio);
			s = bch2_folio(folio);

			if (s) {
				spin_lock(&s->lock);
				for (j = folio_offset; j < folio_offset + folio_len; j++) {
					i_sectors_delta -= s->s[j].state == SECTOR_dirty;
					folio_sector_set(folio, s, j, folio_sector_reserve(s->s[j].state));
				}
				spin_unlock(&s->lock);
			}

			folio_unlock(folio);
		}
		folio_batch_release(&fbatch);
		cond_resched();
	}

	i_sectors_acct(c, inode, NULL, i_sectors_delta);
}

static inline unsigned inode_nr_replicas(struct bch_fs *c, struct bch_inode_info *inode)
{
	/* XXX: this should not be open coded */
	return inode->ei_inode.bi_data_replicas
		? inode->ei_inode.bi_data_replicas - 1
		: c->opts.data_replicas;
}

static inline unsigned sectors_to_reserve(struct bch_folio_sector *s,
					  unsigned nr_replicas)
{
	return max(0, (int) nr_replicas -
		   s->nr_replicas -
		   s->replicas_reserved);
}

static int bch2_get_folio_disk_reservation(struct bch_fs *c,
				struct bch_inode_info *inode,
				struct folio *folio, bool check_enospc)
{
	struct bch_folio *s = bch2_folio_create(folio, 0);
	unsigned nr_replicas = inode_nr_replicas(c, inode);
	struct disk_reservation disk_res = { 0 };
	unsigned i, sectors = folio_sectors(folio), disk_res_sectors = 0;
	int ret;

	if (!s)
		return -ENOMEM;

	for (i = 0; i < sectors; i++)
		disk_res_sectors += sectors_to_reserve(&s->s[i], nr_replicas);

	if (!disk_res_sectors)
		return 0;

	ret = bch2_disk_reservation_get(c, &disk_res,
					disk_res_sectors, 1,
					!check_enospc
					? BCH_DISK_RESERVATION_NOFAIL
					: 0);
	if (unlikely(ret))
		return ret;

	for (i = 0; i < sectors; i++)
		s->s[i].replicas_reserved +=
			sectors_to_reserve(&s->s[i], nr_replicas);

	return 0;
}

struct bch2_folio_reservation {
	struct disk_reservation	disk;
	struct quota_res	quota;
};

static void bch2_folio_reservation_init(struct bch_fs *c,
			struct bch_inode_info *inode,
			struct bch2_folio_reservation *res)
{
	memset(res, 0, sizeof(*res));

	res->disk.nr_replicas = inode_nr_replicas(c, inode);
}

static void bch2_folio_reservation_put(struct bch_fs *c,
			struct bch_inode_info *inode,
			struct bch2_folio_reservation *res)
{
	bch2_disk_reservation_put(c, &res->disk);
	bch2_quota_reservation_put(c, inode, &res->quota);
}

static int bch2_folio_reservation_get(struct bch_fs *c,
			struct bch_inode_info *inode,
			struct folio *folio,
			struct bch2_folio_reservation *res,
			unsigned offset, unsigned len)
{
	struct bch_folio *s = bch2_folio_create(folio, 0);
	unsigned i, disk_sectors = 0, quota_sectors = 0;
	int ret;

	if (!s)
		return -ENOMEM;

	BUG_ON(!s->uptodate);

	for (i = round_down(offset, block_bytes(c)) >> 9;
	     i < round_up(offset + len, block_bytes(c)) >> 9;
	     i++) {
		disk_sectors += sectors_to_reserve(&s->s[i],
						res->disk.nr_replicas);
		quota_sectors += s->s[i].state == SECTOR_unallocated;
	}

	if (disk_sectors) {
		ret = bch2_disk_reservation_add(c, &res->disk, disk_sectors, 0);
		if (unlikely(ret))
			return ret;
	}

	if (quota_sectors) {
		ret = bch2_quota_reservation_add(c, inode, &res->quota,
						 quota_sectors, true);
		if (unlikely(ret)) {
			struct disk_reservation tmp = {
				.sectors = disk_sectors
			};

			bch2_disk_reservation_put(c, &tmp);
			res->disk.sectors -= disk_sectors;
			return ret;
		}
	}

	return 0;
}

static void bch2_clear_folio_bits(struct folio *folio)
{
	struct bch_inode_info *inode = to_bch_ei(folio->mapping->host);
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	struct bch_folio *s = bch2_folio(folio);
	struct disk_reservation disk_res = { 0 };
	int i, sectors = folio_sectors(folio), dirty_sectors = 0;

	if (!s)
		return;

	EBUG_ON(!folio_test_locked(folio));
	EBUG_ON(folio_test_writeback(folio));

	for (i = 0; i < sectors; i++) {
		disk_res.sectors += s->s[i].replicas_reserved;
		s->s[i].replicas_reserved = 0;

		dirty_sectors -= s->s[i].state == SECTOR_dirty;
		folio_sector_set(folio, s, i, folio_sector_undirty(s->s[i].state));
	}

	bch2_disk_reservation_put(c, &disk_res);

	i_sectors_acct(c, inode, NULL, dirty_sectors);

	bch2_folio_release(folio);
}

static void bch2_set_folio_dirty(struct bch_fs *c,
			struct bch_inode_info *inode,
			struct folio *folio,
			struct bch2_folio_reservation *res,
			unsigned offset, unsigned len)
{
	struct bch_folio *s = bch2_folio(folio);
	unsigned i, dirty_sectors = 0;

	WARN_ON((u64) folio_pos(folio) + offset + len >
		round_up((u64) i_size_read(&inode->v), block_bytes(c)));

	BUG_ON(!s->uptodate);

	spin_lock(&s->lock);

	for (i = round_down(offset, block_bytes(c)) >> 9;
	     i < round_up(offset + len, block_bytes(c)) >> 9;
	     i++) {
		unsigned sectors = sectors_to_reserve(&s->s[i],
						res->disk.nr_replicas);

		/*
		 * This can happen if we race with the error path in
		 * bch2_writepage_io_done():
		 */
		sectors = min_t(unsigned, sectors, res->disk.sectors);

		s->s[i].replicas_reserved += sectors;
		res->disk.sectors -= sectors;

		dirty_sectors += s->s[i].state == SECTOR_unallocated;

		folio_sector_set(folio, s, i, folio_sector_dirty(s->s[i].state));
	}

	spin_unlock(&s->lock);

	i_sectors_acct(c, inode, &res->quota, dirty_sectors);

	if (!folio_test_dirty(folio))
		filemap_dirty_folio(inode->v.i_mapping, folio);
}

vm_fault_t bch2_page_fault(struct vm_fault *vmf)
{
	struct file *file = vmf->vma->vm_file;
	struct address_space *mapping = file->f_mapping;
	struct address_space *fdm = faults_disabled_mapping();
	struct bch_inode_info *inode = file_bch_inode(file);
	int ret;

	if (fdm == mapping)
		return VM_FAULT_SIGBUS;

	/* Lock ordering: */
	if (fdm > mapping) {
		struct bch_inode_info *fdm_host = to_bch_ei(fdm->host);

		if (bch2_pagecache_add_tryget(inode))
			goto got_lock;

		bch2_pagecache_block_put(fdm_host);

		bch2_pagecache_add_get(inode);
		bch2_pagecache_add_put(inode);

		bch2_pagecache_block_get(fdm_host);

		/* Signal that lock has been dropped: */
		set_fdm_dropped_locks();
		return VM_FAULT_SIGBUS;
	}

	bch2_pagecache_add_get(inode);
got_lock:
	ret = filemap_fault(vmf);
	bch2_pagecache_add_put(inode);

	return ret;
}

vm_fault_t bch2_page_mkwrite(struct vm_fault *vmf)
{
	struct folio *folio = page_folio(vmf->page);
	struct file *file = vmf->vma->vm_file;
	struct bch_inode_info *inode = file_bch_inode(file);
	struct address_space *mapping = file->f_mapping;
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	struct bch2_folio_reservation res;
	unsigned len;
	loff_t isize;
	int ret;

	bch2_folio_reservation_init(c, inode, &res);

	sb_start_pagefault(inode->v.i_sb);
	file_update_time(file);

	/*
	 * Not strictly necessary, but helps avoid dio writes livelocking in
	 * write_invalidate_inode_pages_range() - can drop this if/when we get
	 * a write_invalidate_inode_pages_range() that works without dropping
	 * page lock before invalidating page
	 */
	bch2_pagecache_add_get(inode);

	folio_lock(folio);
	isize = i_size_read(&inode->v);

	if (folio->mapping != mapping || folio_pos(folio) >= isize) {
		folio_unlock(folio);
		ret = VM_FAULT_NOPAGE;
		goto out;
	}

	len = min_t(loff_t, folio_size(folio), isize - folio_pos(folio));

	if (bch2_folio_set(c, inode_inum(inode), &folio, 1) ?:
	    bch2_folio_reservation_get(c, inode, folio, &res, 0, len)) {
		folio_unlock(folio);
		ret = VM_FAULT_SIGBUS;
		goto out;
	}

	bch2_set_folio_dirty(c, inode, folio, &res, 0, len);
	bch2_folio_reservation_put(c, inode, &res);

	folio_wait_stable(folio);
	ret = VM_FAULT_LOCKED;
out:
	bch2_pagecache_add_put(inode);
	sb_end_pagefault(inode->v.i_sb);

	return ret;
}

void bch2_invalidate_folio(struct folio *folio, size_t offset, size_t length)
{
	if (offset || length < folio_size(folio))
		return;

	bch2_clear_folio_bits(folio);
}

bool bch2_release_folio(struct folio *folio, gfp_t gfp_mask)
{
	if (folio_test_dirty(folio) || folio_test_writeback(folio))
		return false;

	bch2_clear_folio_bits(folio);
	return true;
}

/* readpage(s): */

static void bch2_readpages_end_io(struct bio *bio)
{
	struct folio_iter fi;

	bio_for_each_folio_all(fi, bio) {
		if (!bio->bi_status) {
			folio_mark_uptodate(fi.folio);
		} else {
			folio_clear_uptodate(fi.folio);
			folio_set_error(fi.folio);
		}
		folio_unlock(fi.folio);
	}

	bio_put(bio);
}

struct readpages_iter {
	struct address_space	*mapping;
	unsigned		idx;
	folios			folios;
};

static int readpages_iter_init(struct readpages_iter *iter,
			       struct readahead_control *ractl)
{
	struct folio **fi;
	int ret;

	memset(iter, 0, sizeof(*iter));

	iter->mapping = ractl->mapping;

	ret = filemap_get_contig_folios_d(iter->mapping,
				ractl->_index << PAGE_SHIFT,
				(ractl->_index + ractl->_nr_pages) << PAGE_SHIFT,
				0, mapping_gfp_mask(iter->mapping),
				&iter->folios);
	if (ret)
		return ret;

	darray_for_each(iter->folios, fi) {
		ractl->_nr_pages -= 1U << folio_order(*fi);
		__bch2_folio_create(*fi, __GFP_NOFAIL|GFP_KERNEL);
		folio_put(*fi);
		folio_put(*fi);
	}

	return 0;
}

static inline struct folio *readpage_iter_peek(struct readpages_iter *iter)
{
	if (iter->idx >= iter->folios.nr)
		return NULL;
	return iter->folios.data[iter->idx];
}

static inline void readpage_iter_advance(struct readpages_iter *iter)
{
	iter->idx++;
}

static bool extent_partial_reads_expensive(struct bkey_s_c k)
{
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	struct bch_extent_crc_unpacked crc;
	const union bch_extent_entry *i;

	bkey_for_each_crc(k.k, ptrs, crc, i)
		if (crc.csum_type || crc.compression_type)
			return true;
	return false;
}

static int readpage_bio_extend(struct btree_trans *trans,
			       struct readpages_iter *iter,
			       struct bio *bio,
			       unsigned sectors_this_extent,
			       bool get_more)
{
	/* Don't hold btree locks while allocating memory: */
	bch2_trans_unlock(trans);

	while (bio_sectors(bio) < sectors_this_extent &&
	       bio->bi_vcnt < bio->bi_max_vecs) {
		struct folio *folio = readpage_iter_peek(iter);
		int ret;

		if (folio) {
			readpage_iter_advance(iter);
		} else {
			pgoff_t folio_offset = bio_end_sector(bio) >> PAGE_SECTORS_SHIFT;

			if (!get_more)
				break;

			folio = xa_load(&iter->mapping->i_pages, folio_offset);
			if (folio && !xa_is_value(folio))
				break;

			folio = filemap_alloc_folio(readahead_gfp_mask(iter->mapping), 0);
			if (!folio)
				break;

			if (!__bch2_folio_create(folio, GFP_KERNEL)) {
				folio_put(folio);
				break;
			}

			ret = filemap_add_folio(iter->mapping, folio, folio_offset, GFP_KERNEL);
			if (ret) {
				__bch2_folio_release(folio);
				folio_put(folio);
				break;
			}

			folio_put(folio);
		}

		BUG_ON(folio_sector(folio) != bio_end_sector(bio));

		BUG_ON(!bio_add_folio(bio, folio, folio_size(folio), 0));
	}

	return bch2_trans_relock(trans);
}

static void bchfs_read(struct btree_trans *trans,
		       struct bch_read_bio *rbio,
		       subvol_inum inum,
		       struct readpages_iter *readpages_iter)
{
	struct bch_fs *c = trans->c;
	struct btree_iter iter;
	struct bkey_buf sk;
	int flags = BCH_READ_RETRY_IF_STALE|
		BCH_READ_MAY_PROMOTE;
	u32 snapshot;
	int ret = 0;

	rbio->c = c;
	rbio->start_time = local_clock();
	rbio->subvol = inum.subvol;

	bch2_bkey_buf_init(&sk);
retry:
	bch2_trans_begin(trans);
	iter = (struct btree_iter) { NULL };

	ret = bch2_subvolume_get_snapshot(trans, inum.subvol, &snapshot);
	if (ret)
		goto err;

	bch2_trans_iter_init(trans, &iter, BTREE_ID_extents,
			     SPOS(inum.inum, rbio->bio.bi_iter.bi_sector, snapshot),
			     BTREE_ITER_SLOTS);
	while (1) {
		struct bkey_s_c k;
		unsigned bytes, sectors, offset_into_extent;
		enum btree_id data_btree = BTREE_ID_extents;

		/*
		 * read_extent -> io_time_reset may cause a transaction restart
		 * without returning an error, we need to check for that here:
		 */
		ret = bch2_trans_relock(trans);
		if (ret)
			break;

		bch2_btree_iter_set_pos(&iter,
				POS(inum.inum, rbio->bio.bi_iter.bi_sector));

		k = bch2_btree_iter_peek_slot(&iter);
		ret = bkey_err(k);
		if (ret)
			break;

		offset_into_extent = iter.pos.offset -
			bkey_start_offset(k.k);
		sectors = k.k->size - offset_into_extent;

		bch2_bkey_buf_reassemble(&sk, c, k);

		ret = bch2_read_indirect_extent(trans, &data_btree,
					&offset_into_extent, &sk);
		if (ret)
			break;

		k = bkey_i_to_s_c(sk.k);

		sectors = min(sectors, k.k->size - offset_into_extent);

		if (readpages_iter) {
			ret = readpage_bio_extend(trans, readpages_iter, &rbio->bio, sectors,
						  extent_partial_reads_expensive(k));
			if (ret)
				break;
		}

		bytes = min(sectors, bio_sectors(&rbio->bio)) << 9;
		swap(rbio->bio.bi_iter.bi_size, bytes);

		if (rbio->bio.bi_iter.bi_size == bytes)
			flags |= BCH_READ_LAST_FRAGMENT;

		bch2_bio_page_state_set(&rbio->bio, k);

		bch2_read_extent(trans, rbio, iter.pos,
				 data_btree, k, offset_into_extent, flags);

		if (flags & BCH_READ_LAST_FRAGMENT)
			break;

		swap(rbio->bio.bi_iter.bi_size, bytes);
		bio_advance(&rbio->bio, bytes);

		ret = btree_trans_too_many_iters(trans);
		if (ret)
			break;
	}
err:
	bch2_trans_iter_exit(trans, &iter);

	if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
		goto retry;

	if (ret) {
		bch_err_inum_offset_ratelimited(c,
				iter.pos.inode,
				iter.pos.offset << 9,
				"read error %i from btree lookup", ret);
		rbio->bio.bi_status = BLK_STS_IOERR;
		bio_endio(&rbio->bio);
	}

	bch2_bkey_buf_exit(&sk, c);
}

void bch2_readahead(struct readahead_control *ractl)
{
	struct bch_inode_info *inode = to_bch_ei(ractl->mapping->host);
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	struct bch_io_opts opts;
	struct btree_trans trans;
	struct folio *folio;
	struct readpages_iter readpages_iter;
	int ret;

	bch2_inode_opts_get(&opts, c, &inode->ei_inode);

	ret = readpages_iter_init(&readpages_iter, ractl);
	BUG_ON(ret);

	bch2_trans_init(&trans, c, 0, 0);

	bch2_pagecache_add_get(inode);

	while ((folio = readpage_iter_peek(&readpages_iter))) {
		unsigned n = min_t(unsigned,
				   readpages_iter.folios.nr -
				   readpages_iter.idx,
				   BIO_MAX_VECS);
		struct bch_read_bio *rbio =
			rbio_init(bio_alloc_bioset(NULL, n, REQ_OP_READ,
						   GFP_KERNEL, &c->bio_read),
				  opts);

		readpage_iter_advance(&readpages_iter);

		rbio->bio.bi_iter.bi_sector = folio_sector(folio);
		rbio->bio.bi_end_io = bch2_readpages_end_io;
		BUG_ON(!bio_add_folio(&rbio->bio, folio, folio_size(folio), 0));

		bchfs_read(&trans, rbio, inode_inum(inode),
			   &readpages_iter);
		bch2_trans_unlock(&trans);
	}

	bch2_pagecache_add_put(inode);

	bch2_trans_exit(&trans);
	darray_exit(&readpages_iter.folios);
}

static void __bchfs_readfolio(struct bch_fs *c, struct bch_read_bio *rbio,
			     subvol_inum inum, struct folio *folio)
{
	struct btree_trans trans;

	bch2_folio_create(folio, __GFP_NOFAIL);

	rbio->bio.bi_opf = REQ_OP_READ|REQ_SYNC;
	rbio->bio.bi_iter.bi_sector = folio_sector(folio);
	BUG_ON(!bio_add_folio(&rbio->bio, folio, folio_size(folio), 0));

	bch2_trans_init(&trans, c, 0, 0);
	bchfs_read(&trans, rbio, inum, NULL);
	bch2_trans_exit(&trans);
}

static void bch2_read_single_folio_end_io(struct bio *bio)
{
	complete(bio->bi_private);
}

static int bch2_read_single_folio(struct folio *folio,
				  struct address_space *mapping)
{
	struct bch_inode_info *inode = to_bch_ei(mapping->host);
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	struct bch_read_bio *rbio;
	struct bch_io_opts opts;
	int ret;
	DECLARE_COMPLETION_ONSTACK(done);

	bch2_inode_opts_get(&opts, c, &inode->ei_inode);

	rbio = rbio_init(bio_alloc_bioset(NULL, 1, REQ_OP_READ, GFP_KERNEL, &c->bio_read),
			 opts);
	rbio->bio.bi_private = &done;
	rbio->bio.bi_end_io = bch2_read_single_folio_end_io;

	__bchfs_readfolio(c, rbio, inode_inum(inode), folio);
	wait_for_completion(&done);

	ret = blk_status_to_errno(rbio->bio.bi_status);
	bio_put(&rbio->bio);

	if (ret < 0)
		return ret;

	folio_mark_uptodate(folio);
	return 0;
}

int bch2_read_folio(struct file *file, struct folio *folio)
{
	int ret;

	ret = bch2_read_single_folio(folio, folio->mapping);
	folio_unlock(folio);
	return bch2_err_class(ret);
}

/* writepages: */

struct bch_writepage_state {
	struct bch_writepage_io	*io;
	struct bch_io_opts	opts;
	struct bch_folio_sector	*tmp;
	unsigned		tmp_sectors;
};

static inline struct bch_writepage_state bch_writepage_state_init(struct bch_fs *c,
								  struct bch_inode_info *inode)
{
	struct bch_writepage_state ret = { 0 };

	bch2_inode_opts_get(&ret.opts, c, &inode->ei_inode);
	return ret;
}

static void bch2_writepage_io_done(struct bch_write_op *op)
{
	struct bch_writepage_io *io =
		container_of(op, struct bch_writepage_io, op);
	struct bch_fs *c = io->op.c;
	struct bio *bio = &io->op.wbio.bio;
	struct folio_iter fi;
	unsigned i;

	if (io->op.error) {
		set_bit(EI_INODE_ERROR, &io->inode->ei_flags);

		bio_for_each_folio_all(fi, bio) {
			struct bch_folio *s;

			folio_set_error(fi.folio);
			mapping_set_error(fi.folio->mapping, -EIO);

			s = __bch2_folio(fi.folio);
			spin_lock(&s->lock);
			for (i = 0; i < folio_sectors(fi.folio); i++)
				s->s[i].nr_replicas = 0;
			spin_unlock(&s->lock);
		}
	}

	if (io->op.flags & BCH_WRITE_WROTE_DATA_INLINE) {
		bio_for_each_folio_all(fi, bio) {
			struct bch_folio *s;

			s = __bch2_folio(fi.folio);
			spin_lock(&s->lock);
			for (i = 0; i < folio_sectors(fi.folio); i++)
				s->s[i].nr_replicas = 0;
			spin_unlock(&s->lock);
		}
	}

	/*
	 * racing with fallocate can cause us to add fewer sectors than
	 * expected - but we shouldn't add more sectors than expected:
	 */
	WARN_ON_ONCE(io->op.i_sectors_delta > 0);

	/*
	 * (error (due to going RO) halfway through a page can screw that up
	 * slightly)
	 * XXX wtf?
	   BUG_ON(io->op.op.i_sectors_delta >= PAGE_SECTORS);
	 */

	/*
	 * PageWriteback is effectively our ref on the inode - fixup i_blocks
	 * before calling end_page_writeback:
	 */
	i_sectors_acct(c, io->inode, NULL, io->op.i_sectors_delta);

	bio_for_each_folio_all(fi, bio) {
		struct bch_folio *s = __bch2_folio(fi.folio);

		if (atomic_dec_and_test(&s->write_count))
			folio_end_writeback(fi.folio);
	}

	bio_put(&io->op.wbio.bio);
}

static void bch2_writepage_do_io(struct bch_writepage_state *w)
{
	struct bch_writepage_io *io = w->io;

	w->io = NULL;
	closure_call(&io->op.cl, bch2_write, NULL, NULL);
}

/*
 * Get a bch_writepage_io and add @page to it - appending to an existing one if
 * possible, else allocating a new one:
 */
static void bch2_writepage_io_alloc(struct bch_fs *c,
				    struct writeback_control *wbc,
				    struct bch_writepage_state *w,
				    struct bch_inode_info *inode,
				    u64 sector,
				    unsigned nr_replicas)
{
	struct bch_write_op *op;

	w->io = container_of(bio_alloc_bioset(NULL, BIO_MAX_VECS,
					      REQ_OP_WRITE,
					      GFP_KERNEL,
					      &c->writepage_bioset),
			     struct bch_writepage_io, op.wbio.bio);

	w->io->inode		= inode;
	op			= &w->io->op;
	bch2_write_op_init(op, c, w->opts);
	op->target		= w->opts.foreground_target;
	op->nr_replicas		= nr_replicas;
	op->res.nr_replicas	= nr_replicas;
	op->write_point		= writepoint_hashed(inode->ei_last_dirtied);
	op->subvol		= inode->ei_subvol;
	op->pos			= POS(inode->v.i_ino, sector);
	op->end_io		= bch2_writepage_io_done;
	op->devs_need_flush	= &inode->ei_devs_need_flush;
	op->wbio.bio.bi_iter.bi_sector = sector;
	op->wbio.bio.bi_opf	= wbc_to_write_flags(wbc);
}

static int __bch2_writepage(struct folio *folio,
			    struct writeback_control *wbc,
			    void *data)
{
	struct bch_inode_info *inode = to_bch_ei(folio->mapping->host);
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	struct bch_writepage_state *w = data;
	struct bch_folio *s;
	unsigned i, offset, f_sectors, nr_replicas_this_write = U32_MAX;
	loff_t i_size = i_size_read(&inode->v);
	int ret;

	EBUG_ON(!folio_test_uptodate(folio));

	/* Is the folio fully inside i_size? */
	if (folio_end_pos(folio) <= i_size)
		goto do_io;

	/* Is the folio fully outside i_size? (truncate in progress) */
	if (folio_pos(folio) >= i_size) {
		folio_unlock(folio);
		return 0;
	}

	/*
	 * The folio straddles i_size.  It must be zeroed out on each and every
	 * writepage invocation because it may be mmapped.  "A file is mapped
	 * in multiples of the folio size.  For a file that is not a multiple of
	 * the  folio size, the remaining memory is zeroed when mapped, and
	 * writes to that region are not written out to the file."
	 */
	folio_zero_segment(folio,
			   i_size - folio_pos(folio),
			   folio_size(folio));
do_io:
	f_sectors = folio_sectors(folio);
	s = bch2_folio(folio);

	if (f_sectors > w->tmp_sectors) {
		kfree(w->tmp);
		w->tmp = kzalloc(sizeof(struct bch_folio_sector) *
				 f_sectors, __GFP_NOFAIL);
		w->tmp_sectors = f_sectors;
	}

	/*
	 * Things get really hairy with errors during writeback:
	 */
	ret = bch2_get_folio_disk_reservation(c, inode, folio, false);
	BUG_ON(ret);

	/* Before unlocking the page, get copy of reservations: */
	spin_lock(&s->lock);
	memcpy(w->tmp, s->s, sizeof(struct bch_folio_sector) * f_sectors);

	for (i = 0; i < f_sectors; i++) {
		if (s->s[i].state < SECTOR_dirty)
			continue;

		nr_replicas_this_write =
			min_t(unsigned, nr_replicas_this_write,
			      s->s[i].nr_replicas +
			      s->s[i].replicas_reserved);
	}

	for (i = 0; i < f_sectors; i++) {
		if (s->s[i].state < SECTOR_dirty)
			continue;

		s->s[i].nr_replicas = w->opts.compression
			? 0 : nr_replicas_this_write;

		s->s[i].replicas_reserved = 0;
		folio_sector_set(folio, s, i, SECTOR_allocated);
	}
	spin_unlock(&s->lock);

	BUG_ON(atomic_read(&s->write_count));
	atomic_set(&s->write_count, 1);

	BUG_ON(folio_test_writeback(folio));
	folio_start_writeback(folio);

	folio_unlock(folio);

	offset = 0;
	while (1) {
		unsigned sectors = 0, dirty_sectors = 0, reserved_sectors = 0;
		u64 sector;

		while (offset < f_sectors &&
		       w->tmp[offset].state < SECTOR_dirty)
			offset++;

		if (offset == f_sectors)
			break;

		while (offset + sectors < f_sectors &&
		       w->tmp[offset + sectors].state >= SECTOR_dirty) {
			reserved_sectors += w->tmp[offset + sectors].replicas_reserved;
			dirty_sectors += w->tmp[offset + sectors].state == SECTOR_dirty;
			sectors++;
		}
		BUG_ON(!sectors);

		sector = folio_sector(folio) + offset;

		if (w->io &&
		    (w->io->op.res.nr_replicas != nr_replicas_this_write ||
		     bio_full(&w->io->op.wbio.bio, sectors << 9) ||
		     w->io->op.wbio.bio.bi_iter.bi_size + (sectors << 9) >=
		     (BIO_MAX_VECS * PAGE_SIZE) ||
		     bio_end_sector(&w->io->op.wbio.bio) != sector))
			bch2_writepage_do_io(w);

		if (!w->io)
			bch2_writepage_io_alloc(c, wbc, w, inode, sector,
						nr_replicas_this_write);

		atomic_inc(&s->write_count);

		BUG_ON(inode != w->io->inode);
		BUG_ON(!bio_add_folio(&w->io->op.wbio.bio, folio,
				     sectors << 9, offset << 9));

		/* Check for writing past i_size: */
		WARN_ONCE((bio_end_sector(&w->io->op.wbio.bio) << 9) >
			  round_up(i_size, block_bytes(c)) &&
			  !test_bit(BCH_FS_EMERGENCY_RO, &c->flags),
			  "writing past i_size: %llu > %llu (unrounded %llu)\n",
			  bio_end_sector(&w->io->op.wbio.bio) << 9,
			  round_up(i_size, block_bytes(c)),
			  i_size);

		w->io->op.res.sectors += reserved_sectors;
		w->io->op.i_sectors_delta -= dirty_sectors;
		w->io->op.new_i_size = i_size;

		offset += sectors;
	}

	if (atomic_dec_and_test(&s->write_count))
		folio_end_writeback(folio);

	return 0;
}

int bch2_writepages(struct address_space *mapping, struct writeback_control *wbc)
{
	struct bch_fs *c = mapping->host->i_sb->s_fs_info;
	struct bch_writepage_state w =
		bch_writepage_state_init(c, to_bch_ei(mapping->host));
	struct blk_plug plug;
	int ret;

	blk_start_plug(&plug);
	ret = write_cache_pages(mapping, wbc, __bch2_writepage, &w);
	if (w.io)
		bch2_writepage_do_io(&w);
	blk_finish_plug(&plug);
	kfree(w.tmp);
	return bch2_err_class(ret);
}

/* buffered writes: */

int bch2_write_begin(struct file *file, struct address_space *mapping,
		     loff_t pos, unsigned len,
		     struct page **pagep, void **fsdata)
{
	struct bch_inode_info *inode = to_bch_ei(mapping->host);
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	struct bch2_folio_reservation *res;
	struct folio *folio;
	unsigned offset;
	int ret = -ENOMEM;

	res = kmalloc(sizeof(*res), GFP_KERNEL);
	if (!res)
		return -ENOMEM;

	bch2_folio_reservation_init(c, inode, res);
	*fsdata = res;

	bch2_pagecache_add_get(inode);

	folio = __filemap_get_folio(mapping, pos >> PAGE_SHIFT,
				FGP_LOCK|FGP_WRITE|FGP_CREAT|FGP_STABLE,
				mapping_gfp_mask(mapping));
	if (!folio)
		goto err_unlock;

	if (folio_test_uptodate(folio))
		goto out;

	offset = pos - folio_pos(folio);
	len = min_t(size_t, len, folio_end_pos(folio) - pos);

	/* If we're writing entire folio, don't need to read it in first: */
	if (!offset && len == folio_size(folio))
		goto out;

	if (!offset && pos + len >= inode->v.i_size) {
		folio_zero_segment(folio, len, folio_size(folio));
		flush_dcache_folio(folio);
		goto out;
	}

	if (folio_pos(folio) >= inode->v.i_size) {
		folio_zero_segments(folio, 0, offset, offset + len, folio_size(folio));
		flush_dcache_folio(folio);
		goto out;
	}
readpage:
	ret = bch2_read_single_folio(folio, mapping);
	if (ret)
		goto err;
out:
	ret = bch2_folio_set(c, inode_inum(inode), &folio, 1);
	if (ret)
		goto err;

	ret = bch2_folio_reservation_get(c, inode, folio, res, offset, len);
	if (ret) {
		if (!folio_test_uptodate(folio)) {
			/*
			 * If the folio hasn't been read in, we won't know if we
			 * actually need a reservation - we don't actually need
			 * to read here, we just need to check if the folio is
			 * fully backed by uncompressed data:
			 */
			goto readpage;
		}

		goto err;
	}

	*pagep = &folio->page;
	return 0;
err:
	folio_unlock(folio);
	folio_put(folio);
	*pagep = NULL;
err_unlock:
	bch2_pagecache_add_put(inode);
	kfree(res);
	*fsdata = NULL;
	return bch2_err_class(ret);
}

int bch2_write_end(struct file *file, struct address_space *mapping,
		   loff_t pos, unsigned len, unsigned copied,
		   struct page *page, void *fsdata)
{
	struct bch_inode_info *inode = to_bch_ei(mapping->host);
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	struct bch2_folio_reservation *res = fsdata;
	struct folio *folio = page_folio(page);
	unsigned offset = pos - folio_pos(folio);

	lockdep_assert_held(&inode->v.i_rwsem);
	BUG_ON(offset + copied > folio_size(folio));

	if (unlikely(copied < len && !folio_test_uptodate(folio))) {
		/*
		 * The folio needs to be read in, but that would destroy
		 * our partial write - simplest thing is to just force
		 * userspace to redo the write:
		 */
		folio_zero_range(folio, 0, folio_size(folio));
		flush_dcache_folio(folio);
		copied = 0;
	}

	spin_lock(&inode->v.i_lock);
	if (pos + copied > inode->v.i_size)
		i_size_write(&inode->v, pos + copied);
	spin_unlock(&inode->v.i_lock);

	if (copied) {
		if (!folio_test_uptodate(folio))
			folio_mark_uptodate(folio);

		bch2_set_folio_dirty(c, inode, folio, res, offset, copied);

		inode->ei_last_dirtied = (unsigned long) current;
	}

	folio_unlock(folio);
	folio_put(folio);
	bch2_pagecache_add_put(inode);

	bch2_folio_reservation_put(c, inode, res);
	kfree(res);

	return copied;
}

static noinline void folios_trunc(folios *folios, struct folio **fi)
{
	while (folios->data + folios->nr > fi) {
		struct folio *f = darray_pop(folios);

		folio_unlock(f);
		folio_put(f);
	}
}

static int __bch2_buffered_write(struct bch_inode_info *inode,
				 struct address_space *mapping,
				 struct iov_iter *iter,
				 loff_t pos, unsigned len)
{
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	struct bch2_folio_reservation res;
	folios folios;
	struct folio **fi, *f;
	unsigned copied = 0, f_offset;
	u64 end = pos + len, f_pos;
	loff_t last_folio_pos = inode->v.i_size;
	int ret = 0;

	BUG_ON(!len);

	bch2_folio_reservation_init(c, inode, &res);
	darray_init(&folios);

	ret = filemap_get_contig_folios_d(mapping, pos, end,
				   FGP_LOCK|FGP_WRITE|FGP_STABLE|FGP_CREAT,
				   mapping_gfp_mask(mapping),
				   &folios);
	if (ret)
		goto out;

	BUG_ON(!folios.nr);

	f = darray_first(folios);
	if (pos != folio_pos(f) && !folio_test_uptodate(f)) {
		ret = bch2_read_single_folio(f, mapping);
		if (ret)
			goto out;
	}

	f = darray_last(folios);
	end = min(end, folio_end_pos(f));
	last_folio_pos = folio_pos(f);
	if (end != folio_end_pos(f) && !folio_test_uptodate(f)) {
		if (end >= inode->v.i_size) {
			folio_zero_range(f, 0, folio_size(f));
		} else {
			ret = bch2_read_single_folio(f, mapping);
			if (ret)
				goto out;
		}
	}

	ret = bch2_folio_set(c, inode_inum(inode), folios.data, folios.nr);
	if (ret)
		goto out;

	f_pos = pos;
	f_offset = pos - folio_pos(darray_first(folios));
	darray_for_each(folios, fi) {
		struct folio *f = *fi;
		u64 f_len = min(end, folio_end_pos(f)) - f_pos;

		/*
		 * XXX: per POSIX and fstests generic/275, on -ENOSPC we're
		 * supposed to write as much as we have disk space for.
		 *
		 * On failure here we should still write out a partial page if
		 * we aren't completely out of disk space - we don't do that
		 * yet:
		 */
		ret = bch2_folio_reservation_get(c, inode, f, &res, f_offset, f_len);
		if (unlikely(ret)) {
			folios_trunc(&folios, fi);
			if (!folios.nr)
				goto out;

			end = min(end, folio_end_pos(darray_last(folios)));
			break;
		}

		f_pos = folio_end_pos(f);
		f_offset = 0;
	}

	if (mapping_writably_mapped(mapping))
		darray_for_each(folios, fi)
			flush_dcache_folio(*fi);

	f_pos = pos;
	f_offset = pos - folio_pos(darray_first(folios));
	darray_for_each(folios, fi) {
		struct folio *f = *fi;
		u64 f_len = min(end, folio_end_pos(f)) - f_pos;
		unsigned f_copied = copy_page_from_iter_atomic(&f->page, f_offset, f_len, iter);

		if (!f_copied) {
			folios_trunc(&folios, fi);
			break;
		}

		if (!folio_test_uptodate(f) &&
		    f_copied != folio_size(f) &&
		    pos + copied + f_copied < inode->v.i_size) {
			folio_zero_range(f, 0, folio_size(f));
			folios_trunc(&folios, fi);
			break;
		}

		flush_dcache_folio(f);
		copied += f_copied;

		if (f_copied != f_len) {
			folios_trunc(&folios, fi + 1);
			break;
		}

		f_pos = folio_end_pos(f);
		f_offset = 0;
	}

	if (!copied)
		goto out;

	end = pos + copied;

	spin_lock(&inode->v.i_lock);
	if (end > inode->v.i_size)
		i_size_write(&inode->v, end);
	spin_unlock(&inode->v.i_lock);

	f_pos = pos;
	f_offset = pos - folio_pos(darray_first(folios));
	darray_for_each(folios, fi) {
		struct folio *f = *fi;
		u64 f_len = min(end, folio_end_pos(f)) - f_pos;

		if (!folio_test_uptodate(f))
			folio_mark_uptodate(f);

		bch2_set_folio_dirty(c, inode, f, &res, f_offset, f_len);

		f_pos = folio_end_pos(f);
		f_offset = 0;
	}

	inode->ei_last_dirtied = (unsigned long) current;
out:
	darray_for_each(folios, fi) {
		folio_unlock(*fi);
		folio_put(*fi);
	}

	/*
	 * If the last folio added to the mapping starts beyond current EOF, we
	 * performed a short write but left around at least one post-EOF folio.
	 * Clean up the mapping before we return.
	 */
	if (last_folio_pos >= inode->v.i_size)
		truncate_pagecache(&inode->v, inode->v.i_size);

	darray_exit(&folios);
	bch2_folio_reservation_put(c, inode, &res);

	return copied ?: ret;
}

static ssize_t bch2_buffered_write(struct kiocb *iocb, struct iov_iter *iter)
{
	struct file *file = iocb->ki_filp;
	struct address_space *mapping = file->f_mapping;
	struct bch_inode_info *inode = file_bch_inode(file);
	loff_t pos = iocb->ki_pos;
	ssize_t written = 0;
	int ret = 0;

	bch2_pagecache_add_get(inode);

	do {
		unsigned offset = pos & (PAGE_SIZE - 1);
		unsigned bytes = iov_iter_count(iter);
again:
		/*
		 * Bring in the user page that we will copy from _first_.
		 * Otherwise there's a nasty deadlock on copying from the
		 * same page as we're writing to, without it being marked
		 * up-to-date.
		 *
		 * Not only is this an optimisation, but it is also required
		 * to check that the address is actually valid, when atomic
		 * usercopies are used, below.
		 */
		if (unlikely(fault_in_iov_iter_readable(iter, bytes))) {
			bytes = min_t(unsigned long, iov_iter_count(iter),
				      PAGE_SIZE - offset);

			if (unlikely(fault_in_iov_iter_readable(iter, bytes))) {
				ret = -EFAULT;
				break;
			}
		}

		if (unlikely(fatal_signal_pending(current))) {
			ret = -EINTR;
			break;
		}

		ret = __bch2_buffered_write(inode, mapping, iter, pos, bytes);
		if (unlikely(ret < 0))
			break;

		cond_resched();

		if (unlikely(ret == 0)) {
			/*
			 * If we were unable to copy any data at all, we must
			 * fall back to a single segment length write.
			 *
			 * If we didn't fallback here, we could livelock
			 * because not all segments in the iov can be copied at
			 * once without a pagefault.
			 */
			bytes = min_t(unsigned long, PAGE_SIZE - offset,
				      iov_iter_single_seg_count(iter));
			goto again;
		}
		pos += ret;
		written += ret;
		ret = 0;

		balance_dirty_pages_ratelimited(mapping);
	} while (iov_iter_count(iter));

	bch2_pagecache_add_put(inode);

	return written ? written : ret;
}

/* O_DIRECT reads */

static void bio_check_or_release(struct bio *bio, bool check_dirty)
{
	if (check_dirty) {
		bio_check_pages_dirty(bio);
	} else {
		bio_release_pages(bio, false);
		bio_put(bio);
	}
}

static void bch2_dio_read_complete(struct closure *cl)
{
	struct dio_read *dio = container_of(cl, struct dio_read, cl);

	dio->req->ki_complete(dio->req, dio->ret);
	bio_check_or_release(&dio->rbio.bio, dio->should_dirty);
}

static void bch2_direct_IO_read_endio(struct bio *bio)
{
	struct dio_read *dio = bio->bi_private;

	if (bio->bi_status)
		dio->ret = blk_status_to_errno(bio->bi_status);

	closure_put(&dio->cl);
}

static void bch2_direct_IO_read_split_endio(struct bio *bio)
{
	struct dio_read *dio = bio->bi_private;
	bool should_dirty = dio->should_dirty;

	bch2_direct_IO_read_endio(bio);
	bio_check_or_release(bio, should_dirty);
}

static int bch2_direct_IO_read(struct kiocb *req, struct iov_iter *iter)
{
	struct file *file = req->ki_filp;
	struct bch_inode_info *inode = file_bch_inode(file);
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	struct bch_io_opts opts;
	struct dio_read *dio;
	struct bio *bio;
	loff_t offset = req->ki_pos;
	bool sync = is_sync_kiocb(req);
	size_t shorten;
	ssize_t ret;

	bch2_inode_opts_get(&opts, c, &inode->ei_inode);

	if ((offset|iter->count) & (block_bytes(c) - 1))
		return -EINVAL;

	ret = min_t(loff_t, iter->count,
		    max_t(loff_t, 0, i_size_read(&inode->v) - offset));

	if (!ret)
		return ret;

	shorten = iov_iter_count(iter) - round_up(ret, block_bytes(c));
	iter->count -= shorten;

	bio = bio_alloc_bioset(NULL,
			       bio_iov_vecs_to_alloc(iter, BIO_MAX_VECS),
			       REQ_OP_READ,
			       GFP_KERNEL,
			       &c->dio_read_bioset);

	bio->bi_end_io = bch2_direct_IO_read_endio;

	dio = container_of(bio, struct dio_read, rbio.bio);
	closure_init(&dio->cl, NULL);

	/*
	 * this is a _really_ horrible hack just to avoid an atomic sub at the
	 * end:
	 */
	if (!sync) {
		set_closure_fn(&dio->cl, bch2_dio_read_complete, NULL);
		atomic_set(&dio->cl.remaining,
			   CLOSURE_REMAINING_INITIALIZER -
			   CLOSURE_RUNNING +
			   CLOSURE_DESTRUCTOR);
	} else {
		atomic_set(&dio->cl.remaining,
			   CLOSURE_REMAINING_INITIALIZER + 1);
	}

	dio->req	= req;
	dio->ret	= ret;
	/*
	 * This is one of the sketchier things I've encountered: we have to skip
	 * the dirtying of requests that are internal from the kernel (i.e. from
	 * loopback), because we'll deadlock on page_lock.
	 */
	dio->should_dirty = iter_is_iovec(iter);

	goto start;
	while (iter->count) {
		bio = bio_alloc_bioset(NULL,
				       bio_iov_vecs_to_alloc(iter, BIO_MAX_VECS),
				       REQ_OP_READ,
				       GFP_KERNEL,
				       &c->bio_read);
		bio->bi_end_io		= bch2_direct_IO_read_split_endio;
start:
		bio->bi_opf		= REQ_OP_READ|REQ_SYNC;
		bio->bi_iter.bi_sector	= offset >> 9;
		bio->bi_private		= dio;

		ret = bio_iov_iter_get_pages(bio, iter);
		if (ret < 0) {
			/* XXX: fault inject this path */
			bio->bi_status = BLK_STS_RESOURCE;
			bio_endio(bio);
			break;
		}

		offset += bio->bi_iter.bi_size;

		if (dio->should_dirty)
			bio_set_pages_dirty(bio);

		if (iter->count)
			closure_get(&dio->cl);

		bch2_read(c, rbio_init(bio, opts), inode_inum(inode));
	}

	iter->count += shorten;

	if (sync) {
		closure_sync(&dio->cl);
		closure_debug_destroy(&dio->cl);
		ret = dio->ret;
		bio_check_or_release(&dio->rbio.bio, dio->should_dirty);
		return ret;
	} else {
		return -EIOCBQUEUED;
	}
}

ssize_t bch2_read_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	struct file *file = iocb->ki_filp;
	struct bch_inode_info *inode = file_bch_inode(file);
	struct address_space *mapping = file->f_mapping;
	size_t count = iov_iter_count(iter);
	ssize_t ret;

	if (!count)
		return 0; /* skip atime */

	if (iocb->ki_flags & IOCB_DIRECT) {
		struct blk_plug plug;

		if (unlikely(mapping->nrpages)) {
			ret = filemap_write_and_wait_range(mapping,
						iocb->ki_pos,
						iocb->ki_pos + count - 1);
			if (ret < 0)
				goto out;
		}

		file_accessed(file);

		blk_start_plug(&plug);
		ret = bch2_direct_IO_read(iocb, iter);
		blk_finish_plug(&plug);

		if (ret >= 0)
			iocb->ki_pos += ret;
	} else {
		bch2_pagecache_add_get(inode);
		ret = generic_file_read_iter(iocb, iter);
		bch2_pagecache_add_put(inode);
	}
out:
	return bch2_err_class(ret);
}

/* O_DIRECT writes */

static bool bch2_check_range_allocated(struct bch_fs *c, subvol_inum inum,
				       u64 offset, u64 size,
				       unsigned nr_replicas, bool compressed)
{
	struct btree_trans trans;
	struct btree_iter iter;
	struct bkey_s_c k;
	u64 end = offset + size;
	u32 snapshot;
	bool ret = true;
	int err;

	bch2_trans_init(&trans, c, 0, 0);
retry:
	bch2_trans_begin(&trans);

	err = bch2_subvolume_get_snapshot(&trans, inum.subvol, &snapshot);
	if (err)
		goto err;

	for_each_btree_key_norestart(&trans, iter, BTREE_ID_extents,
			   SPOS(inum.inum, offset, snapshot),
			   BTREE_ITER_SLOTS, k, err) {
		if (bkey_ge(bkey_start_pos(k.k), POS(inum.inum, end)))
			break;

		if (k.k->p.snapshot != snapshot ||
		    nr_replicas > bch2_bkey_replicas(c, k) ||
		    (!compressed && bch2_bkey_sectors_compressed(k))) {
			ret = false;
			break;
		}
	}

	offset = iter.pos.offset;
	bch2_trans_iter_exit(&trans, &iter);
err:
	if (bch2_err_matches(err, BCH_ERR_transaction_restart))
		goto retry;
	bch2_trans_exit(&trans);

	return err ? false : ret;
}

static noinline bool bch2_dio_write_check_allocated(struct dio_write *dio)
{
	struct bch_fs *c = dio->op.c;
	struct bch_inode_info *inode = dio->inode;
	struct bio *bio = &dio->op.wbio.bio;

	return bch2_check_range_allocated(c, inode_inum(inode),
				dio->op.pos.offset, bio_sectors(bio),
				dio->op.opts.data_replicas,
				dio->op.opts.compression != 0);
}

static void bch2_dio_write_loop_async(struct bch_write_op *);
static __always_inline long bch2_dio_write_done(struct dio_write *dio);

/*
 * We're going to return -EIOCBQUEUED, but we haven't finished consuming the
 * iov_iter yet, so we need to stash a copy of the iovec: it might be on the
 * caller's stack, we're not guaranteed that it will live for the duration of
 * the IO:
 */
static noinline int bch2_dio_write_copy_iov(struct dio_write *dio)
{
	struct iovec *iov = dio->inline_vecs;

	/*
	 * iov_iter has a single embedded iovec - nothing to do:
	 */
	if (iter_is_ubuf(&dio->iter))
		return 0;

	/*
	 * We don't currently handle non-iovec iov_iters here - return an error,
	 * and we'll fall back to doing the IO synchronously:
	 */
	if (!iter_is_iovec(&dio->iter))
		return -1;

	if (dio->iter.nr_segs > ARRAY_SIZE(dio->inline_vecs)) {
		iov = kmalloc_array(dio->iter.nr_segs, sizeof(*iov),
				    GFP_KERNEL);
		if (unlikely(!iov))
			return -ENOMEM;

		dio->free_iov = true;
	}

	memcpy(iov, dio->iter.__iov, dio->iter.nr_segs * sizeof(*iov));
	dio->iter.__iov = iov;
	return 0;
}

static void bch2_dio_write_flush_done(struct closure *cl)
{
	struct dio_write *dio = container_of(cl, struct dio_write, op.cl);
	struct bch_fs *c = dio->op.c;

	closure_debug_destroy(cl);

	dio->op.error = bch2_journal_error(&c->journal);

	bch2_dio_write_done(dio);
}

static noinline void bch2_dio_write_flush(struct dio_write *dio)
{
	struct bch_fs *c = dio->op.c;
	struct bch_inode_unpacked inode;
	int ret;

	dio->flush = 0;

	closure_init(&dio->op.cl, NULL);

	if (!dio->op.error) {
		ret = bch2_inode_find_by_inum(c, inode_inum(dio->inode), &inode);
		if (ret) {
			dio->op.error = ret;
		} else {
			bch2_journal_flush_seq_async(&c->journal, inode.bi_journal_seq, &dio->op.cl);
			bch2_inode_flush_nocow_writes_async(c, dio->inode, &dio->op.cl);
		}
	}

	if (dio->sync) {
		closure_sync(&dio->op.cl);
		closure_debug_destroy(&dio->op.cl);
	} else {
		continue_at(&dio->op.cl, bch2_dio_write_flush_done, NULL);
	}
}

static __always_inline long bch2_dio_write_done(struct dio_write *dio)
{
	struct kiocb *req = dio->req;
	struct bch_inode_info *inode = dio->inode;
	bool sync = dio->sync;
	long ret;

	if (unlikely(dio->flush)) {
		bch2_dio_write_flush(dio);
		if (!sync)
			return -EIOCBQUEUED;
	}

	bch2_pagecache_block_put(inode);

	if (dio->free_iov)
		kfree(dio->iter.__iov);

	ret = dio->op.error ?: ((long) dio->written << 9);
	bio_put(&dio->op.wbio.bio);

	/* inode->i_dio_count is our ref on inode and thus bch_fs */
	inode_dio_end(&inode->v);

	if (ret < 0)
		ret = bch2_err_class(ret);

	if (!sync) {
		req->ki_complete(req, ret);
		ret = -EIOCBQUEUED;
	}
	return ret;
}

static __always_inline void bch2_dio_write_end(struct dio_write *dio)
{
	struct bch_fs *c = dio->op.c;
	struct kiocb *req = dio->req;
	struct bch_inode_info *inode = dio->inode;
	struct bio *bio = &dio->op.wbio.bio;

	req->ki_pos	+= (u64) dio->op.written << 9;
	dio->written	+= dio->op.written;

	if (dio->extending) {
		spin_lock(&inode->v.i_lock);
		if (req->ki_pos > inode->v.i_size)
			i_size_write(&inode->v, req->ki_pos);
		spin_unlock(&inode->v.i_lock);
	}

	if (dio->op.i_sectors_delta || dio->quota_res.sectors) {
		mutex_lock(&inode->ei_quota_lock);
		__i_sectors_acct(c, inode, &dio->quota_res, dio->op.i_sectors_delta);
		__bch2_quota_reservation_put(c, inode, &dio->quota_res);
		mutex_unlock(&inode->ei_quota_lock);
	}

	bio_release_pages(bio, false);

	if (unlikely(dio->op.error))
		set_bit(EI_INODE_ERROR, &inode->ei_flags);
}

static __always_inline long bch2_dio_write_loop(struct dio_write *dio)
{
	struct bch_fs *c = dio->op.c;
	struct kiocb *req = dio->req;
	struct address_space *mapping = dio->mapping;
	struct bch_inode_info *inode = dio->inode;
	struct bch_io_opts opts;
	struct bio *bio = &dio->op.wbio.bio;
	unsigned unaligned, iter_count;
	bool sync = dio->sync, dropped_locks;
	long ret;

	bch2_inode_opts_get(&opts, c, &inode->ei_inode);

	while (1) {
		iter_count = dio->iter.count;

		EBUG_ON(current->faults_disabled_mapping);
		current->faults_disabled_mapping = mapping;

		ret = bio_iov_iter_get_pages(bio, &dio->iter);

		dropped_locks = fdm_dropped_locks();

		current->faults_disabled_mapping = NULL;

		/*
		 * If the fault handler returned an error but also signalled
		 * that it dropped & retook ei_pagecache_lock, we just need to
		 * re-shoot down the page cache and retry:
		 */
		if (dropped_locks && ret)
			ret = 0;

		if (unlikely(ret < 0))
			goto err;

		if (unlikely(dropped_locks)) {
			ret = write_invalidate_inode_pages_range(mapping,
					req->ki_pos,
					req->ki_pos + iter_count - 1);
			if (unlikely(ret))
				goto err;

			if (!bio->bi_iter.bi_size)
				continue;
		}

		unaligned = bio->bi_iter.bi_size & (block_bytes(c) - 1);
		bio->bi_iter.bi_size -= unaligned;
		iov_iter_revert(&dio->iter, unaligned);

		if (!bio->bi_iter.bi_size) {
			/*
			 * bio_iov_iter_get_pages was only able to get <
			 * blocksize worth of pages:
			 */
			ret = -EFAULT;
			goto err;
		}

		bch2_write_op_init(&dio->op, c, opts);
		dio->op.end_io		= sync
			? NULL
			: bch2_dio_write_loop_async;
		dio->op.target		= dio->op.opts.foreground_target;
		dio->op.write_point	= writepoint_hashed((unsigned long) current);
		dio->op.nr_replicas	= dio->op.opts.data_replicas;
		dio->op.subvol		= inode->ei_subvol;
		dio->op.pos		= POS(inode->v.i_ino, (u64) req->ki_pos >> 9);
		dio->op.devs_need_flush	= &inode->ei_devs_need_flush;

		if (sync)
			dio->op.flags |= BCH_WRITE_SYNC;
		dio->op.flags |= BCH_WRITE_CHECK_ENOSPC;

		ret = bch2_quota_reservation_add(c, inode, &dio->quota_res,
						 bio_sectors(bio), true);
		if (unlikely(ret))
			goto err;

		ret = bch2_disk_reservation_get(c, &dio->op.res, bio_sectors(bio),
						dio->op.opts.data_replicas, 0);
		if (unlikely(ret) &&
		    !bch2_dio_write_check_allocated(dio))
			goto err;

		task_io_account_write(bio->bi_iter.bi_size);

		if (unlikely(dio->iter.count) &&
		    !dio->sync &&
		    !dio->loop &&
		    bch2_dio_write_copy_iov(dio))
			dio->sync = sync = true;

		dio->loop = true;
		closure_call(&dio->op.cl, bch2_write, NULL, NULL);

		if (!sync)
			return -EIOCBQUEUED;

		bch2_dio_write_end(dio);

		if (likely(!dio->iter.count) || dio->op.error)
			break;

		bio_reset(bio, NULL, REQ_OP_WRITE);
	}
out:
	return bch2_dio_write_done(dio);
err:
	dio->op.error = ret;

	bio_release_pages(bio, false);

	bch2_quota_reservation_put(c, inode, &dio->quota_res);
	goto out;
}

static noinline __cold void bch2_dio_write_continue(struct dio_write *dio)
{
	struct mm_struct *mm = dio->mm;

	bio_reset(&dio->op.wbio.bio, NULL, REQ_OP_WRITE);

	if (mm)
		kthread_use_mm(mm);
	bch2_dio_write_loop(dio);
	if (mm)
		kthread_unuse_mm(mm);
}

static void bch2_dio_write_loop_async(struct bch_write_op *op)
{
	struct dio_write *dio = container_of(op, struct dio_write, op);

	bch2_dio_write_end(dio);

	if (likely(!dio->iter.count) || dio->op.error)
		bch2_dio_write_done(dio);
	else
		bch2_dio_write_continue(dio);
}

static noinline
ssize_t bch2_direct_write(struct kiocb *req, struct iov_iter *iter)
{
	struct file *file = req->ki_filp;
	struct address_space *mapping = file->f_mapping;
	struct bch_inode_info *inode = file_bch_inode(file);
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	struct dio_write *dio;
	struct bio *bio;
	bool locked = true, extending;
	ssize_t ret;

	prefetch(&c->opts);
	prefetch((void *) &c->opts + 64);
	prefetch(&inode->ei_inode);
	prefetch((void *) &inode->ei_inode + 64);

	inode_lock(&inode->v);

	ret = generic_write_checks(req, iter);
	if (unlikely(ret <= 0))
		goto err;

	ret = file_remove_privs(file);
	if (unlikely(ret))
		goto err;

	ret = file_update_time(file);
	if (unlikely(ret))
		goto err;

	if (unlikely((req->ki_pos|iter->count) & (block_bytes(c) - 1)))
		goto err;

	inode_dio_begin(&inode->v);
	bch2_pagecache_block_get(inode);

	extending = req->ki_pos + iter->count > inode->v.i_size;
	if (!extending) {
		inode_unlock(&inode->v);
		locked = false;
	}

	bio = bio_alloc_bioset(NULL,
			       bio_iov_vecs_to_alloc(iter, BIO_MAX_VECS),
			       REQ_OP_WRITE,
			       GFP_KERNEL,
			       &c->dio_write_bioset);
	dio = container_of(bio, struct dio_write, op.wbio.bio);
	dio->req		= req;
	dio->mapping		= mapping;
	dio->inode		= inode;
	dio->mm			= current->mm;
	dio->loop		= false;
	dio->extending		= extending;
	dio->sync		= is_sync_kiocb(req) || extending;
	dio->flush		= iocb_is_dsync(req) && !c->opts.journal_flush_disabled;
	dio->free_iov		= false;
	dio->quota_res.sectors	= 0;
	dio->written		= 0;
	dio->iter		= *iter;
	dio->op.c		= c;

	if (unlikely(mapping->nrpages)) {
		ret = write_invalidate_inode_pages_range(mapping,
						req->ki_pos,
						req->ki_pos + iter->count - 1);
		if (unlikely(ret))
			goto err_put_bio;
	}

	ret = bch2_dio_write_loop(dio);
err:
	if (locked)
		inode_unlock(&inode->v);
	return ret;
err_put_bio:
	bch2_pagecache_block_put(inode);
	bio_put(bio);
	inode_dio_end(&inode->v);
	goto err;
}

ssize_t bch2_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	struct bch_inode_info *inode = file_bch_inode(file);
	ssize_t	ret;

	if (iocb->ki_flags & IOCB_DIRECT) {
		ret = bch2_direct_write(iocb, from);
		goto out;
	}

	inode_lock(&inode->v);

	ret = generic_write_checks(iocb, from);
	if (ret <= 0)
		goto unlock;

	ret = file_remove_privs(file);
	if (ret)
		goto unlock;

	ret = file_update_time(file);
	if (ret)
		goto unlock;

	ret = bch2_buffered_write(iocb, from);
	if (likely(ret > 0))
		iocb->ki_pos += ret;
unlock:
	inode_unlock(&inode->v);

	if (ret > 0)
		ret = generic_write_sync(iocb, ret);
out:
	return bch2_err_class(ret);
}

/* fsync: */

/*
 * inode->ei_inode.bi_journal_seq won't be up to date since it's set in an
 * insert trigger: look up the btree inode instead
 */
static int bch2_flush_inode(struct bch_fs *c,
			    struct bch_inode_info *inode)
{
	struct bch_inode_unpacked u;
	int ret;

	if (c->opts.journal_flush_disabled)
		return 0;

	ret = bch2_inode_find_by_inum(c, inode_inum(inode), &u);
	if (ret)
		return ret;

	return bch2_journal_flush_seq(&c->journal, u.bi_journal_seq) ?:
		bch2_inode_flush_nocow_writes(c, inode);
}

int bch2_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	struct bch_inode_info *inode = file_bch_inode(file);
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	int ret, ret2, ret3;

	ret = file_write_and_wait_range(file, start, end);
	ret2 = sync_inode_metadata(&inode->v, 1);
	ret3 = bch2_flush_inode(c, inode);

	return bch2_err_class(ret ?: ret2 ?: ret3);
}

/* truncate: */

static inline int range_has_data(struct bch_fs *c, u32 subvol,
				 struct bpos start,
				 struct bpos end)
{
	struct btree_trans trans;
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret = 0;

	bch2_trans_init(&trans, c, 0, 0);
retry:
	bch2_trans_begin(&trans);

	ret = bch2_subvolume_get_snapshot(&trans, subvol, &start.snapshot);
	if (ret)
		goto err;

	for_each_btree_key_upto_norestart(&trans, iter, BTREE_ID_extents, start, end, 0, k, ret)
		if (bkey_extent_is_data(k.k) && !bkey_extent_is_unwritten(k)) {
			ret = 1;
			break;
		}
	start = iter.pos;
	bch2_trans_iter_exit(&trans, &iter);
err:
	if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
		goto retry;

	bch2_trans_exit(&trans);
	return ret;
}

static int __bch2_truncate_folio(struct bch_inode_info *inode,
				 pgoff_t index, loff_t start, loff_t end)
{
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	struct address_space *mapping = inode->v.i_mapping;
	struct bch_folio *s;
	unsigned start_offset = start & (PAGE_SIZE - 1);
	unsigned end_offset = ((end - 1) & (PAGE_SIZE - 1)) + 1;
	unsigned i;
	struct folio *folio;
	s64 i_sectors_delta = 0;
	int ret = 0;
	u64 end_pos;

	folio = filemap_lock_folio(mapping, index);
	if (!folio) {
		/*
		 * XXX: we're doing two index lookups when we end up reading the
		 * folio
		 */
		ret = range_has_data(c, inode->ei_subvol,
				POS(inode->v.i_ino, (index << PAGE_SECTORS_SHIFT)),
				POS(inode->v.i_ino, (index << PAGE_SECTORS_SHIFT) + PAGE_SECTORS));
		if (ret <= 0)
			return ret;

		folio = __filemap_get_folio(mapping, index,
					    FGP_LOCK|FGP_CREAT, GFP_KERNEL);
		if (unlikely(!folio)) {
			ret = -ENOMEM;
			goto out;
		}
	}

	BUG_ON(start	>= folio_end_pos(folio));
	BUG_ON(end	<= folio_pos(folio));

	start_offset	= max(start, folio_pos(folio)) - folio_pos(folio);
	end_offset	= min_t(u64, end, folio_end_pos(folio)) - folio_pos(folio);

	/* Folio boundary? Nothing to do */
	if (start_offset == 0 &&
	    end_offset == folio_size(folio)) {
		ret = 0;
		goto unlock;
	}

	s = bch2_folio_create(folio, 0);
	if (!s) {
		ret = -ENOMEM;
		goto unlock;
	}

	if (!folio_test_uptodate(folio)) {
		ret = bch2_read_single_folio(folio, mapping);
		if (ret)
			goto unlock;
	}

	ret = bch2_folio_set(c, inode_inum(inode), &folio, 1);
	if (ret)
		goto unlock;

	for (i = round_up(start_offset, block_bytes(c)) >> 9;
	     i < round_down(end_offset, block_bytes(c)) >> 9;
	     i++) {
		s->s[i].nr_replicas	= 0;

		i_sectors_delta -= s->s[i].state == SECTOR_dirty;
		folio_sector_set(folio, s, i, SECTOR_unallocated);
	}

	i_sectors_acct(c, inode, NULL, i_sectors_delta);

	/*
	 * Caller needs to know whether this folio will be written out by
	 * writeback - doing an i_size update if necessary - or whether it will
	 * be responsible for the i_size update.
	 *
	 * Note that we shouldn't ever see a folio beyond EOF, but check and
	 * warn if so. This has been observed by failure to clean up folios
	 * after a short write and there's still a chance reclaim will fix
	 * things up.
	 */
	WARN_ON_ONCE(folio_pos(folio) >= inode->v.i_size);
	end_pos = folio_end_pos(folio);
	if (inode->v.i_size > folio_pos(folio))
		end_pos = min_t(u64, inode->v.i_size, end_pos);
	ret = s->s[folio_pos_to_s(folio, end_pos - 1)].state >= SECTOR_dirty;

	folio_zero_segment(folio, start_offset, end_offset);

	/*
	 * Bit of a hack - we don't want truncate to fail due to -ENOSPC.
	 *
	 * XXX: because we aren't currently tracking whether the folio has actual
	 * data in it (vs. just 0s, or only partially written) this wrong. ick.
	 */
	BUG_ON(bch2_get_folio_disk_reservation(c, inode, folio, false));

	/*
	 * This removes any writeable userspace mappings; we need to force
	 * .page_mkwrite to be called again before any mmapped writes, to
	 * redirty the full page:
	 */
	folio_mkclean(folio);
	filemap_dirty_folio(mapping, folio);
unlock:
	folio_unlock(folio);
	folio_put(folio);
out:
	return ret;
}

static int bch2_truncate_folio(struct bch_inode_info *inode, loff_t from)
{
	return __bch2_truncate_folio(inode, from >> PAGE_SHIFT,
				     from, ANYSINT_MAX(loff_t));
}

static int bch2_truncate_folios(struct bch_inode_info *inode,
				loff_t start, loff_t end)
{
	int ret = __bch2_truncate_folio(inode, start >> PAGE_SHIFT,
					start, end);

	if (ret >= 0 &&
	    start >> PAGE_SHIFT != end >> PAGE_SHIFT)
		ret = __bch2_truncate_folio(inode,
					(end - 1) >> PAGE_SHIFT,
					start, end);
	return ret;
}

static int bch2_extend(struct mnt_idmap *idmap,
		       struct bch_inode_info *inode,
		       struct bch_inode_unpacked *inode_u,
		       struct iattr *iattr)
{
	struct address_space *mapping = inode->v.i_mapping;
	int ret;

	/*
	 * sync appends:
	 *
	 * this has to be done _before_ extending i_size:
	 */
	ret = filemap_write_and_wait_range(mapping, inode_u->bi_size, S64_MAX);
	if (ret)
		return ret;

	truncate_setsize(&inode->v, iattr->ia_size);

	return bch2_setattr_nonsize(idmap, inode, iattr);
}

static int bch2_truncate_finish_fn(struct bch_inode_info *inode,
				   struct bch_inode_unpacked *bi,
				   void *p)
{
	bi->bi_flags &= ~BCH_INODE_I_SIZE_DIRTY;
	return 0;
}

static int bch2_truncate_start_fn(struct bch_inode_info *inode,
				  struct bch_inode_unpacked *bi, void *p)
{
	u64 *new_i_size = p;

	bi->bi_flags |= BCH_INODE_I_SIZE_DIRTY;
	bi->bi_size = *new_i_size;
	return 0;
}

int bch2_truncate(struct mnt_idmap *idmap,
		  struct bch_inode_info *inode, struct iattr *iattr)
{
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	struct address_space *mapping = inode->v.i_mapping;
	struct bch_inode_unpacked inode_u;
	u64 new_i_size = iattr->ia_size;
	s64 i_sectors_delta = 0;
	int ret = 0;

	/*
	 * If the truncate call with change the size of the file, the
	 * cmtimes should be updated. If the size will not change, we
	 * do not need to update the cmtimes.
	 */
	if (iattr->ia_size != inode->v.i_size) {
		if (!(iattr->ia_valid & ATTR_MTIME))
			ktime_get_coarse_real_ts64(&iattr->ia_mtime);
		if (!(iattr->ia_valid & ATTR_CTIME))
			ktime_get_coarse_real_ts64(&iattr->ia_ctime);
		iattr->ia_valid |= ATTR_MTIME|ATTR_CTIME;
	}

	inode_dio_wait(&inode->v);
	bch2_pagecache_block_get(inode);

	ret = bch2_inode_find_by_inum(c, inode_inum(inode), &inode_u);
	if (ret)
		goto err;

	/*
	 * check this before next assertion; on filesystem error our normal
	 * invariants are a bit broken (truncate has to truncate the page cache
	 * before the inode).
	 */
	ret = bch2_journal_error(&c->journal);
	if (ret)
		goto err;

	WARN_ONCE(!test_bit(EI_INODE_ERROR, &inode->ei_flags) &&
		  inode->v.i_size < inode_u.bi_size,
		  "truncate spotted in mem i_size < btree i_size: %llu < %llu\n",
		  (u64) inode->v.i_size, inode_u.bi_size);

	if (iattr->ia_size > inode->v.i_size) {
		ret = bch2_extend(idmap, inode, &inode_u, iattr);
		goto err;
	}

	iattr->ia_valid &= ~ATTR_SIZE;

	ret = bch2_truncate_folio(inode, iattr->ia_size);
	if (unlikely(ret < 0))
		goto err;

	/*
	 * When extending, we're going to write the new i_size to disk
	 * immediately so we need to flush anything above the current on disk
	 * i_size first:
	 *
	 * Also, when extending we need to flush the page that i_size currently
	 * straddles - if it's mapped to userspace, we need to ensure that
	 * userspace has to redirty it and call .mkwrite -> set_page_dirty
	 * again to allocate the part of the page that was extended.
	 */
	if (iattr->ia_size > inode_u.bi_size)
		ret = filemap_write_and_wait_range(mapping,
				inode_u.bi_size,
				iattr->ia_size - 1);
	else if (iattr->ia_size & (PAGE_SIZE - 1))
		ret = filemap_write_and_wait_range(mapping,
				round_down(iattr->ia_size, PAGE_SIZE),
				iattr->ia_size - 1);
	if (ret)
		goto err;

	mutex_lock(&inode->ei_update_lock);
	ret = bch2_write_inode(c, inode, bch2_truncate_start_fn,
			       &new_i_size, 0);
	mutex_unlock(&inode->ei_update_lock);

	if (unlikely(ret))
		goto err;

	truncate_setsize(&inode->v, iattr->ia_size);

	ret = bch2_fpunch(c, inode_inum(inode),
			round_up(iattr->ia_size, block_bytes(c)) >> 9,
			U64_MAX, &i_sectors_delta);
	i_sectors_acct(c, inode, NULL, i_sectors_delta);

	bch2_fs_inconsistent_on(!inode->v.i_size && inode->v.i_blocks &&
				!bch2_journal_error(&c->journal), c,
				"inode %lu truncated to 0 but i_blocks %llu (ondisk %lli)",
				inode->v.i_ino, (u64) inode->v.i_blocks,
				inode->ei_inode.bi_sectors);
	if (unlikely(ret))
		goto err;

	mutex_lock(&inode->ei_update_lock);
	ret = bch2_write_inode(c, inode, bch2_truncate_finish_fn, NULL, 0);
	mutex_unlock(&inode->ei_update_lock);

	ret = bch2_setattr_nonsize(idmap, inode, iattr);
err:
	bch2_pagecache_block_put(inode);
	return bch2_err_class(ret);
}

/* fallocate: */

static int inode_update_times_fn(struct bch_inode_info *inode,
				 struct bch_inode_unpacked *bi, void *p)
{
	struct bch_fs *c = inode->v.i_sb->s_fs_info;

	bi->bi_mtime = bi->bi_ctime = bch2_current_time(c);
	return 0;
}

static long bchfs_fpunch(struct bch_inode_info *inode, loff_t offset, loff_t len)
{
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	u64 end		= offset + len;
	u64 block_start	= round_up(offset, block_bytes(c));
	u64 block_end	= round_down(end, block_bytes(c));
	bool truncated_last_page;
	int ret = 0;

	ret = bch2_truncate_folios(inode, offset, end);
	if (unlikely(ret < 0))
		goto err;

	truncated_last_page = ret;

	truncate_pagecache_range(&inode->v, offset, end - 1);

	if (block_start < block_end) {
		s64 i_sectors_delta = 0;

		ret = bch2_fpunch(c, inode_inum(inode),
				  block_start >> 9, block_end >> 9,
				  &i_sectors_delta);
		i_sectors_acct(c, inode, NULL, i_sectors_delta);
	}

	mutex_lock(&inode->ei_update_lock);
	if (end >= inode->v.i_size && !truncated_last_page) {
		ret = bch2_write_inode_size(c, inode, inode->v.i_size,
					    ATTR_MTIME|ATTR_CTIME);
	} else {
		ret = bch2_write_inode(c, inode, inode_update_times_fn, NULL,
				       ATTR_MTIME|ATTR_CTIME);
	}
	mutex_unlock(&inode->ei_update_lock);
err:
	return ret;
}

static long bchfs_fcollapse_finsert(struct bch_inode_info *inode,
				   loff_t offset, loff_t len,
				   bool insert)
{
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	struct address_space *mapping = inode->v.i_mapping;
	struct bkey_buf copy;
	struct btree_trans trans;
	struct btree_iter src, dst, del;
	loff_t shift, new_size;
	u64 src_start;
	int ret = 0;

	if ((offset | len) & (block_bytes(c) - 1))
		return -EINVAL;

	if (insert) {
		if (inode->v.i_sb->s_maxbytes - inode->v.i_size < len)
			return -EFBIG;

		if (offset >= inode->v.i_size)
			return -EINVAL;

		src_start	= U64_MAX;
		shift		= len;
	} else {
		if (offset + len >= inode->v.i_size)
			return -EINVAL;

		src_start	= offset + len;
		shift		= -len;
	}

	new_size = inode->v.i_size + shift;

	ret = write_invalidate_inode_pages_range(mapping, offset, LLONG_MAX);
	if (ret)
		return ret;

	if (insert) {
		i_size_write(&inode->v, new_size);
		mutex_lock(&inode->ei_update_lock);
		ret = bch2_write_inode_size(c, inode, new_size,
					    ATTR_MTIME|ATTR_CTIME);
		mutex_unlock(&inode->ei_update_lock);
	} else {
		s64 i_sectors_delta = 0;

		ret = bch2_fpunch(c, inode_inum(inode),
				  offset >> 9, (offset + len) >> 9,
				  &i_sectors_delta);
		i_sectors_acct(c, inode, NULL, i_sectors_delta);

		if (ret)
			return ret;
	}

	bch2_bkey_buf_init(&copy);
	bch2_trans_init(&trans, c, BTREE_ITER_MAX, 1024);
	bch2_trans_iter_init(&trans, &src, BTREE_ID_extents,
			POS(inode->v.i_ino, src_start >> 9),
			BTREE_ITER_INTENT);
	bch2_trans_copy_iter(&dst, &src);
	bch2_trans_copy_iter(&del, &src);

	while (ret == 0 ||
	       bch2_err_matches(ret, BCH_ERR_transaction_restart)) {
		struct disk_reservation disk_res =
			bch2_disk_reservation_init(c, 0);
		struct bkey_i delete;
		struct bkey_s_c k;
		struct bpos next_pos;
		struct bpos move_pos = POS(inode->v.i_ino, offset >> 9);
		struct bpos atomic_end;
		unsigned trigger_flags = 0;
		u32 snapshot;

		bch2_trans_begin(&trans);

		ret = bch2_subvolume_get_snapshot(&trans,
					inode->ei_subvol, &snapshot);
		if (ret)
			continue;

		bch2_btree_iter_set_snapshot(&src, snapshot);
		bch2_btree_iter_set_snapshot(&dst, snapshot);
		bch2_btree_iter_set_snapshot(&del, snapshot);

		bch2_trans_begin(&trans);

		k = insert
			? bch2_btree_iter_peek_prev(&src)
			: bch2_btree_iter_peek_upto(&src, POS(inode->v.i_ino, U64_MAX));
		if ((ret = bkey_err(k)))
			continue;

		if (!k.k || k.k->p.inode != inode->v.i_ino)
			break;

		if (insert &&
		    bkey_le(k.k->p, POS(inode->v.i_ino, offset >> 9)))
			break;
reassemble:
		bch2_bkey_buf_reassemble(&copy, c, k);

		if (insert &&
		    bkey_lt(bkey_start_pos(k.k), move_pos))
			bch2_cut_front(move_pos, copy.k);

		copy.k->k.p.offset += shift >> 9;
		bch2_btree_iter_set_pos(&dst, bkey_start_pos(&copy.k->k));

		ret = bch2_extent_atomic_end(&trans, &dst, copy.k, &atomic_end);
		if (ret)
			continue;

		if (!bkey_eq(atomic_end, copy.k->k.p)) {
			if (insert) {
				move_pos = atomic_end;
				move_pos.offset -= shift >> 9;
				goto reassemble;
			} else {
				bch2_cut_back(atomic_end, copy.k);
			}
		}

		bkey_init(&delete.k);
		delete.k.p = copy.k->k.p;
		delete.k.size = copy.k->k.size;
		delete.k.p.offset -= shift >> 9;
		bch2_btree_iter_set_pos(&del, bkey_start_pos(&delete.k));

		next_pos = insert ? bkey_start_pos(&delete.k) : delete.k.p;

		if (copy.k->k.size != k.k->size) {
			/* We might end up splitting compressed extents: */
			unsigned nr_ptrs =
				bch2_bkey_nr_ptrs_allocated(bkey_i_to_s_c(copy.k));

			ret = bch2_disk_reservation_get(c, &disk_res,
					copy.k->k.size, nr_ptrs,
					BCH_DISK_RESERVATION_NOFAIL);
			BUG_ON(ret);
		}

		ret =   bch2_btree_iter_traverse(&del) ?:
			bch2_trans_update(&trans, &del, &delete, trigger_flags) ?:
			bch2_trans_update(&trans, &dst, copy.k, trigger_flags) ?:
			bch2_trans_commit(&trans, &disk_res, NULL,
					  BTREE_INSERT_NOFAIL);
		bch2_disk_reservation_put(c, &disk_res);

		if (!ret)
			bch2_btree_iter_set_pos(&src, next_pos);
	}
	bch2_trans_iter_exit(&trans, &del);
	bch2_trans_iter_exit(&trans, &dst);
	bch2_trans_iter_exit(&trans, &src);
	bch2_trans_exit(&trans);
	bch2_bkey_buf_exit(&copy, c);

	if (ret)
		return ret;

	mutex_lock(&inode->ei_update_lock);
	if (!insert) {
		i_size_write(&inode->v, new_size);
		ret = bch2_write_inode_size(c, inode, new_size,
					    ATTR_MTIME|ATTR_CTIME);
	} else {
		/* We need an inode update to update bi_journal_seq for fsync: */
		ret = bch2_write_inode(c, inode, inode_update_times_fn, NULL,
				       ATTR_MTIME|ATTR_CTIME);
	}
	mutex_unlock(&inode->ei_update_lock);
	return ret;
}

static int __bchfs_fallocate(struct bch_inode_info *inode, int mode,
			     u64 start_sector, u64 end_sector)
{
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	struct btree_trans trans;
	struct btree_iter iter;
	struct bpos end_pos = POS(inode->v.i_ino, end_sector);
	struct bch_io_opts opts;
	int ret = 0;

	bch2_inode_opts_get(&opts, c, &inode->ei_inode);
	bch2_trans_init(&trans, c, BTREE_ITER_MAX, 512);

	bch2_trans_iter_init(&trans, &iter, BTREE_ID_extents,
			POS(inode->v.i_ino, start_sector),
			BTREE_ITER_SLOTS|BTREE_ITER_INTENT);

	while (!ret && bkey_lt(iter.pos, end_pos)) {
		s64 i_sectors_delta = 0;
		struct quota_res quota_res = { 0 };
		struct bkey_s_c k;
		unsigned sectors;
		u32 snapshot;

		bch2_trans_begin(&trans);

		ret = bch2_subvolume_get_snapshot(&trans,
					inode->ei_subvol, &snapshot);
		if (ret)
			goto bkey_err;

		bch2_btree_iter_set_snapshot(&iter, snapshot);

		k = bch2_btree_iter_peek_slot(&iter);
		if ((ret = bkey_err(k)))
			goto bkey_err;

		/* already reserved */
		if (bkey_extent_is_reservation(k) &&
		    bch2_bkey_nr_ptrs_fully_allocated(k) >= opts.data_replicas) {
			bch2_btree_iter_advance(&iter);
			continue;
		}

		if (bkey_extent_is_data(k.k) &&
		    !(mode & FALLOC_FL_ZERO_RANGE)) {
			bch2_btree_iter_advance(&iter);
			continue;
		}

		/*
		 * XXX: for nocow mode, we should promote shared extents to
		 * unshared here
		 */

		sectors = bpos_min(k.k->p, end_pos).offset - iter.pos.offset;

		if (!bkey_extent_is_allocation(k.k)) {
			ret = bch2_quota_reservation_add(c, inode,
					&quota_res,
					sectors, true);
			if (unlikely(ret))
				goto bkey_err;
		}

		ret = bch2_extent_fallocate(&trans, inode_inum(inode), &iter,
					    sectors, opts, &i_sectors_delta,
					    writepoint_hashed((unsigned long) current));
		if (ret)
			goto bkey_err;

		i_sectors_acct(c, inode, &quota_res, i_sectors_delta);
bkey_err:
		bch2_quota_reservation_put(c, inode, &quota_res);
		if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
			ret = 0;
	}

	bch2_trans_unlock(&trans); /* lock ordering, before taking pagecache locks: */
	mark_pagecache_reserved(inode, start_sector, iter.pos.offset);

	if (bch2_err_matches(ret, ENOSPC) && (mode & FALLOC_FL_ZERO_RANGE)) {
		struct quota_res quota_res = { 0 };
		s64 i_sectors_delta = 0;

		bch2_fpunch_at(&trans, &iter, inode_inum(inode),
			       end_sector, &i_sectors_delta);
		i_sectors_acct(c, inode, &quota_res, i_sectors_delta);
		bch2_quota_reservation_put(c, inode, &quota_res);
	}

	bch2_trans_iter_exit(&trans, &iter);
	bch2_trans_exit(&trans);
	return ret;
}

static long bchfs_fallocate(struct bch_inode_info *inode, int mode,
			    loff_t offset, loff_t len)
{
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	u64 end		= offset + len;
	u64 block_start	= round_down(offset,	block_bytes(c));
	u64 block_end	= round_up(end,		block_bytes(c));
	bool truncated_last_page = false;
	int ret, ret2 = 0;

	if (!(mode & FALLOC_FL_KEEP_SIZE) && end > inode->v.i_size) {
		ret = inode_newsize_ok(&inode->v, end);
		if (ret)
			return ret;
	}

	if (mode & FALLOC_FL_ZERO_RANGE) {
		ret = bch2_truncate_folios(inode, offset, end);
		if (unlikely(ret < 0))
			return ret;

		truncated_last_page = ret;

		truncate_pagecache_range(&inode->v, offset, end - 1);

		block_start	= round_up(offset,	block_bytes(c));
		block_end	= round_down(end,	block_bytes(c));
	}

	ret = __bchfs_fallocate(inode, mode, block_start >> 9, block_end >> 9);

	/*
	 * On -ENOSPC in ZERO_RANGE mode, we still want to do the inode update,
	 * so that the VFS cache i_size is consistent with the btree i_size:
	 */
	if (ret &&
	    !(bch2_err_matches(ret, ENOSPC) && (mode & FALLOC_FL_ZERO_RANGE)))
		return ret;

	if (mode & FALLOC_FL_KEEP_SIZE && end > inode->v.i_size)
		end = inode->v.i_size;

	if (end >= inode->v.i_size &&
	    (((mode & FALLOC_FL_ZERO_RANGE) && !truncated_last_page) ||
	     !(mode & FALLOC_FL_KEEP_SIZE))) {
		spin_lock(&inode->v.i_lock);
		i_size_write(&inode->v, end);
		spin_unlock(&inode->v.i_lock);

		mutex_lock(&inode->ei_update_lock);
		ret2 = bch2_write_inode_size(c, inode, end, 0);
		mutex_unlock(&inode->ei_update_lock);
	}

	return ret ?: ret2;
}

long bch2_fallocate_dispatch(struct file *file, int mode,
			     loff_t offset, loff_t len)
{
	struct bch_inode_info *inode = file_bch_inode(file);
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	long ret;

	if (!bch2_write_ref_tryget(c, BCH_WRITE_REF_fallocate))
		return -EROFS;

	inode_lock(&inode->v);
	inode_dio_wait(&inode->v);
	bch2_pagecache_block_get(inode);

	ret = file_modified(file);
	if (ret)
		goto err;

	if (!(mode & ~(FALLOC_FL_KEEP_SIZE|FALLOC_FL_ZERO_RANGE)))
		ret = bchfs_fallocate(inode, mode, offset, len);
	else if (mode == (FALLOC_FL_PUNCH_HOLE|FALLOC_FL_KEEP_SIZE))
		ret = bchfs_fpunch(inode, offset, len);
	else if (mode == FALLOC_FL_INSERT_RANGE)
		ret = bchfs_fcollapse_finsert(inode, offset, len, true);
	else if (mode == FALLOC_FL_COLLAPSE_RANGE)
		ret = bchfs_fcollapse_finsert(inode, offset, len, false);
	else
		ret = -EOPNOTSUPP;
err:
	bch2_pagecache_block_put(inode);
	inode_unlock(&inode->v);
	bch2_write_ref_put(c, BCH_WRITE_REF_fallocate);

	return bch2_err_class(ret);
}

/*
 * Take a quota reservation for unallocated blocks in a given file range
 * Does not check pagecache
 */
static int quota_reserve_range(struct bch_inode_info *inode,
			       struct quota_res *res,
			       u64 start, u64 end)
{
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	struct btree_trans trans;
	struct btree_iter iter;
	struct bkey_s_c k;
	u32 snapshot;
	u64 sectors = end - start;
	u64 pos = start;
	int ret;

	bch2_trans_init(&trans, c, 0, 0);
retry:
	bch2_trans_begin(&trans);

	ret = bch2_subvolume_get_snapshot(&trans, inode->ei_subvol, &snapshot);
	if (ret)
		goto err;

	bch2_trans_iter_init(&trans, &iter, BTREE_ID_extents,
			     SPOS(inode->v.i_ino, pos, snapshot), 0);

	while (!(ret = btree_trans_too_many_iters(&trans)) &&
	       (k = bch2_btree_iter_peek_upto(&iter, POS(inode->v.i_ino, end - 1))).k &&
	       !(ret = bkey_err(k))) {
		if (bkey_extent_is_allocation(k.k)) {
			u64 s = min(end, k.k->p.offset) -
				max(start, bkey_start_offset(k.k));
			BUG_ON(s > sectors);
			sectors -= s;
		}
		bch2_btree_iter_advance(&iter);
	}
	pos = iter.pos.offset;
	bch2_trans_iter_exit(&trans, &iter);
err:
	if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
		goto retry;

	bch2_trans_exit(&trans);

	if (ret)
		return ret;

	return bch2_quota_reservation_add(c, inode, res, sectors, true);
}

loff_t bch2_remap_file_range(struct file *file_src, loff_t pos_src,
			     struct file *file_dst, loff_t pos_dst,
			     loff_t len, unsigned remap_flags)
{
	struct bch_inode_info *src = file_bch_inode(file_src);
	struct bch_inode_info *dst = file_bch_inode(file_dst);
	struct bch_fs *c = src->v.i_sb->s_fs_info;
	struct quota_res quota_res = { 0 };
	s64 i_sectors_delta = 0;
	u64 aligned_len;
	loff_t ret = 0;

	if (remap_flags & ~(REMAP_FILE_DEDUP|REMAP_FILE_ADVISORY))
		return -EINVAL;

	if (remap_flags & REMAP_FILE_DEDUP)
		return -EOPNOTSUPP;

	if ((pos_src & (block_bytes(c) - 1)) ||
	    (pos_dst & (block_bytes(c) - 1)))
		return -EINVAL;

	if (src == dst &&
	    abs(pos_src - pos_dst) < len)
		return -EINVAL;

	bch2_lock_inodes(INODE_LOCK|INODE_PAGECACHE_BLOCK, src, dst);

	inode_dio_wait(&src->v);
	inode_dio_wait(&dst->v);

	ret = generic_remap_file_range_prep(file_src, pos_src,
					    file_dst, pos_dst,
					    &len, remap_flags);
	if (ret < 0 || len == 0)
		goto err;

	aligned_len = round_up((u64) len, block_bytes(c));

	ret = write_invalidate_inode_pages_range(dst->v.i_mapping,
				pos_dst, pos_dst + len - 1);
	if (ret)
		goto err;

	ret = quota_reserve_range(dst, &quota_res, pos_dst >> 9,
				  (pos_dst + aligned_len) >> 9);
	if (ret)
		goto err;

	file_update_time(file_dst);

	mark_pagecache_unallocated(src, pos_src >> 9,
				   (pos_src + aligned_len) >> 9);

	ret = bch2_remap_range(c,
			       inode_inum(dst), pos_dst >> 9,
			       inode_inum(src), pos_src >> 9,
			       aligned_len >> 9,
			       pos_dst + len, &i_sectors_delta);
	if (ret < 0)
		goto err;

	/*
	 * due to alignment, we might have remapped slightly more than requsted
	 */
	ret = min((u64) ret << 9, (u64) len);

	i_sectors_acct(c, dst, &quota_res, i_sectors_delta);

	spin_lock(&dst->v.i_lock);
	if (pos_dst + ret > dst->v.i_size)
		i_size_write(&dst->v, pos_dst + ret);
	spin_unlock(&dst->v.i_lock);

	if ((file_dst->f_flags & (__O_SYNC | O_DSYNC)) ||
	    IS_SYNC(file_inode(file_dst)))
		ret = bch2_flush_inode(c, dst);
err:
	bch2_quota_reservation_put(c, dst, &quota_res);
	bch2_unlock_inodes(INODE_LOCK|INODE_PAGECACHE_BLOCK, src, dst);

	return bch2_err_class(ret);
}

/* fseek: */

static int folio_data_offset(struct folio *folio, loff_t pos)
{
	struct bch_folio *s = bch2_folio(folio);
	unsigned i, sectors = folio_sectors(folio);

	if (s)
		for (i = folio_pos_to_s(folio, pos); i < sectors; i++)
			if (s->s[i].state >= SECTOR_dirty)
				return i << SECTOR_SHIFT;

	return -1;
}

static loff_t bch2_seek_pagecache_data(struct inode *vinode,
				       loff_t start_offset,
				       loff_t end_offset)
{
	struct folio_batch fbatch;
	pgoff_t start_index	= start_offset >> PAGE_SHIFT;
	pgoff_t end_index	= end_offset >> PAGE_SHIFT;
	pgoff_t index		= start_index;
	unsigned i;
	loff_t ret;
	int offset;

	folio_batch_init(&fbatch);

	while (filemap_get_folios(vinode->i_mapping,
				  &index, end_index, &fbatch)) {
		for (i = 0; i < folio_batch_count(&fbatch); i++) {
			struct folio *folio = fbatch.folios[i];

			folio_lock(folio);
			offset = folio_data_offset(folio,
					max(folio_pos(folio), start_offset));
			if (offset >= 0) {
				ret = clamp(folio_pos(folio) + offset,
					    start_offset, end_offset);
				folio_unlock(folio);
				folio_batch_release(&fbatch);
				return ret;
			}
			folio_unlock(folio);
		}
		folio_batch_release(&fbatch);
		cond_resched();
	}

	return end_offset;
}

static loff_t bch2_seek_data(struct file *file, u64 offset)
{
	struct bch_inode_info *inode = file_bch_inode(file);
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	struct btree_trans trans;
	struct btree_iter iter;
	struct bkey_s_c k;
	subvol_inum inum = inode_inum(inode);
	u64 isize, next_data = MAX_LFS_FILESIZE;
	u32 snapshot;
	int ret;

	isize = i_size_read(&inode->v);
	if (offset >= isize)
		return -ENXIO;

	bch2_trans_init(&trans, c, 0, 0);
retry:
	bch2_trans_begin(&trans);

	ret = bch2_subvolume_get_snapshot(&trans, inum.subvol, &snapshot);
	if (ret)
		goto err;

	for_each_btree_key_upto_norestart(&trans, iter, BTREE_ID_extents,
			   SPOS(inode->v.i_ino, offset >> 9, snapshot),
			   POS(inode->v.i_ino, U64_MAX),
			   0, k, ret) {
		if (bkey_extent_is_data(k.k)) {
			next_data = max(offset, bkey_start_offset(k.k) << 9);
			break;
		} else if (k.k->p.offset >> 9 > isize)
			break;
	}
	bch2_trans_iter_exit(&trans, &iter);
err:
	if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
		goto retry;

	bch2_trans_exit(&trans);
	if (ret)
		return ret;

	if (next_data > offset)
		next_data = bch2_seek_pagecache_data(&inode->v,
						     offset, next_data);

	if (next_data >= isize)
		return -ENXIO;

	return vfs_setpos(file, next_data, MAX_LFS_FILESIZE);
}

static bool folio_hole_offset(struct address_space *mapping, loff_t *offset)
{
	struct folio *folio;
	struct bch_folio *s;
	unsigned i, sectors;
	bool ret = true;

	folio = filemap_lock_folio(mapping, *offset >> PAGE_SHIFT);
	if (!folio)
		return true;

	s = bch2_folio(folio);
	if (!s)
		goto unlock;

	sectors = folio_sectors(folio);
	for (i = folio_pos_to_s(folio, *offset); i < sectors; i++)
		if (s->s[i].state < SECTOR_dirty) {
			*offset = max(*offset,
				      folio_pos(folio) + (i << SECTOR_SHIFT));
			goto unlock;
		}

	*offset = folio_end_pos(folio);
	ret = false;
unlock:
	folio_unlock(folio);
	return ret;
}

static loff_t bch2_seek_pagecache_hole(struct inode *vinode,
				       loff_t start_offset,
				       loff_t end_offset)
{
	struct address_space *mapping = vinode->i_mapping;
	loff_t offset = start_offset;

	while (offset < end_offset &&
	       !folio_hole_offset(mapping, &offset))
		;

	return min(offset, end_offset);
}

static loff_t bch2_seek_hole(struct file *file, u64 offset)
{
	struct bch_inode_info *inode = file_bch_inode(file);
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	struct btree_trans trans;
	struct btree_iter iter;
	struct bkey_s_c k;
	subvol_inum inum = inode_inum(inode);
	u64 isize, next_hole = MAX_LFS_FILESIZE;
	u32 snapshot;
	int ret;

	isize = i_size_read(&inode->v);
	if (offset >= isize)
		return -ENXIO;

	bch2_trans_init(&trans, c, 0, 0);
retry:
	bch2_trans_begin(&trans);

	ret = bch2_subvolume_get_snapshot(&trans, inum.subvol, &snapshot);
	if (ret)
		goto err;

	for_each_btree_key_norestart(&trans, iter, BTREE_ID_extents,
			   SPOS(inode->v.i_ino, offset >> 9, snapshot),
			   BTREE_ITER_SLOTS, k, ret) {
		if (k.k->p.inode != inode->v.i_ino) {
			next_hole = bch2_seek_pagecache_hole(&inode->v,
					offset, MAX_LFS_FILESIZE);
			break;
		} else if (!bkey_extent_is_data(k.k)) {
			next_hole = bch2_seek_pagecache_hole(&inode->v,
					max(offset, bkey_start_offset(k.k) << 9),
					k.k->p.offset << 9);

			if (next_hole < k.k->p.offset << 9)
				break;
		} else {
			offset = max(offset, bkey_start_offset(k.k) << 9);
		}
	}
	bch2_trans_iter_exit(&trans, &iter);
err:
	if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
		goto retry;

	bch2_trans_exit(&trans);
	if (ret)
		return ret;

	if (next_hole > isize)
		next_hole = isize;

	return vfs_setpos(file, next_hole, MAX_LFS_FILESIZE);
}

loff_t bch2_llseek(struct file *file, loff_t offset, int whence)
{
	loff_t ret;

	switch (whence) {
	case SEEK_SET:
	case SEEK_CUR:
	case SEEK_END:
		ret = generic_file_llseek(file, offset, whence);
		break;
	case SEEK_DATA:
		ret = bch2_seek_data(file, offset);
		break;
	case SEEK_HOLE:
		ret = bch2_seek_hole(file, offset);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return bch2_err_class(ret);
}

void bch2_fs_fsio_exit(struct bch_fs *c)
{
	bioset_exit(&c->nocow_flush_bioset);
	bioset_exit(&c->dio_write_bioset);
	bioset_exit(&c->dio_read_bioset);
	bioset_exit(&c->writepage_bioset);
}

int bch2_fs_fsio_init(struct bch_fs *c)
{
	int ret = 0;

	pr_verbose_init(c->opts, "");

	if (bioset_init(&c->writepage_bioset,
			4, offsetof(struct bch_writepage_io, op.wbio.bio),
			BIOSET_NEED_BVECS))
		return -BCH_ERR_ENOMEM_writepage_bioset_init;

	if (bioset_init(&c->dio_read_bioset,
			4, offsetof(struct dio_read, rbio.bio),
			BIOSET_NEED_BVECS))
		return -BCH_ERR_ENOMEM_dio_read_bioset_init;

	if (bioset_init(&c->dio_write_bioset,
			4, offsetof(struct dio_write, op.wbio.bio),
			BIOSET_NEED_BVECS))
		return -BCH_ERR_ENOMEM_dio_write_bioset_init;

	if (bioset_init(&c->nocow_flush_bioset,
			1, offsetof(struct nocow_flush, bio), 0))
		return -BCH_ERR_ENOMEM_nocow_flush_bioset_init;

	pr_verbose_init(c->opts, "ret %i", ret);
	return ret;
}

#endif /* NO_BCACHEFS_FS */
