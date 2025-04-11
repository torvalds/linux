// SPDX-License-Identifier: GPL-2.0
#ifndef NO_BCACHEFS_FS

#include "bcachefs.h"
#include "alloc_foreground.h"
#include "bkey_buf.h"
#include "fs-io.h"
#include "fs-io-buffered.h"
#include "fs-io-direct.h"
#include "fs-io-pagecache.h"
#include "io_read.h"
#include "io_write.h"

#include <linux/backing-dev.h>
#include <linux/pagemap.h>
#include <linux/writeback.h>

static inline bool bio_full(struct bio *bio, unsigned len)
{
	if (bio->bi_vcnt >= bio->bi_max_vecs)
		return true;
	if (bio->bi_iter.bi_size > UINT_MAX - len)
		return true;
	return false;
}

/* readpage(s): */

static void bch2_readpages_end_io(struct bio *bio)
{
	struct folio_iter fi;

	bio_for_each_folio_all(fi, bio)
		folio_end_read(fi.folio, bio->bi_status == BLK_STS_OK);

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
	struct folio *folio;

	*iter = (struct readpages_iter) { ractl->mapping };

	while ((folio = __readahead_folio(ractl))) {
		if (!bch2_folio_create(folio, GFP_KERNEL) ||
		    darray_push(&iter->folios, folio)) {
			bch2_folio_release(folio);
			ractl->_nr_pages += folio_nr_pages(folio);
			ractl->_index -= folio_nr_pages(folio);
			return iter->folios.nr ? 0 : -ENOMEM;
		}

		folio_put(folio);
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

			unsigned sectors_remaining = sectors_this_extent - bio_sectors(bio);

			if (sectors_remaining < PAGE_SECTORS << mapping_min_folio_order(iter->mapping))
				break;

			unsigned order = ilog2(rounddown_pow_of_two(sectors_remaining) / PAGE_SECTORS);

			/* ensure proper alignment */
			order = min(order, __ffs(folio_offset|BIT(31)));

			folio = xa_load(&iter->mapping->i_pages, folio_offset);
			if (folio && !xa_is_value(folio))
				break;

			folio = filemap_alloc_folio(readahead_gfp_mask(iter->mapping), order);
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
	int flags = BCH_READ_retry_if_stale|
		BCH_READ_may_promote;
	int ret = 0;

	rbio->subvol = inum.subvol;

	bch2_bkey_buf_init(&sk);
	bch2_trans_begin(trans);
	bch2_trans_iter_init(trans, &iter, BTREE_ID_extents,
			     POS(inum.inum, rbio->bio.bi_iter.bi_sector),
			     BTREE_ITER_slots);
	while (1) {
		struct bkey_s_c k;
		unsigned bytes, sectors;
		s64 offset_into_extent;
		enum btree_id data_btree = BTREE_ID_extents;

		bch2_trans_begin(trans);

		u32 snapshot;
		ret = bch2_subvolume_get_snapshot(trans, inum.subvol, &snapshot);
		if (ret)
			goto err;

		bch2_btree_iter_set_snapshot(trans, &iter, snapshot);

		bch2_btree_iter_set_pos(trans, &iter,
				POS(inum.inum, rbio->bio.bi_iter.bi_sector));

		k = bch2_btree_iter_peek_slot(trans, &iter);
		ret = bkey_err(k);
		if (ret)
			goto err;

		offset_into_extent = iter.pos.offset -
			bkey_start_offset(k.k);
		sectors = k.k->size - offset_into_extent;

		bch2_bkey_buf_reassemble(&sk, c, k);

		ret = bch2_read_indirect_extent(trans, &data_btree,
					&offset_into_extent, &sk);
		if (ret)
			goto err;

		k = bkey_i_to_s_c(sk.k);

		sectors = min_t(unsigned, sectors, k.k->size - offset_into_extent);

		if (readpages_iter) {
			ret = readpage_bio_extend(trans, readpages_iter, &rbio->bio, sectors,
						  extent_partial_reads_expensive(k));
			if (ret)
				goto err;
		}

		bytes = min(sectors, bio_sectors(&rbio->bio)) << 9;
		swap(rbio->bio.bi_iter.bi_size, bytes);

		if (rbio->bio.bi_iter.bi_size == bytes)
			flags |= BCH_READ_last_fragment;

		bch2_bio_page_state_set(&rbio->bio, k);

		bch2_read_extent(trans, rbio, iter.pos,
				 data_btree, k, offset_into_extent, flags);
		swap(rbio->bio.bi_iter.bi_size, bytes);

		if (flags & BCH_READ_last_fragment)
			break;

		bio_advance(&rbio->bio, bytes);
err:
		if (ret &&
		    !bch2_err_matches(ret, BCH_ERR_transaction_restart))
			break;
	}
	bch2_trans_iter_exit(trans, &iter);

	if (ret) {
		struct printbuf buf = PRINTBUF;
		lockrestart_do(trans,
			bch2_inum_offset_err_msg_trans(trans, &buf, inum, iter.pos.offset << 9));
		prt_printf(&buf, "read error %i from btree lookup", ret);
		bch_err_ratelimited(c, "%s", buf.buf);
		printbuf_exit(&buf);

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
	struct folio *folio;
	struct readpages_iter readpages_iter;
	struct blk_plug plug;

	bch2_inode_opts_get(&opts, c, &inode->ei_inode);

	int ret = readpages_iter_init(&readpages_iter, ractl);
	if (ret)
		return;

	/*
	 * Besides being a general performance optimization, plugging helps with
	 * avoiding btree transaction srcu warnings - submitting a bio can
	 * block, and we don't want todo that with the transaction locked.
	 *
	 * However, plugged bios are submitted when we schedule; we ideally
	 * would have our own scheduler hook to call unlock_long() before
	 * scheduling.
	 */
	blk_start_plug(&plug);
	bch2_pagecache_add_get(inode);

	struct btree_trans *trans = bch2_trans_get(c);
	while ((folio = readpage_iter_peek(&readpages_iter))) {
		unsigned n = min_t(unsigned,
				   readpages_iter.folios.nr -
				   readpages_iter.idx,
				   BIO_MAX_VECS);
		struct bch_read_bio *rbio =
			rbio_init(bio_alloc_bioset(NULL, n, REQ_OP_READ,
						   GFP_KERNEL, &c->bio_read),
				  c,
				  opts,
				  bch2_readpages_end_io);

		readpage_iter_advance(&readpages_iter);

		rbio->bio.bi_iter.bi_sector = folio_sector(folio);
		BUG_ON(!bio_add_folio(&rbio->bio, folio, folio_size(folio), 0));

		bchfs_read(trans, rbio, inode_inum(inode),
			   &readpages_iter);
		bch2_trans_unlock(trans);
	}
	bch2_trans_put(trans);

	bch2_pagecache_add_put(inode);
	blk_finish_plug(&plug);
	darray_exit(&readpages_iter.folios);
}

static void bch2_read_single_folio_end_io(struct bio *bio)
{
	complete(bio->bi_private);
}

int bch2_read_single_folio(struct folio *folio, struct address_space *mapping)
{
	struct bch_inode_info *inode = to_bch_ei(mapping->host);
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	struct bch_read_bio *rbio;
	struct bch_io_opts opts;
	struct blk_plug plug;
	int ret;
	DECLARE_COMPLETION_ONSTACK(done);

	BUG_ON(folio_test_uptodate(folio));
	BUG_ON(folio_test_dirty(folio));

	if (!bch2_folio_create(folio, GFP_KERNEL))
		return -ENOMEM;

	bch2_inode_opts_get(&opts, c, &inode->ei_inode);

	rbio = rbio_init(bio_alloc_bioset(NULL, 1, REQ_OP_READ, GFP_KERNEL, &c->bio_read),
			 c,
			 opts,
			 bch2_read_single_folio_end_io);
	rbio->bio.bi_private = &done;
	rbio->bio.bi_opf = REQ_OP_READ|REQ_SYNC;
	rbio->bio.bi_iter.bi_sector = folio_sector(folio);
	BUG_ON(!bio_add_folio(&rbio->bio, folio, folio_size(folio), 0));

	blk_start_plug(&plug);
	bch2_trans_run(c, (bchfs_read(trans, rbio, inode_inum(inode), NULL), 0));
	blk_finish_plug(&plug);
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

struct bch_writepage_io {
	struct bch_inode_info		*inode;

	/* must be last: */
	struct bch_write_op		op;
};

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

/*
 * Determine when a writepage io is full. We have to limit writepage bios to a
 * single page per bvec (i.e. 1MB with 4k pages) because that is the limit to
 * what the bounce path in bch2_write_extent() can handle. In theory we could
 * loosen this restriction for non-bounce I/O, but we don't have that context
 * here. Ideally, we can up this limit and make it configurable in the future
 * when the bounce path can be enhanced to accommodate larger source bios.
 */
static inline bool bch_io_full(struct bch_writepage_io *io, unsigned len)
{
	struct bio *bio = &io->op.wbio.bio;
	return bio_full(bio, len) ||
		(bio->bi_iter.bi_size + len > BIO_MAX_VECS * PAGE_SIZE);
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

			mapping_set_error(fi.folio->mapping, -EIO);

			s = __bch2_folio(fi.folio);
			spin_lock(&s->lock);
			for (i = 0; i < folio_sectors(fi.folio); i++)
				s->s[i].nr_replicas = 0;
			spin_unlock(&s->lock);
		}
	}

	if (io->op.flags & BCH_WRITE_wrote_data_inline) {
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
	 * The writeback flag is effectively our ref on the inode -
	 * fixup i_blocks before calling folio_end_writeback:
	 */
	bch2_i_sectors_acct(c, io->inode, NULL, io->op.i_sectors_delta);

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
	op->subvol		= inode->ei_inum.subvol;
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
		w->tmp = kcalloc(f_sectors, sizeof(struct bch_folio_sector), GFP_NOFS|__GFP_NOFAIL);
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
		bch2_folio_sector_set(folio, s, i, SECTOR_allocated);
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
		     bch_io_full(w->io, sectors << 9) ||
		     bio_end_sector(&w->io->op.wbio.bio) != sector))
			bch2_writepage_do_io(w);

		if (!w->io)
			bch2_writepage_io_alloc(c, wbc, w, inode, sector,
						nr_replicas_this_write);

		atomic_inc(&s->write_count);

		BUG_ON(inode != w->io->inode);
		BUG_ON(!bio_add_folio(&w->io->op.wbio.bio, folio,
				     sectors << 9, offset << 9));

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
		     struct folio **foliop, void **fsdata)
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
				    FGP_WRITEBEGIN | fgf_set_order(len),
				    mapping_gfp_mask(mapping));
	if (IS_ERR(folio))
		goto err_unlock;

	offset = pos - folio_pos(folio);
	len = min_t(size_t, len, folio_end_pos(folio) - pos);

	if (folio_test_uptodate(folio))
		goto out;

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

	*foliop = folio;
	return 0;
