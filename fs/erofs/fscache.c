// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022, Alibaba Cloud
 * Copyright (C) 2022, Bytedance Inc. All rights reserved.
 */
#include <linux/fscache.h>
#include "internal.h"

static DEFINE_MUTEX(erofs_domain_list_lock);
static DEFINE_MUTEX(erofs_domain_cookies_lock);
static LIST_HEAD(erofs_domain_list);
static struct vfsmount *erofs_pseudo_mnt;

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
	if (IS_ERR(rreq)) {
		ret = PTR_ERR(rreq);
		goto out;
	}

	return erofs_fscache_read_folios_async(mdev.m_fscache->cookie,
				rreq, mdev.m_pa);
out:
	folio_unlock(folio);
	return ret;
}

/*
 * Read into page cache in the range described by (@pos, @len).
 *
 * On return, the caller is responsible for page unlocking if the output @unlock
 * is true, or the callee will take this responsibility through netfs_io_request
 * interface.
 *
 * The return value is the number of bytes successfully handled, or negative
 * error code on failure. The only exception is that, the length of the range
 * instead of the error code is returned on failure after netfs_io_request is
 * allocated, so that .readahead() could advance rac accordingly.
 */
static int erofs_fscache_data_read(struct address_space *mapping,
				   loff_t pos, size_t len, bool *unlock)
{
	struct inode *inode = mapping->host;
	struct super_block *sb = inode->i_sb;
	struct netfs_io_request *rreq;
	struct erofs_map_blocks map;
	struct erofs_map_dev mdev;
	struct iov_iter iter;
	size_t count;
	int ret;

	*unlock = true;

	map.m_la = pos;
	ret = erofs_map_blocks(inode, &map, EROFS_GET_BLOCKS_RAW);
	if (ret)
		return ret;

	if (map.m_flags & EROFS_MAP_META) {
		struct erofs_buf buf = __EROFS_BUF_INITIALIZER;
		erofs_blk_t blknr;
		size_t offset, size;
		void *src;

		/* For tail packing layout, the offset may be non-zero. */
		offset = erofs_blkoff(map.m_pa);
		blknr = erofs_blknr(map.m_pa);
		size = map.m_llen;

		src = erofs_read_metabuf(&buf, sb, blknr, EROFS_KMAP);
		if (IS_ERR(src))
			return PTR_ERR(src);

		iov_iter_xarray(&iter, READ, &mapping->i_pages, pos, PAGE_SIZE);
		if (copy_to_iter(src + offset, size, &iter) != size)
			return -EFAULT;
		iov_iter_zero(PAGE_SIZE - size, &iter);
		erofs_put_metabuf(&buf);
		return PAGE_SIZE;
	}

	count = min_t(size_t, map.m_llen - (pos - map.m_la), len);
	DBG_BUGON(!count || count % PAGE_SIZE);

	if (!(map.m_flags & EROFS_MAP_MAPPED)) {
		iov_iter_xarray(&iter, READ, &mapping->i_pages, pos, count);
		iov_iter_zero(count, &iter);
		return count;
	}

	mdev = (struct erofs_map_dev) {
		.m_deviceid = map.m_deviceid,
		.m_pa = map.m_pa,
	};
	ret = erofs_map_dev(sb, &mdev);
	if (ret)
		return ret;

	rreq = erofs_fscache_alloc_request(mapping, pos, count);
	if (IS_ERR(rreq))
		return PTR_ERR(rreq);

	*unlock = false;
	erofs_fscache_read_folios_async(mdev.m_fscache->cookie,
			rreq, mdev.m_pa + (pos - map.m_la));
	return count;
}

static int erofs_fscache_read_folio(struct file *file, struct folio *folio)
{
	bool unlock;
	int ret;

	DBG_BUGON(folio_size(folio) != EROFS_BLKSIZ);

	ret = erofs_fscache_data_read(folio_mapping(folio), folio_pos(folio),
				      folio_size(folio), &unlock);
	if (unlock) {
		if (ret > 0)
			folio_mark_uptodate(folio);
		folio_unlock(folio);
	}
	return ret < 0 ? ret : 0;
}

