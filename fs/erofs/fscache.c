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
static LIST_HEAD(erofs_domain_cookies_list);
static struct vfsmount *erofs_pseudo_mnt;

struct erofs_fscache_request {
	struct erofs_fscache_request *primary;
	struct netfs_cache_resources cache_resources;
	struct address_space	*mapping;	/* The mapping being accessed */
	loff_t			start;		/* Start position */
	size_t			len;		/* Length of the request */
	size_t			submitted;	/* Length of submitted */
	short			error;		/* 0 or error that occurred */
	refcount_t		ref;
};

static struct erofs_fscache_request *erofs_fscache_req_alloc(struct address_space *mapping,
					     loff_t start, size_t len)
{
	struct erofs_fscache_request *req;

	req = kzalloc(sizeof(struct erofs_fscache_request), GFP_KERNEL);
	if (!req)
		return ERR_PTR(-ENOMEM);

	req->mapping = mapping;
	req->start   = start;
	req->len     = len;
	refcount_set(&req->ref, 1);

	return req;
}

static struct erofs_fscache_request *erofs_fscache_req_chain(struct erofs_fscache_request *primary,
					     size_t len)
{
	struct erofs_fscache_request *req;

	/* use primary request for the first submission */
	if (!primary->submitted) {
		refcount_inc(&primary->ref);
		return primary;
	}

	req = erofs_fscache_req_alloc(primary->mapping,
			primary->start + primary->submitted, len);
	if (!IS_ERR(req)) {
		req->primary = primary;
		refcount_inc(&primary->ref);
	}
	return req;
}

static void erofs_fscache_req_complete(struct erofs_fscache_request *req)
{
	struct folio *folio;
	bool failed = req->error;
	pgoff_t start_page = req->start / PAGE_SIZE;
	pgoff_t last_page = ((req->start + req->len) / PAGE_SIZE) - 1;

	XA_STATE(xas, &req->mapping->i_pages, start_page);

	rcu_read_lock();
	xas_for_each(&xas, folio, last_page) {
		if (xas_retry(&xas, folio))
			continue;
		if (!failed)
			folio_mark_uptodate(folio);
		folio_unlock(folio);
	}
	rcu_read_unlock();
}

static void erofs_fscache_req_put(struct erofs_fscache_request *req)
{
	if (refcount_dec_and_test(&req->ref)) {
		if (req->cache_resources.ops)
			req->cache_resources.ops->end_operation(&req->cache_resources);
		if (!req->primary)
			erofs_fscache_req_complete(req);
		else
			erofs_fscache_req_put(req->primary);
		kfree(req);
	}
}

static void erofs_fscache_subreq_complete(void *priv,
		ssize_t transferred_or_error, bool was_async)
{
	struct erofs_fscache_request *req = priv;

	if (IS_ERR_VALUE(transferred_or_error)) {
		if (req->primary)
			req->primary->error = transferred_or_error;
		else
			req->error = transferred_or_error;
	}
	erofs_fscache_req_put(req);
}

/*
 * Read data from fscache (cookie, pstart, len), and fill the read data into
 * page cache described by (req->mapping, lstart, len). @pstart describeis the
 * start physical address in the cache file.
 */
static int erofs_fscache_read_folios_async(struct fscache_cookie *cookie,
		struct erofs_fscache_request *req, loff_t pstart, size_t len)
{
	enum netfs_io_source source;
	struct super_block *sb = req->mapping->host->i_sb;
	struct netfs_cache_resources *cres = &req->cache_resources;
	struct iov_iter iter;
	loff_t lstart = req->start + req->submitted;
	size_t done = 0;
	int ret;

	DBG_BUGON(len > req->len - req->submitted);

	ret = fscache_begin_read_operation(cres, cookie);
	if (ret)
		return ret;