err:
	folio_unlock(folio);
	folio_put(folio);
err_unlock:
	bch2_pagecache_add_put(inode);
	kfree(res);
	*fsdata = NULL;
	return bch2_err_class(ret);
}

int bch2_write_end(struct file *file, struct address_space *mapping,
		   loff_t pos, unsigned len, unsigned copied,
		   struct folio *folio, void *fsdata)
{
	struct bch_inode_info *inode = to_bch_ei(mapping->host);
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	struct bch2_folio_reservation *res = fsdata;
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

static noinline void folios_trunc(folios *fs, struct folio **fi)
{
	while (fs->data + fs->nr > fi) {
		struct folio *f = darray_pop(fs);

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
	folios fs;
	struct folio *f;
	unsigned copied = 0, f_offset, f_copied;
	u64 end = pos + len, f_pos, f_len;
	loff_t last_folio_pos = inode->v.i_size;
	int ret = 0;

	BUG_ON(!len);

	bch2_folio_reservation_init(c, inode, &res);
	darray_init(&fs);

	ret = bch2_filemap_get_contig_folios_d(mapping, pos, end,
					       FGP_WRITEBEGIN | fgf_set_order(len),
					       mapping_gfp_mask(mapping), &fs);
	if (ret)
		goto out;

	BUG_ON(!fs.nr);

	f = darray_first(fs);
	if (pos != folio_pos(f) && !folio_test_uptodate(f)) {
		ret = bch2_read_single_folio(f, mapping);
		if (ret)
			goto out;
	}

	f = darray_last(fs);
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

	ret = bch2_folio_set(c, inode_inum(inode), fs.data, fs.nr);
	if (ret)
		goto out;

	f_pos = pos;
	f_offset = pos - folio_pos(darray_first(fs));
	darray_for_each(fs, fi) {
		ssize_t f_reserved;

		f = *fi;
		f_len = min(end, folio_end_pos(f)) - f_pos;
		f_reserved = bch2_folio_reservation_get_partial(c, inode, f, &res, f_offset, f_len);

		if (unlikely(f_reserved != f_len)) {
			if (f_reserved < 0) {
				if (f == darray_first(fs)) {
					ret = f_reserved;
					goto out;
				}

				folios_trunc(&fs, fi);
				end = min(end, folio_end_pos(darray_last(fs)));
			} else {
				if (!folio_test_uptodate(f)) {
					ret = bch2_read_single_folio(f, mapping);
					if (ret)
						goto out;
				}

				folios_trunc(&fs, fi + 1);
				end = f_pos + f_reserved;
			}

			break;
		}

		f_pos = folio_end_pos(f);
		f_offset = 0;
	}

	if (mapping_writably_mapped(mapping))
		darray_for_each(fs, fi)
			flush_dcache_folio(*fi);

	f_pos = pos;
	f_offset = pos - folio_pos(darray_first(fs));
	darray_for_each(fs, fi) {
		f = *fi;
		f_len = min(end, folio_end_pos(f)) - f_pos;
		f_copied = copy_folio_from_iter_atomic(f, f_offset, f_len, iter);
		if (!f_copied) {
			folios_trunc(&fs, fi);
			break;
		}

		if (!folio_test_uptodate(f) &&
		    f_copied != folio_size(f) &&
		    pos + copied + f_copied < inode->v.i_size) {
			iov_iter_revert(iter, f_copied);
			folio_zero_range(f, 0, folio_size(f));
			folios_trunc(&fs, fi);
			break;
		}

		flush_dcache_folio(f);
		copied += f_copied;

		if (f_copied != f_len) {
			folios_trunc(&fs, fi + 1);
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
	f_offset = pos - folio_pos(darray_first(fs));
	darray_for_each(fs, fi) {
		f = *fi;
		f_len = min(end, folio_end_pos(f)) - f_pos;

		if (!folio_test_uptodate(f))
			folio_mark_uptodate(f);

		bch2_set_folio_dirty(c, inode, f, &res, f_offset, f_len);

		f_pos = folio_end_pos(f);
		f_offset = 0;
	}

	inode->ei_last_dirtied = (unsigned long) current;
out:
	darray_for_each(fs, fi) {
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

	darray_exit(&fs);
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

ssize_t bch2_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	struct bch_inode_info *inode = file_bch_inode(file);
	ssize_t ret;

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

void bch2_fs_fs_io_buffered_exit(struct bch_fs *c)
{
	bioset_exit(&c->writepage_bioset);
}

int bch2_fs_fs_io_buffered_init(struct bch_fs *c)
{
	if (bioset_init(&c->writepage_bioset,
			4, offsetof(struct bch_writepage_io, op.wbio.bio),
			BIOSET_NEED_BVECS))
		return -BCH_ERR_ENOMEM_writepage_bioset_init;

	return 0;
}

#endif /* NO_BCACHEFS_FS */