static void erofs_fscache_readahead(struct readahead_control *rac)
{
	struct folio *folio;
	size_t len, done = 0;
	loff_t start, pos;
	bool unlock;
	int ret, size;

	if (!readahead_count(rac))
		return;

	start = readahead_pos(rac);
	len = readahead_length(rac);

	do {
		pos = start + done;
		ret = erofs_fscache_data_read(rac->mapping, pos,
					      len - done, &unlock);
		if (ret <= 0)
			return;

		size = ret;
		while (size) {
			folio = readahead_folio(rac);
			size -= folio_size(folio);
			if (unlock) {
				folio_mark_uptodate(folio);
				folio_unlock(folio);
			}
		}
	} while ((done += ret) < len);
}

static const struct address_space_operations erofs_fscache_meta_aops = {
	.read_folio = erofs_fscache_meta_read_folio,
};

const struct address_space_operations erofs_fscache_access_aops = {
	.read_folio = erofs_fscache_read_folio,
	.readahead = erofs_fscache_readahead,
};

static void erofs_fscache_domain_put(struct erofs_domain *domain)
{
	if (!domain)
		return;
	mutex_lock(&erofs_domain_list_lock);
	if (refcount_dec_and_test(&domain->ref)) {
		list_del(&domain->list);
		if (list_empty(&erofs_domain_list)) {
			kern_unmount(erofs_pseudo_mnt);
			erofs_pseudo_mnt = NULL;
		}
		mutex_unlock(&erofs_domain_list_lock);
		fscache_relinquish_volume(domain->volume, NULL, false);
		kfree(domain->domain_id);
		kfree(domain);
		return;
	}
	mutex_unlock(&erofs_domain_list_lock);
}