	while (done < len) {
		loff_t sstart = pstart + done;
		size_t slen = len - done;
		unsigned long flags = 1 << NETFS_SREQ_ONDEMAND;

		source = cres->ops->prepare_ondemand_read(cres,
				sstart, &slen, LLONG_MAX, &flags, 0);
		if (WARN_ON(slen == 0))
			source = NETFS_INVALID_READ;
		if (source != NETFS_READ_FROM_CACHE) {
			erofs_err(sb, "failed to fscache prepare_read (source %d)", source);
			return -EIO;
		}

		refcount_inc(&req->ref);
		iov_iter_xarray(&iter, ITER_DEST, &req->mapping->i_pages,
				lstart + done, slen);

		ret = fscache_read(cres, sstart, &iter, NETFS_READ_HOLE_FAIL,
				   erofs_fscache_subreq_complete, req);
		if (ret == -EIOCBQUEUED)
			ret = 0;
		if (ret) {
			erofs_err(sb, "failed to fscache_read (ret %d)", ret);
			return ret;
		}

		done += slen;
	}
	DBG_BUGON(done != len);
	return 0;
}

static int erofs_fscache_meta_read_folio(struct file *data, struct folio *folio)
{
	int ret;
	struct erofs_fscache *ctx = folio->mapping->host->i_private;
	struct erofs_fscache_request *req;

	req = erofs_fscache_req_alloc(folio->mapping,
				folio_pos(folio), folio_size(folio));
	if (IS_ERR(req)) {
		folio_unlock(folio);
		return PTR_ERR(req);
	}

	ret = erofs_fscache_read_folios_async(ctx->cookie, req,
				folio_pos(folio), folio_size(folio));
	if (ret)
		req->error = ret;

	erofs_fscache_req_put(req);
	return ret;
}

static int erofs_fscache_data_read_slice(struct erofs_fscache_request *primary)
{
	struct address_space *mapping = primary->mapping;
	struct inode *inode = mapping->host;
	struct super_block *sb = inode->i_sb;
	struct erofs_fscache_request *req;
	struct erofs_map_blocks map;
	struct erofs_map_dev mdev;
	struct iov_iter iter;
	loff_t pos = primary->start + primary->submitted;
	size_t count;
	int ret;

	map.m_la = pos;
	ret = erofs_map_blocks(inode, &map);
	if (ret)
		return ret;

	if (map.m_flags & EROFS_MAP_META) {
		struct erofs_buf buf = __EROFS_BUF_INITIALIZER;
		erofs_blk_t blknr;
		size_t offset, size;
		void *src;

		/* For tail packing layout, the offset may be non-zero. */
		offset = erofs_blkoff(sb, map.m_pa);
		blknr = erofs_blknr(sb, map.m_pa);
		size = map.m_llen;

		src = erofs_read_metabuf(&buf, sb, blknr, EROFS_KMAP);
		if (IS_ERR(src))
			return PTR_ERR(src);

		iov_iter_xarray(&iter, ITER_DEST, &mapping->i_pages, pos, PAGE_SIZE);
		if (copy_to_iter(src + offset, size, &iter) != size) {
			erofs_put_metabuf(&buf);
			return -EFAULT;
		}
		iov_iter_zero(PAGE_SIZE - size, &iter);
		erofs_put_metabuf(&buf);
		primary->submitted += PAGE_SIZE;
		return 0;
	}

	count = primary->len - primary->submitted;
	if (!(map.m_flags & EROFS_MAP_MAPPED)) {
		iov_iter_xarray(&iter, ITER_DEST, &mapping->i_pages, pos, count);
		iov_iter_zero(count, &iter);
		primary->submitted += count;
		return 0;
	}

	count = min_t(size_t, map.m_llen - (pos - map.m_la), count);
	DBG_BUGON(!count || count % PAGE_SIZE);

	mdev = (struct erofs_map_dev) {
		.m_deviceid = map.m_deviceid,
		.m_pa = map.m_pa,
	};
	ret = erofs_map_dev(sb, &mdev);
	if (ret)
		return ret;

	req = erofs_fscache_req_chain(primary, count);
	if (IS_ERR(req))
		return PTR_ERR(req);

	ret = erofs_fscache_read_folios_async(mdev.m_fscache->cookie,
			req, mdev.m_pa + (pos - map.m_la), count);
	erofs_fscache_req_put(req);
	primary->submitted += count;
	return ret;
}

static int erofs_fscache_data_read(struct erofs_fscache_request *req)
{
	int ret;

	do {
		ret = erofs_fscache_data_read_slice(req);
		if (ret)
			req->error = ret;
	} while (!ret && req->submitted < req->len);

	return ret;
}

