// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022, Alibaba Cloud
 */
#include <linux/fscache.h>
#include "internal.h"

static struct netfs_io_request *erofs_fscache_alloc_request(struct address_space *mapping,
					     loff_t start, size_t len)
{
	struct netfs_io_request *rreq;

	rreq = kzalloc(sizeof(struct netfs_io_request), GFP_KERNEL);
	if (!rreq)
		return ERR_PTR(-ENOMEM);

	rreq->start	= start;
	rreq->len	= len;
	rreq->mapping	= mapping;
	rreq->inode	= mapping->host;
	INIT_LIST_HEAD(&rreq->subrequests);
	refcount_set(&rreq->ref, 1);
	return rreq;
}

static void erofs_fscache_put_request(struct netfs_io_request *rreq)
{
	if (!refcount_dec_and_test(&rreq->ref))
		return;
	if (rreq->cache_resources.ops)
		rreq->cache_resources.ops->end_operation(&rreq->cache_resources);
	kfree(rreq);
}

static void erofs_fscache_put_subrequest(struct netfs_io_subrequest *subreq)
{
	if (!refcount_dec_and_test(&subreq->ref))
		return;
	erofs_fscache_put_request(subreq->rreq);
	kfree(subreq);
}

static void erofs_fscache_clear_subrequests(struct netfs_io_request *rreq)
{
	struct netfs_io_subrequest *subreq;

	while (!list_empty(&rreq->subrequests)) {
		subreq = list_first_entry(&rreq->subrequests,
				struct netfs_io_subrequest, rreq_link);
		list_del(&subreq->rreq_link);
		erofs_fscache_put_subrequest(subreq);
	}
}

static void erofs_fscache_rreq_unlock_folios(struct netfs_io_request *rreq)
{
	struct netfs_io_subrequest *subreq;
	struct folio *folio;
	unsigned int iopos = 0;
	pgoff_t start_page = rreq->start / PAGE_SIZE;
	pgoff_t last_page = ((rreq->start + rreq->len) / PAGE_SIZE) - 1;
	bool subreq_failed = false;

	XA_STATE(xas, &rreq->mapping->i_pages, start_page);

	subreq = list_first_entry(&rreq->subrequests,
				  struct netfs_io_subrequest, rreq_link);
	subreq_failed = (subreq->error < 0);

	rcu_read_lock();
	xas_for_each(&xas, folio, last_page) {
		unsigned int pgpos =
			(folio_index(folio) - start_page) * PAGE_SIZE;
		unsigned int pgend = pgpos + folio_size(folio);
		bool pg_failed = false;

		for (;;) {
			if (!subreq) {
				pg_failed = true;
				break;
			}

			pg_failed |= subreq_failed;
			if (pgend < iopos + subreq->len)
				break;

			iopos += subreq->len;
			if (!list_is_last(&subreq->rreq_link,
					  &rreq->subrequests)) {
				subreq = list_next_entry(subreq, rreq_link);
				subreq_failed = (subreq->error < 0);
			} else {
				subreq = NULL;
				subreq_failed = false;
			}
			if (pgend == iopos)
				break;
		}

		if (!pg_failed)
			folio_mark_uptodate(folio);

		folio_unlock(folio);
	}
	rcu_read_unlock();
}

static void erofs_fscache_rreq_complete(struct netfs_io_request *rreq)
{
	erofs_fscache_rreq_unlock_folios(rreq);
	erofs_fscache_clear_subrequests(rreq);
	erofs_fscache_put_request(rreq);
}

static void erofc_fscache_subreq_complete(void *priv,
		ssize_t transferred_or_error, bool was_async)
{
	struct netfs_io_subrequest *subreq = priv;
	struct netfs_io_request *rreq = subreq->rreq;

	if (IS_ERR_VALUE(transferred_or_error))
		subreq->error = transferred_or_error;

	if (atomic_dec_and_test(&rreq->nr_outstanding))
		erofs_fscache_rreq_complete(rreq);

	erofs_fscache_put_subrequest(subreq);
}