static int erofs_fscache_register_volume(struct super_block *sb)
{
	struct erofs_sb_info *sbi = EROFS_SB(sb);
	char *domain_id = sbi->opt.domain_id;
	struct fscache_volume *volume;
	char *name;
	int ret = 0;

	name = kasprintf(GFP_KERNEL, "erofs,%s",
			 domain_id ? domain_id : sbi->opt.fsid);
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

static int erofs_fscache_init_domain(struct super_block *sb)
{
	int err;
	struct erofs_domain *domain;
	struct erofs_sb_info *sbi = EROFS_SB(sb);

	domain = kzalloc(sizeof(struct erofs_domain), GFP_KERNEL);
	if (!domain)
		return -ENOMEM;

	domain->domain_id = kstrdup(sbi->opt.domain_id, GFP_KERNEL);
	if (!domain->domain_id) {
		kfree(domain);
		return -ENOMEM;
	}

	err = erofs_fscache_register_volume(sb);
	if (err)
		goto out;

	if (!erofs_pseudo_mnt) {
		erofs_pseudo_mnt = kern_mount(&erofs_fs_type);
		if (IS_ERR(erofs_pseudo_mnt)) {
			err = PTR_ERR(erofs_pseudo_mnt);
			goto out;
		}
	}

	domain->volume = sbi->volume;
	refcount_set(&domain->ref, 1);
	list_add(&domain->list, &erofs_domain_list);
	sbi->domain = domain;
	return 0;
out:
	kfree(domain->domain_id);
	kfree(domain);
	return err;
}

static int erofs_fscache_register_domain(struct super_block *sb)
{
	int err;
	struct erofs_domain *domain;
	struct erofs_sb_info *sbi = EROFS_SB(sb);

	mutex_lock(&erofs_domain_list_lock);
	list_for_each_entry(domain, &erofs_domain_list, list) {
		if (!strcmp(domain->domain_id, sbi->opt.domain_id)) {
			sbi->domain = domain;
			sbi->volume = domain->volume;
			refcount_inc(&domain->ref);
			mutex_unlock(&erofs_domain_list_lock);
			return 0;
		}
	}
	err = erofs_fscache_init_domain(sb);
	mutex_unlock(&erofs_domain_list_lock);
	return err;
}

static
struct erofs_fscache *erofs_fscache_acquire_cookie(struct super_block *sb,
						    char *name, bool need_inode)
{
	struct fscache_volume *volume = EROFS_SB(sb)->volume;
	struct erofs_fscache *ctx;
	struct fscache_cookie *cookie;
	int ret;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

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

	return ctx;

err_cookie:
	fscache_unuse_cookie(ctx->cookie, NULL, NULL);
	fscache_relinquish_cookie(ctx->cookie, false);
err:
	kfree(ctx);
	return ERR_PTR(ret);
}

static void erofs_fscache_relinquish_cookie(struct erofs_fscache *ctx)
{
	fscache_unuse_cookie(ctx->cookie, NULL, NULL);
	fscache_relinquish_cookie(ctx->cookie, false);
	iput(ctx->inode);
	kfree(ctx->name);
	kfree(ctx);
}

static
struct erofs_fscache *erofs_fscache_domain_init_cookie(struct super_block *sb,
		char *name, bool need_inode)
{
	int err;
	struct inode *inode;
	struct erofs_fscache *ctx;
	struct erofs_domain *domain = EROFS_SB(sb)->domain;

	ctx = erofs_fscache_acquire_cookie(sb, name, need_inode);
	if (IS_ERR(ctx))
		return ctx;

	ctx->name = kstrdup(name, GFP_KERNEL);
	if (!ctx->name) {
		err = -ENOMEM;
		goto out;
	}

	inode = new_inode(erofs_pseudo_mnt->mnt_sb);
	if (!inode) {
		err = -ENOMEM;
		goto out;
	}

	ctx->domain = domain;
	ctx->anon_inode = inode;
	inode->i_private = ctx;
	refcount_inc(&domain->ref);
	return ctx;
out:
	erofs_fscache_relinquish_cookie(ctx);
	return ERR_PTR(err);
}

static
struct erofs_fscache *erofs_domain_register_cookie(struct super_block *sb,
						   char *name, bool need_inode)
{
	struct inode *inode;
	struct erofs_fscache *ctx;
	struct erofs_domain *domain = EROFS_SB(sb)->domain;
	struct super_block *psb = erofs_pseudo_mnt->mnt_sb;

	mutex_lock(&erofs_domain_cookies_lock);
	list_for_each_entry(inode, &psb->s_inodes, i_sb_list) {
		ctx = inode->i_private;
		if (!ctx || ctx->domain != domain || strcmp(ctx->name, name))
			continue;
		igrab(inode);
		mutex_unlock(&erofs_domain_cookies_lock);
		return ctx;
	}
	ctx = erofs_fscache_domain_init_cookie(sb, name, need_inode);
	mutex_unlock(&erofs_domain_cookies_lock);
	return ctx;
}

struct erofs_fscache *erofs_fscache_register_cookie(struct super_block *sb,
						    char *name, bool need_inode)
{
	if (EROFS_SB(sb)->opt.domain_id)
		return erofs_domain_register_cookie(sb, name, need_inode);
	return erofs_fscache_acquire_cookie(sb, name, need_inode);
}

void erofs_fscache_unregister_cookie(struct erofs_fscache *ctx)
{
	bool drop;
	struct erofs_domain *domain;

	if (!ctx)
		return;
	domain = ctx->domain;
	if (domain) {
		mutex_lock(&erofs_domain_cookies_lock);
		drop = atomic_read(&ctx->anon_inode->i_count) == 1;
		iput(ctx->anon_inode);
		mutex_unlock(&erofs_domain_cookies_lock);
		if (!drop)
			return;
	}

	erofs_fscache_relinquish_cookie(ctx);
	erofs_fscache_domain_put(domain);
}

int erofs_fscache_register_fs(struct super_block *sb)
{
	int ret;
	struct erofs_sb_info *sbi = EROFS_SB(sb);
	struct erofs_fscache *fscache;

	if (sbi->opt.domain_id)
		ret = erofs_fscache_register_domain(sb);
	else
		ret = erofs_fscache_register_volume(sb);
	if (ret)
		return ret;

	/* acquired domain/volume will be relinquished in kill_sb() on error */
	fscache = erofs_fscache_register_cookie(sb, sbi->opt.fsid, true);
	if (IS_ERR(fscache))
		return PTR_ERR(fscache);

	sbi->s_fscache = fscache;
	return 0;
}

void erofs_fscache_unregister_fs(struct super_block *sb)
{
	struct erofs_sb_info *sbi = EROFS_SB(sb);

	erofs_fscache_unregister_cookie(sbi->s_fscache);

	if (sbi->domain)
		erofs_fscache_domain_put(sbi->domain);
	else
		fscache_relinquish_volume(sbi->volume, NULL, false);

	sbi->s_fscache = NULL;
	sbi->volume = NULL;
	sbi->domain = NULL;
}
