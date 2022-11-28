/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2018  Miklos Szeredi <miklos@szeredi.hu>

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.
*/


#include "fuse_i.h"
#include <linux/iversion.h>
#include <linux/posix_acl.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>

static bool fuse_use_readdirplus(struct inode *dir, struct dir_context *ctx)
{
	struct fuse_conn *fc = get_fuse_conn(dir);
	struct fuse_inode *fi = get_fuse_inode(dir);

	if (!fc->do_readdirplus)
		return false;
	if (!fc->readdirplus_auto)
		return true;
	if (test_and_clear_bit(FUSE_I_ADVISE_RDPLUS, &fi->state))
		return true;
	if (ctx->pos == 0)
		return true;
	return false;
}

static void fuse_add_dirent_to_cache(struct file *file,
				     struct fuse_dirent *dirent, loff_t pos)
{
	struct fuse_inode *fi = get_fuse_inode(file_inode(file));
	size_t reclen = FUSE_DIRENT_SIZE(dirent);
	pgoff_t index;
	struct page *page;
	loff_t size;
	u64 version;
	unsigned int offset;
	void *addr;

	spin_lock(&fi->rdc.lock);
	/*
	 * Is cache already completed?  Or this entry does not go at the end of
	 * cache?
	 */
	if (fi->rdc.cached || pos != fi->rdc.pos) {
		spin_unlock(&fi->rdc.lock);
		return;
	}
	version = fi->rdc.version;
	size = fi->rdc.size;
	offset = size & ~PAGE_MASK;
	index = size >> PAGE_SHIFT;
	/* Dirent doesn't fit in current page?  Jump to next page. */
	if (offset + reclen > PAGE_SIZE) {
		index++;
		offset = 0;
	}
	spin_unlock(&fi->rdc.lock);

	if (offset) {
		page = find_lock_page(file->f_mapping, index);
	} else {
		page = find_or_create_page(file->f_mapping, index,
					   mapping_gfp_mask(file->f_mapping));
	}
	if (!page)
		return;

	spin_lock(&fi->rdc.lock);
	/* Raced with another readdir */
	if (fi->rdc.version != version || fi->rdc.size != size ||
	    WARN_ON(fi->rdc.pos != pos))
		goto unlock;

	addr = kmap_atomic(page);
	if (!offset) {
		clear_page(addr);
		SetPageUptodate(page);
	}
	memcpy(addr + offset, dirent, reclen);
	kunmap_atomic(addr);
	fi->rdc.size = (index << PAGE_SHIFT) + offset + reclen;
	fi->rdc.pos = dirent->off;
unlock:
	spin_unlock(&fi->rdc.lock);
	unlock_page(page);
	put_page(page);
}

static void fuse_readdir_cache_end(struct file *file, loff_t pos)
{
	struct fuse_inode *fi = get_fuse_inode(file_inode(file));
	loff_t end;

	spin_lock(&fi->rdc.lock);
	/* does cache end position match current position? */
	if (fi->rdc.pos != pos) {
		spin_unlock(&fi->rdc.lock);
		return;
	}

	fi->rdc.cached = true;
	end = ALIGN(fi->rdc.size, PAGE_SIZE);
	spin_unlock(&fi->rdc.lock);

	/* truncate unused tail of cache */
	truncate_inode_pages(file->f_mapping, end);
}

static bool fuse_emit(struct file *file, struct dir_context *ctx,
		      struct fuse_dirent *dirent)
{
	struct fuse_file *ff = file->private_data;

	if (ff->open_flags & FOPEN_CACHE_DIR)
		fuse_add_dirent_to_cache(file, dirent, ctx->pos);

	return dir_emit(ctx, dirent->name, dirent->namelen, dirent->ino,
			dirent->type);
}

static int parse_dirfile(char *buf, size_t nbytes, struct file *file,
			 struct dir_context *ctx)
{
	while (nbytes >= FUSE_NAME_OFFSET) {
		struct fuse_dirent *dirent = (struct fuse_dirent *) buf;
		size_t reclen = FUSE_DIRENT_SIZE(dirent);
		if (!dirent->namelen || dirent->namelen > FUSE_NAME_MAX)
			return -EIO;
		if (reclen > nbytes)
			break;
		if (memchr(dirent->name, '/', dirent->namelen) != NULL)
			return -EIO;

		if (!fuse_emit(file, ctx, dirent))
			break;

		buf += reclen;
		nbytes -= reclen;
		ctx->pos = dirent->off;
	}

	return 0;
}