/*
 * Read data from fscache and fill the read data into page cache described by
 * @rreq, which shall be both aligned with PAGE_SIZE. @pstart describes
 * the start physical address in the cache file.
 */
static int erofs_fscache_read_folios_async(struct fscache_cookie *cookie,
				struct netfs_io_request *rreq, loff_t pstart)
{
	enum netfs_io_source source;
	struct super_block *sb = rreq->mapping->host->i_sb;
	struct netfs_io_subrequest *subreq;
	struct netfs_cache_resources *cres = &rreq->cache_resources;
	struct iov_iter iter;
	loff_t start = rreq->start;
	size_t len = rreq->len;
	size_t done = 0;
	int ret;

	atomic_set(&rreq->nr_outstanding, 1);

	ret = fscache_begin_read_operation(cres, cookie);
	if (ret)
		goto out;

	while (done < len) {
		subreq = kzalloc(sizeof(struct netfs_io_subrequest),
				 GFP_KERNEL);
		if (subreq) {
			INIT_LIST_HEAD(&subreq->rreq_link);
			refcount_set(&subreq->ref, 2);
			subreq->rreq = rreq;
			refcount_inc(&rreq->ref);
		} else {
			ret = -ENOMEM;
			goto out;
		}

		subreq->start = pstart + done;
		subreq->len	=  len - done;
		subreq->flags = 1 << NETFS_SREQ_ONDEMAND;

		list_add_tail(&subreq->rreq_link, &rreq->subrequests);

		source = cres->ops->prepare_read(subreq, LLONG_MAX);
		if (WARN_ON(subreq->len == 0))
			source = NETFS_INVALID_READ;
		if (source != NETFS_READ_FROM_CACHE) {
			erofs_err(sb, "failed to fscache prepare_read (source %d)",
				  source);
			ret = -EIO;
			subreq->error = ret;
			erofs_fscache_put_subrequest(subreq);
			goto out;
		}

		atomic_inc(&rreq->nr_outstanding);

		iov_iter_xarray(&iter, READ, &rreq->mapping->i_pages,
				start + done, subreq->len);

		ret = fscache_read(cres, subreq->start, &iter,
				   NETFS_READ_HOLE_FAIL,
				   erofc_fscache_subreq_complete, subreq);
		if (ret == -EIOCBQUEUED)
			ret = 0;
		if (ret) {
			erofs_err(sb, "failed to fscache_read (ret %d)", ret);
			goto out;
		}

		done += subreq->len;
	}
out:
	if (atomic_dec_and_test(&rreq->nr_outstanding))
		erofs_fscache_rreq_complete(rreq);

	return ret;
}

static int erofs_fscache_meta_read_folio(struct file *data, struct folio *folio)
{
	int ret;
	struct super_block *sb = folio_mapping(folio)->host->i_sb;
	struct netfs_io_request *rreq;
	struct erofs_map_dev mdev = {
		.m_deviceid = 0,
		.m_pa = folio_pos(folio),
	};

	ret = erofs_map_dev(sb, &mdev);
	if (ret)
		goto out;

	rreq = erofs_fscache_alloc_request(folio_mapping(folio),
				folio_pos(folio), folio_size(folio));
	if (IS_ERR(rreq))
		goto out;

	return erofs_fscache_read_folios_async(mdev.m_fscache->cookie,
				rreq, mdev.m_pa);
out:
	folio_unlock(folio);
	return ret;
}

static int erofs_fscache_read_folio_inline(struct folio *folio,
					 struct erofs_map_blocks *map)
{
	struct super_block *sb = folio_mapping(folio)->host->i_sb;
	struct erofs_buf buf = __EROFS_BUF_INITIALIZER;
	erofs_blk_t blknr;
	size_t offset, len;
	void *src, *dst;

	/* For tail packing layout, the offset may be non-zero. */
	offset = erofs_blkoff(map->m_pa);
	blknr = erofs_blknr(map->m_pa);
	len = map->m_llen;