static int erofs_fscache_read_folio(struct file *file, struct folio *folio)
{
	struct erofs_fscache_request *req;
	int ret;

	req = erofs_fscache_req_alloc(folio->mapping,
			folio_pos(folio), folio_size(folio));
	if (IS_ERR(req)) {
		folio_unlock(folio);
		return PTR_ERR(req);
	}

	ret = erofs_fscache_data_read(req);
	erofs_fscache_req_put(req);
	return ret;
}

static void erofs_fscache_readahead(struct readahead_control *rac)
{
	struct erofs_fscache_request *req;

	if (!readahead_count(rac))
		return;

	req = erofs_fscache_req_alloc(rac->mapping,
			readahead_pos(rac), readahead_length(rac));
	if (IS_ERR(req))
		return;

	/* The request completion will drop refs on the folios. */
	while (readahead_folio(rac))
		;

	erofs_fscache_data_read(req);
	erofs_fscache_req_put(req);
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
	mutex_lock(&erofs_domain_list_lock);
	if (refcount_dec_and_test(&domain->ref)) {
		list_del(&domain->list);
		if (list_empty(&erofs_domain_list)) {
			kern_unmount(erofs_pseudo_mnt);
			erofs_pseudo_mnt = NULL;
		}
		fscache_relinquish_volume(domain->volume, NULL, false);
		mutex_unlock(&erofs_domain_list_lock);
		kfree(domain->domain_id);
		kfree(domain);
		return;
	}
	mutex_unlock(&erofs_domain_list_lock);
}