static int fuse_direntplus_link(struct file *file,
				struct fuse_direntplus *direntplus,
				u64 attr_version)
{
	struct fuse_entry_out *o = &direntplus->entry_out;
	struct fuse_dirent *dirent = &direntplus->dirent;
	struct dentry *parent = file->f_path.dentry;
	struct qstr name = QSTR_INIT(dirent->name, dirent->namelen);
	struct dentry *dentry;
	struct dentry *alias;
	struct inode *dir = d_inode(parent);
	struct fuse_conn *fc;
	struct inode *inode;
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(wq);

	if (!o->nodeid) {
		/*
		 * Unlike in the case of fuse_lookup, zero nodeid does not mean
		 * ENOENT. Instead, it only means the userspace filesystem did
		 * not want to return attributes/handle for this entry.
		 *
		 * So do nothing.
		 */
		return 0;
	}

	if (name.name[0] == '.') {
		/*
		 * We could potentially refresh the attributes of the directory
		 * and its parent?
		 */
		if (name.len == 1)
			return 0;
		if (name.name[1] == '.' && name.len == 2)
			return 0;
	}

	if (invalid_nodeid(o->nodeid))
		return -EIO;
	if (fuse_invalid_attr(&o->attr))
		return -EIO;

	fc = get_fuse_conn(dir);

	name.hash = full_name_hash(parent, name.name, name.len);
	dentry = d_lookup(parent, &name);
	if (!dentry) {
retry:
		dentry = d_alloc_parallel(parent, &name, &wq);
		if (IS_ERR(dentry))
			return PTR_ERR(dentry);
	}
	if (!d_in_lookup(dentry)) {
		struct fuse_inode *fi;
		inode = d_inode(dentry);
		if (inode && get_node_id(inode) != o->nodeid)
			inode = NULL;
		if (!inode ||
		    fuse_stale_inode(inode, o->generation, &o->attr)) {
			if (inode)
				fuse_make_bad(inode);
			d_invalidate(dentry);
			dput(dentry);
			goto retry;
		}
		if (fuse_is_bad(inode)) {
			dput(dentry);
			return -EIO;
		}

		fi = get_fuse_inode(inode);
		spin_lock(&fi->lock);
		fi->nlookup++;
		spin_unlock(&fi->lock);

		forget_all_cached_acls(inode);
		fuse_change_attributes(inode, &o->attr,
				       entry_attr_timeout(o),
				       attr_version);
		/*
		 * The other branch comes via fuse_iget()
		 * which bumps nlookup inside
		 */
	} else {
		inode = fuse_iget(dir->i_sb, o->nodeid, o->generation,
				  &o->attr, entry_attr_timeout(o),
				  attr_version);
		if (!inode)
			inode = ERR_PTR(-ENOMEM);

		alias = d_splice_alias(inode, dentry);
		d_lookup_done(dentry);
		if (alias) {
			dput(dentry);
			dentry = alias;
		}
		if (IS_ERR(dentry))
			return PTR_ERR(dentry);
	}
	if (fc->readdirplus_auto)
		set_bit(FUSE_I_INIT_RDPLUS, &get_fuse_inode(inode)->state);
	fuse_change_entry_timeout(dentry, o);

	dput(dentry);
	return 0;
}

static void fuse_force_forget(struct file *file, u64 nodeid)
{
	struct inode *inode = file_inode(file);
	struct fuse_mount *fm = get_fuse_mount(inode);
	struct fuse_forget_in inarg;
	FUSE_ARGS(args);

	memset(&inarg, 0, sizeof(inarg));
	inarg.nlookup = 1;
	args.opcode = FUSE_FORGET;
	args.nodeid = nodeid;
	args.in_numargs = 1;
	args.in_args[0].size = sizeof(inarg);
	args.in_args[0].value = &inarg;
	args.force = true;
	args.noreply = true;

	fuse_simple_request(fm, &args);
	/* ignore errors */
}

static int parse_dirplusfile(char *buf, size_t nbytes, struct file *file,
			     struct dir_context *ctx, u64 attr_version)
{
	struct fuse_direntplus *direntplus;
	struct fuse_dirent *dirent;
	size_t reclen;
	int over = 0;
	int ret;