	src = erofs_read_metabuf(&buf, sb, blknr, EROFS_KMAP);
	if (IS_ERR(src))
		return PTR_ERR(src);

	dst = kmap_local_folio(folio, 0);
	memcpy(dst, src + offset, len);
	memset(dst + len, 0, PAGE_SIZE - len);
	kunmap_local(dst);

	erofs_put_metabuf(&buf);
	return 0;
}

static int erofs_fscache_read_folio(struct file *file, struct folio *folio)
{
	struct inode *inode = folio_mapping(folio)->host;
	struct super_block *sb = inode->i_sb;
	struct erofs_map_blocks map;
	struct erofs_map_dev mdev;
	struct netfs_io_request *rreq;
	erofs_off_t pos;
	loff_t pstart;
	int ret;

	DBG_BUGON(folio_size(folio) != EROFS_BLKSIZ);

	pos = folio_pos(folio);
	map.m_la = pos;

	ret = erofs_map_blocks(inode, &map, EROFS_GET_BLOCKS_RAW);
	if (ret)
		goto out_unlock;

	if (!(map.m_flags & EROFS_MAP_MAPPED)) {
		folio_zero_range(folio, 0, folio_size(folio));
		goto out_uptodate;
	}

	if (map.m_flags & EROFS_MAP_META) {
		ret = erofs_fscache_read_folio_inline(folio, &map);
		goto out_uptodate;
	}

	mdev = (struct erofs_map_dev) {
		.m_deviceid = map.m_deviceid,
		.m_pa = map.m_pa,
	};

	ret = erofs_map_dev(sb, &mdev);
	if (ret)
		goto out_unlock;


	rreq = erofs_fscache_alloc_request(folio_mapping(folio),
				folio_pos(folio), folio_size(folio));
	if (IS_ERR(rreq))
		goto out_unlock;

	pstart = mdev.m_pa + (pos - map.m_la);
	return erofs_fscache_read_folios_async(mdev.m_fscache->cookie,
				rreq, pstart);

out_uptodate:
	if (!ret)
		folio_mark_uptodate(folio);
out_unlock:
	folio_unlock(folio);
	return ret;
}

static void erofs_fscache_advance_folios(struct readahead_control *rac,
					 size_t len, bool unlock)
{
	while (len) {
		struct folio *folio = readahead_folio(rac);
		len -= folio_size(folio);
		if (unlock) {
			folio_mark_uptodate(folio);
			folio_unlock(folio);
		}
	}
}

static void erofs_fscache_readahead(struct readahead_control *rac)
{
	struct inode *inode = rac->mapping->host;
	struct super_block *sb = inode->i_sb;
	size_t len, count, done = 0;
	erofs_off_t pos;
	loff_t start, offset;
	int ret;

	if (!readahead_count(rac))
		return;

	start = readahead_pos(rac);
	len = readahead_length(rac);

	do {
		struct erofs_map_blocks map;
		struct erofs_map_dev mdev;
		struct netfs_io_request *rreq;

		pos = start + done;
		map.m_la = pos;

		ret = erofs_map_blocks(inode, &map, EROFS_GET_BLOCKS_RAW);
		if (ret)
			return;

		offset = start + done;
		count = min_t(size_t, map.m_llen - (pos - map.m_la),
			      len - done);

		if (!(map.m_flags & EROFS_MAP_MAPPED)) {
			struct iov_iter iter;

			iov_iter_xarray(&iter, READ, &rac->mapping->i_pages,
					offset, count);
			iov_iter_zero(count, &iter);

			erofs_fscache_advance_folios(rac, count, true);
			ret = count;
			continue;
		}

		if (map.m_flags & EROFS_MAP_META) {
			struct folio *folio = readahead_folio(rac);

			ret = erofs_fscache_read_folio_inline(folio, &map);
			if (!ret) {
				folio_mark_uptodate(folio);
				ret = folio_size(folio);
			}

			folio_unlock(folio);
			continue;
		}

		mdev = (struct erofs_map_dev) {
			.m_deviceid = map.m_deviceid,
			.m_pa = map.m_pa,
		};
		ret = erofs_map_dev(sb, &mdev);
		if (ret)
			return;

		rreq = erofs_fscache_alloc_request(rac->mapping, offset, count);
		if (IS_ERR(rreq))
			return;
		/*
		 * Drop the ref of folios here. Unlock them in
		 * rreq_unlock_folios() when rreq complete.
		 */
		erofs_fscache_advance_folios(rac, count, false);
		ret = erofs_fscache_read_folios_async(mdev.m_fscache->cookie,
					rreq, mdev.m_pa + (pos - map.m_la));
		if (!ret)
			ret = count;
	} while (ret > 0 && ((done += ret) < len));
}

