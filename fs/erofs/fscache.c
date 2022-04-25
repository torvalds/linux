// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022, Alibaba Cloud
 */
#include <linux/fscache.h>
#include "internal.h"

/*
 * Read data from fscache and fill the read data into page cache described by
 * @start/len, which shall be both aligned with PAGE_SIZE. @pstart describes
 * the start physical address in the cache file.
 */
static int erofs_fscache_read_folios(struct fscache_cookie *cookie,
				     struct address_space *mapping,
				     loff_t start, size_t len,
				     loff_t pstart)
{
	enum netfs_io_source source;
	struct netfs_io_request rreq = {};
	struct netfs_io_subrequest subreq = { .rreq = &rreq, };
	struct netfs_cache_resources *cres = &rreq.cache_resources;
	struct super_block *sb = mapping->host->i_sb;
	struct iov_iter iter;
	size_t done = 0;
	int ret;

	ret = fscache_begin_read_operation(cres, cookie);
	if (ret)
		return ret;

	while (done < len) {
		subreq.start = pstart + done;
		subreq.len = len - done;
		subreq.flags = 1 << NETFS_SREQ_ONDEMAND;

		source = cres->ops->prepare_read(&subreq, LLONG_MAX);
		if (WARN_ON(subreq.len == 0))
			source = NETFS_INVALID_READ;
		if (source != NETFS_READ_FROM_CACHE) {
			erofs_err(sb, "failed to fscache prepare_read (source %d)",
				  source);
			ret = -EIO;
			goto out;
		}

		iov_iter_xarray(&iter, READ, &mapping->i_pages,
				start + done, subreq.len);
		ret = fscache_read(cres, subreq.start, &iter,
				   NETFS_READ_HOLE_FAIL, NULL, NULL);
		if (ret) {
			erofs_err(sb, "failed to fscache_read (ret %d)", ret);
			goto out;
		}

		done += subreq.len;
	}
out:
	fscache_end_operation(cres);
	return ret;
}

static int erofs_fscache_meta_readpage(struct file *data, struct page *page)
{
	int ret;
	struct folio *folio = page_folio(page);
	struct super_block *sb = folio_mapping(folio)->host->i_sb;
	struct erofs_map_dev mdev = {
		.m_deviceid = 0,
		.m_pa = folio_pos(folio),
	};

	ret = erofs_map_dev(sb, &mdev);
	if (ret)
		goto out;

	ret = erofs_fscache_read_folios(mdev.m_fscache->cookie,
			folio_mapping(folio), folio_pos(folio),
			folio_size(folio), mdev.m_pa);
	if (!ret)
		folio_mark_uptodate(folio);
out:
	folio_unlock(folio);
	return ret;
}

static int erofs_fscache_readpage_inline(struct folio *folio,
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

static int erofs_fscache_readpage(struct file *file, struct page *page)
{
	struct folio *folio = page_folio(page);
	struct inode *inode = folio_mapping(folio)->host;
	struct super_block *sb = inode->i_sb;
	struct erofs_map_blocks map;
	struct erofs_map_dev mdev;
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
		ret = erofs_fscache_readpage_inline(folio, &map);
		goto out_uptodate;
	}

	mdev = (struct erofs_map_dev) {
		.m_deviceid = map.m_deviceid,
		.m_pa = map.m_pa,
	};

	ret = erofs_map_dev(sb, &mdev);
	if (ret)
		goto out_unlock;

	pstart = mdev.m_pa + (pos - map.m_la);
	ret = erofs_fscache_read_folios(mdev.m_fscache->cookie,
			folio_mapping(folio), folio_pos(folio),
			folio_size(folio), pstart);

out_uptodate:
	if (!ret)
		folio_mark_uptodate(folio);
out_unlock:
	folio_unlock(folio);
	return ret;
}

static void erofs_fscache_unlock_folios(struct readahead_control *rac,
					size_t len)
{
	while (len) {
		struct folio *folio = readahead_folio(rac);

		len -= folio_size(folio);
		folio_mark_uptodate(folio);
		folio_unlock(folio);
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

			erofs_fscache_unlock_folios(rac, count);
			ret = count;
			continue;
		}

		if (map.m_flags & EROFS_MAP_META) {
			struct folio *folio = readahead_folio(rac);

			ret = erofs_fscache_readpage_inline(folio, &map);
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

		ret = erofs_fscache_read_folios(mdev.m_fscache->cookie,
				rac->mapping, offset, count,
				mdev.m_pa + (pos - map.m_la));
		/*
		 * For the error cases, the folios will be unlocked when
		 * .readahead() returns.
		 */
		if (!ret) {
			erofs_fscache_unlock_folios(rac, count);
			ret = count;
		}
	} while (ret > 0 && ((done += ret) < len));
}

static const struct address_space_operations erofs_fscache_meta_aops = {
	.readpage = erofs_fscache_meta_readpage,
};

const struct address_space_operations erofs_fscache_access_aops = {
	.readpage = erofs_fscache_readpage,
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