	while (nbytes >= FUSE_NAME_OFFSET_DIRENTPLUS) {
		direntplus = (struct fuse_direntplus *) buf;
		dirent = &direntplus->dirent;
		reclen = FUSE_DIRENTPLUS_SIZE(direntplus);

		if (!dirent->namelen || dirent->namelen > FUSE_NAME_MAX)
			return -EIO;
		if (reclen > nbytes)
			break;
		if (memchr(dirent->name, '/', dirent->namelen) != NULL)
			return -EIO;

		if (!over) {
			/* We fill entries into dstbuf only as much as
			   it can hold. But we still continue iterating
			   over remaining entries to link them. If not,
			   we need to send a FORGET for each of those
			   which we did not link.
			*/
			over = !fuse_emit(file, ctx, dirent);
			if (!over)
				ctx->pos = dirent->off;
		}

		buf += reclen;
		nbytes -= reclen;

		ret = fuse_direntplus_link(file, direntplus, attr_version);
		if (ret)
			fuse_force_forget(file, direntplus->entry_out.nodeid);
	}

	return 0;
}

static int fuse_readdir_uncached(struct file *file, struct dir_context *ctx)
{
	int plus;
	ssize_t res;
	struct page *page;
	struct inode *inode = file_inode(file);
	struct fuse_mount *fm = get_fuse_mount(inode);
	struct fuse_io_args ia = {};
	struct fuse_args_pages *ap = &ia.ap;
	struct fuse_page_desc desc = { .length = PAGE_SIZE };
	u64 attr_version = 0;
	bool locked;

	page = alloc_page(GFP_KERNEL);
	if (!page)
		return -ENOMEM;

	plus = fuse_use_readdirplus(inode, ctx);
	ap->args.out_pages = true;
	ap->num_pages = 1;
	ap->pages = &page;
	ap->descs = &desc;
	if (plus) {
		attr_version = fuse_get_attr_version(fm->fc);
		fuse_read_args_fill(&ia, file, ctx->pos, PAGE_SIZE,
				    FUSE_READDIRPLUS);
	} else {
		fuse_read_args_fill(&ia, file, ctx->pos, PAGE_SIZE,
				    FUSE_READDIR);
	}
	locked = fuse_lock_inode(inode);
	res = fuse_simple_request(fm, &ap->args);
	fuse_unlock_inode(inode, locked);
	if (res >= 0) {
		if (!res) {
			struct fuse_file *ff = file->private_data;

			if (ff->open_flags & FOPEN_CACHE_DIR)
				fuse_readdir_cache_end(file, ctx->pos);
		} else if (plus) {
			res = parse_dirplusfile(page_address(page), res,
						file, ctx, attr_version);
		} else {
			res = parse_dirfile(page_address(page), res, file,
					    ctx);
		}
	}

	__free_page(page);
	fuse_invalidate_atime(inode);
	return res;
}

enum fuse_parse_result {
	FOUND_ERR = -1,
	FOUND_NONE = 0,
	FOUND_SOME,
	FOUND_ALL,
};

static enum fuse_parse_result fuse_parse_cache(struct fuse_file *ff,
					       void *addr, unsigned int size,
					       struct dir_context *ctx)
{
	unsigned int offset = ff->readdir.cache_off & ~PAGE_MASK;
	enum fuse_parse_result res = FOUND_NONE;

	WARN_ON(offset >= size);

	for (;;) {
		struct fuse_dirent *dirent = addr + offset;
		unsigned int nbytes = size - offset;
		size_t reclen;

		if (nbytes < FUSE_NAME_OFFSET || !dirent->namelen)
			break;

		reclen = FUSE_DIRENT_SIZE(dirent); /* derefs ->namelen */

		if (WARN_ON(dirent->namelen > FUSE_NAME_MAX))
			return FOUND_ERR;
		if (WARN_ON(reclen > nbytes))
			return FOUND_ERR;
		if (WARN_ON(memchr(dirent->name, '/', dirent->namelen) != NULL))
			return FOUND_ERR;

		if (ff->readdir.pos == ctx->pos) {
			res = FOUND_SOME;
			if (!dir_emit(ctx, dirent->name, dirent->namelen,
				      dirent->ino, dirent->type))
				return FOUND_ALL;
			ctx->pos = dirent->off;
		}
		ff->readdir.pos = dirent->off;
		ff->readdir.cache_off += reclen;

		offset += reclen;
	}

	return res;
}

static void fuse_rdc_reset(struct inode *inode)
{
	struct fuse_inode *fi = get_fuse_inode(inode);

	fi->rdc.cached = false;
	fi->rdc.version++;
	fi->rdc.size = 0;
	fi->rdc.pos = 0;
}

#define UNCACHED 1