static int erofs_fscache_register_volume(struct super_block *sb)
{
	struct erofs_sb_info *sbi = EROFS_SB(sb);
	char *domain_id = sbi->domain_id;
	struct fscache_volume *volume;
	char *name;
	int ret = 0;

	name = kasprintf(GFP_KERNEL, "erofs,%s",
			 domain_id ? domain_id : sbi->fsid);
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

	domain->domain_id = kstrdup(sbi->domain_id, GFP_KERNEL);
	if (!domain->domain_id) {
		kfree(domain);
		return -ENOMEM;
	}

	err = erofs_fscache_register_volume(sb);
	if (err)
		goto out;

	if (!erofs_pseudo_mnt) {
		struct vfsmount *mnt = kern_mount(&erofs_fs_type);
		if (IS_ERR(mnt)) {
			err = PTR_ERR(mnt);
			goto out;
		}
		erofs_pseudo_mnt = mnt;
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
		if (!strcmp(domain->domain_id, sbi->domain_id)) {
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

static struct erofs_fscache *erofs_fscache_acquire_cookie(struct super_block *sb,
						char *name, unsigned int flags)
{
	struct fscache_volume *volume = EROFS_SB(sb)->volume;
	struct erofs_fscache *ctx;
	struct fscache_cookie *cookie;
	struct super_block *isb;
	struct inode *inode;
	int ret;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return ERR_PTR(-ENOMEM);
	INIT_LIST_HEAD(&ctx->node);
	refcount_set(&ctx->ref, 1);

	cookie = fscache_acquire_cookie(volume, FSCACHE_ADV_WANT_CACHE_SIZE,
					name, strlen(name), NULL, 0, 0);
	if (!cookie) {
		erofs_err(sb, "failed to get cookie for %s", name);
		ret = -EINVAL;
		goto err;
	}
	fscache_use_cookie(cookie, false);

	/*
	 * Allocate anonymous inode in global pseudo mount for shareable blobs,
	 * so that they are accessible among erofs fs instances.
	 */
	isb = flags & EROFS_REG_COOKIE_SHARE ? erofs_pseudo_mnt->mnt_sb : sb;
	inode = new_inode(isb);
	if (!inode) {
		erofs_err(sb, "failed to get anon inode for %s", name);
		ret = -ENOMEM;
		goto err_cookie;
	}

	inode->i_size = OFFSET_MAX;
	inode->i_mapping->a_ops = &erofs_fscache_meta_aops;
	mapping_set_gfp_mask(inode->i_mapping, GFP_KERNEL);
	inode->i_blkbits = EROFS_SB(sb)->blkszbits;
	inode->i_private = ctx;

	ctx->cookie = cookie;
	ctx->inode = inode;
	return ctx;

err_cookie:
	fscache_unuse_cookie(cookie, NULL, NULL);
	fscache_relinquish_cookie(cookie, false);
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

static struct erofs_fscache *erofs_domain_init_cookie(struct super_block *sb,
						char *name, unsigned int flags)
{
	struct erofs_fscache *ctx;
	struct erofs_domain *domain = EROFS_SB(sb)->domain;

	ctx = erofs_fscache_acquire_cookie(sb, name, flags);
	if (IS_ERR(ctx))
		return ctx;

	ctx->name = kstrdup(name, GFP_KERNEL);
	if (!ctx->name) {
		erofs_fscache_relinquish_cookie(ctx);
		return ERR_PTR(-ENOMEM);
	}

	refcount_inc(&domain->ref);
	ctx->domain = domain;
	list_add(&ctx->node, &erofs_domain_cookies_list);
	return ctx;
}

static struct erofs_fscache *erofs_domain_register_cookie(struct super_block *sb,
						char *name, unsigned int flags)
{
	struct erofs_fscache *ctx;
	struct erofs_domain *domain = EROFS_SB(sb)->domain;

	flags |= EROFS_REG_COOKIE_SHARE;
	mutex_lock(&erofs_domain_cookies_lock);
	list_for_each_entry(ctx, &erofs_domain_cookies_list, node) {
		if (ctx->domain != domain || strcmp(ctx->name, name))
			continue;
		if (!(flags & EROFS_REG_COOKIE_NEED_NOEXIST)) {
			refcount_inc(&ctx->ref);
		} else {
			erofs_err(sb, "%s already exists in domain %s", name,
				  domain->domain_id);
			ctx = ERR_PTR(-EEXIST);
		}
		mutex_unlock(&erofs_domain_cookies_lock);
		return ctx;
	}
	ctx = erofs_domain_init_cookie(sb, name, flags);
	mutex_unlock(&erofs_domain_cookies_lock);
	return ctx;
}

struct erofs_fscache *erofs_fscache_register_cookie(struct super_block *sb,
						    char *name,
						    unsigned int flags)
{
	if (EROFS_SB(sb)->domain_id)
		return erofs_domain_register_cookie(sb, name, flags);
	return erofs_fscache_acquire_cookie(sb, name, flags);
}

void erofs_fscache_unregister_cookie(struct erofs_fscache *ctx)
{
	struct erofs_domain *domain = NULL;

	if (!ctx)
		return;
	if (!ctx->domain)
		return erofs_fscache_relinquish_cookie(ctx);

	mutex_lock(&erofs_domain_cookies_lock);
	if (refcount_dec_and_test(&ctx->ref)) {
		domain = ctx->domain;
		list_del(&ctx->node);
		erofs_fscache_relinquish_cookie(ctx);
	}
	mutex_unlock(&erofs_domain_cookies_lock);
	if (domain)
		erofs_fscache_domain_put(domain);
}

int erofs_fscache_register_fs(struct super_block *sb)
{
	int ret;
	struct erofs_sb_info *sbi = EROFS_SB(sb);
	struct erofs_fscache *fscache;
	unsigned int flags = 0;

	if (sbi->domain_id)
		ret = erofs_fscache_register_domain(sb);
	else
		ret = erofs_fscache_register_volume(sb);
	if (ret)
		return ret;

	/*
	 * When shared domain is enabled, using NEED_NOEXIST to guarantee
	 * the primary data blob (aka fsid) is unique in the shared domain.
	 *
	 * For non-shared-domain case, fscache_acquire_volume() invoked by
	 * erofs_fscache_register_volume() has already guaranteed
	 * the uniqueness of primary data blob.
	 *
	 * Acquired domain/volume will be relinquished in kill_sb() on error.
	 */
	if (sbi->domain_id)
		flags |= EROFS_REG_COOKIE_NEED_NOEXIST;
	fscache = erofs_fscache_register_cookie(sb, sbi->fsid, flags);
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