static const struct address_space_operations erofs_fscache_meta_aops = {
	.read_folio = erofs_fscache_meta_read_folio,
};

const struct address_space_operations erofs_fscache_access_aops = {
	.read_folio = erofs_fscache_read_folio,
	.readahead = erofs_fscache_readahead,
};

int erofs_fscache_register_cookie(struct super_block *sb,
				  struct erofs_fscache **fscache,
				  char *name, bool need_inode)
{
	struct fscache_volume *volume = EROFS_SB(sb)->volume;
	struct erofs_fscache *ctx;
	struct fscache_cookie *cookie;
	int ret;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	cookie = fscache_acquire_cookie(volume, FSCACHE_ADV_WANT_CACHE_SIZE,
					name, strlen(name), NULL, 0, 0);
	if (!cookie) {
		erofs_err(sb, "failed to get cookie for %s", name);
		ret = -EINVAL;
		goto err;
	}

	fscache_use_cookie(cookie, false);
	ctx->cookie = cookie;

	if (need_inode) {
		struct inode *const inode = new_inode(sb);

		if (!inode) {
			erofs_err(sb, "failed to get anon inode for %s", name);
			ret = -ENOMEM;
			goto err_cookie;
		}

		set_nlink(inode, 1);
		inode->i_size = OFFSET_MAX;
		inode->i_mapping->a_ops = &erofs_fscache_meta_aops;
		mapping_set_gfp_mask(inode->i_mapping, GFP_NOFS);

		ctx->inode = inode;
	}

	*fscache = ctx;
	return 0;

err_cookie:
	fscache_unuse_cookie(ctx->cookie, NULL, NULL);
	fscache_relinquish_cookie(ctx->cookie, false);
	ctx->cookie = NULL;
err:
	kfree(ctx);
	return ret;
}

void erofs_fscache_unregister_cookie(struct erofs_fscache **fscache)
{
	struct erofs_fscache *ctx = *fscache;

	if (!ctx)
		return;

	fscache_unuse_cookie(ctx->cookie, NULL, NULL);
	fscache_relinquish_cookie(ctx->cookie, false);
	ctx->cookie = NULL;

	iput(ctx->inode);
	ctx->inode = NULL;

	kfree(ctx);
	*fscache = NULL;
}

int erofs_fscache_register_fs(struct super_block *sb)
{
	struct erofs_sb_info *sbi = EROFS_SB(sb);
	struct fscache_volume *volume;
	char *name;
	int ret = 0;

	name = kasprintf(GFP_KERNEL, "erofs,%s", sbi->opt.fsid);
	if (!name)
		return -ENOMEM;

	volume = fscache_acquire_volume(name, NULL, NULL, 0);
	if (IS_ERR_OR_NULL(volume)) {
		erofs_err(sb, "failed to register volume for %s", name);
		ret = volume ? PTR_ERR(volume) : -EOPNOTSUPP;
		volume = NULL;
	}

	sbi->volume = volume;
	kfree(name);
	return ret;
}

void erofs_fscache_unregister_fs(struct super_block *sb)
{
	struct erofs_sb_info *sbi = EROFS_SB(sb);

	fscache_relinquish_volume(sbi->volume, NULL, false);
	sbi->volume = NULL;
}