static int fuse_readdir_cached(struct file *file, struct dir_context *ctx)
{
	struct fuse_file *ff = file->private_data;
	struct inode *inode = file_inode(file);
	struct fuse_conn *fc = get_fuse_conn(inode);
	struct fuse_inode *fi = get_fuse_inode(inode);
	enum fuse_parse_result res;
	pgoff_t index;
	unsigned int size;
	struct page *page;
	void *addr;

	/* Seeked?  If so, reset the cache stream */
	if (ff->readdir.pos != ctx->pos) {
		ff->readdir.pos = 0;
		ff->readdir.cache_off = 0;
	}

	/*
	 * We're just about to start reading into the cache or reading the
	 * cache; both cases require an up-to-date mtime value.
	 */
	if (!ctx->pos && fc->auto_inval_data) {
		int err = fuse_update_attributes(inode, file);

		if (err)
			return err;
	}

retry:
	spin_lock(&fi->rdc.lock);
retry_locked:
	if (!fi->rdc.cached) {
		/* Starting cache? Set cache mtime. */
		if (!ctx->pos && !fi->rdc.size) {
			fi->rdc.mtime = inode->i_mtime;
			fi->rdc.iversion = inode_query_iversion(inode);
		}
		spin_unlock(&fi->rdc.lock);
		return UNCACHED;
	}
	/*
	 * When at the beginning of the directory (i.e. just after opendir(3) or
	 * rewinddir(3)), then need to check whether directory contents have
	 * changed, and reset the cache if so.
	 */
	if (!ctx->pos) {
		if (inode_peek_iversion(inode) != fi->rdc.iversion ||
		    !timespec64_equal(&fi->rdc.mtime, &inode->i_mtime)) {
			fuse_rdc_reset(inode);
			goto retry_locked;
		}
	}

	/*
	 * If cache version changed since the last getdents() call, then reset
	 * the cache stream.
	 */
	if (ff->readdir.version != fi->rdc.version) {
		ff->readdir.pos = 0;
		ff->readdir.cache_off = 0;
	}
	/*
	 * If at the beginning of the cache, than reset version to
	 * current.
	 */
	if (ff->readdir.pos == 0)
		ff->readdir.version = fi->rdc.version;

	WARN_ON(fi->rdc.size < ff->readdir.cache_off);

	index = ff->readdir.cache_off >> PAGE_SHIFT;

	if (index == (fi->rdc.size >> PAGE_SHIFT))
		size = fi->rdc.size & ~PAGE_MASK;
	else
		size = PAGE_SIZE;
	spin_unlock(&fi->rdc.lock);

	/* EOF? */
	if ((ff->readdir.cache_off & ~PAGE_MASK) == size)
		return 0;

	page = find_get_page_flags(file->f_mapping, index,
				   FGP_ACCESSED | FGP_LOCK);
	/* Page gone missing, then re-added to cache, but not initialized? */
	if (page && !PageUptodate(page)) {
		unlock_page(page);
		put_page(page);
		page = NULL;
	}
	spin_lock(&fi->rdc.lock);
	if (!page) {
		/*
		 * Uh-oh: page gone missing, cache is useless
		 */
		if (fi->rdc.version == ff->readdir.version)
			fuse_rdc_reset(inode);
		goto retry_locked;
	}

	/* Make sure it's still the same version after getting the page. */
	if (ff->readdir.version != fi->rdc.version) {
		spin_unlock(&fi->rdc.lock);
		unlock_page(page);
		put_page(page);
		goto retry;
	}
	spin_unlock(&fi->rdc.lock);

	/*
	 * Contents of the page are now protected against changing by holding
	 * the page lock.
	 */
	addr = kmap(page);
	res = fuse_parse_cache(ff, addr, size, ctx);
	kunmap(page);
	unlock_page(page);
	put_page(page);

	if (res == FOUND_ERR)
		return -EIO;

	if (res == FOUND_ALL)
		return 0;

	if (size == PAGE_SIZE) {
		/* We hit end of page: skip to next page. */
		ff->readdir.cache_off = ALIGN(ff->readdir.cache_off, PAGE_SIZE);
		goto retry;
	}

	/*
	 * End of cache reached.  If found position, then we are done, otherwise
	 * need to fall back to uncached, since the position we were looking for
	 * wasn't in the cache.
	 */
	return res == FOUND_SOME ? 0 : UNCACHED;
}

int fuse_readdir(struct file *file, struct dir_context *ctx)
{
	struct fuse_file *ff = file->private_data;
	struct inode *inode = file_inode(file);
	int err;

	if (fuse_is_bad(inode))
		return -EIO;

	mutex_lock(&ff->readdir.lock);

	err = UNCACHED;
	if (ff->open_flags & FOPEN_CACHE_DIR)
		err = fuse_readdir_cached(file, ctx);
	if (err == UNCACHED)
		err = fuse_readdir_uncached(file, ctx);

	mutex_unlock(&ff->readdir.lock);

	return err;
}
