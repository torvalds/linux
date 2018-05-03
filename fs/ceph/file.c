// SPDX-License-Identifier: GPL-2.0
#include <linux/ceph/ceph_debug.h>

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/writeback.h>
#include <linux/falloc.h>

#include "super.h"
#include "mds_client.h"
#include "cache.h"

static __le32 ceph_flags_sys2wire(u32 flags)
{
	u32 wire_flags = 0;

	switch (flags & O_ACCMODE) {
	case O_RDONLY:
		wire_flags |= CEPH_O_RDONLY;
		break;
	case O_WRONLY:
		wire_flags |= CEPH_O_WRONLY;
		break;
	case O_RDWR:
		wire_flags |= CEPH_O_RDWR;
		break;
	}

#define ceph_sys2wire(a) if (flags & a) { wire_flags |= CEPH_##a; flags &= ~a; }

	ceph_sys2wire(O_CREAT);
	ceph_sys2wire(O_EXCL);
	ceph_sys2wire(O_TRUNC);
	ceph_sys2wire(O_DIRECTORY);
	ceph_sys2wire(O_NOFOLLOW);

#undef ceph_sys2wire

	if (flags)
		dout("unused open flags: %x", flags);

	return cpu_to_le32(wire_flags);
}

/*
 * Ceph file operations
 *
 * Implement basic open/close functionality, and implement
 * read/write.
 *
 * We implement three modes of file I/O:
 *  - buffered uses the generic_file_aio_{read,write} helpers
 *
 *  - synchronous is used when there is multi-client read/write
 *    sharing, avoids the page cache, and synchronously waits for an
 *    ack from the OSD.
 *
 *  - direct io takes the variant of the sync path that references
 *    user pages directly.
 *
 * fsync() flushes and waits on dirty pages, but just queues metadata
 * for writeback: since the MDS can recover size and mtime there is no
 * need to wait for MDS acknowledgement.
 */

/*
 * Calculate the length sum of direct io vectors that can
 * be combined into one page vector.
 */
static size_t dio_get_pagev_size(const struct iov_iter *it)
{
    const struct iovec *iov = it->iov;
    const struct iovec *iovend = iov + it->nr_segs;
    size_t size;

    size = iov->iov_len - it->iov_offset;
    /*
     * An iov can be page vectored when both the current tail
     * and the next base are page aligned.
     */
    while (PAGE_ALIGNED((iov->iov_base + iov->iov_len)) &&
           (++iov < iovend && PAGE_ALIGNED((iov->iov_base)))) {
        size += iov->iov_len;
    }
    dout("dio_get_pagevlen len = %zu\n", size);
    return size;
}

/*
 * Allocate a page vector based on (@it, @nbytes).
 * The return value is the tuple describing a page vector,
 * that is (@pages, @page_align, @num_pages).
 */
static struct page **
dio_get_pages_alloc(const struct iov_iter *it, size_t nbytes,
		    size_t *page_align, int *num_pages)
{
	struct iov_iter tmp_it = *it;
	size_t align;
	struct page **pages;
	int ret = 0, idx, npages;

	align = (unsigned long)(it->iov->iov_base + it->iov_offset) &
		(PAGE_SIZE - 1);
	npages = calc_pages_for(align, nbytes);
	pages = kvmalloc(sizeof(*pages) * npages, GFP_KERNEL);
	if (!pages)
		return ERR_PTR(-ENOMEM);

	for (idx = 0; idx < npages; ) {
		size_t start;
		ret = iov_iter_get_pages(&tmp_it, pages + idx, nbytes,
					 npages - idx, &start);
		if (ret < 0)
			goto fail;

		iov_iter_advance(&tmp_it, ret);
		nbytes -= ret;
		idx += (ret + start + PAGE_SIZE - 1) / PAGE_SIZE;
	}

	BUG_ON(nbytes != 0);
	*num_pages = npages;
	*page_align = align;
	dout("dio_get_pages_alloc: got %d pages align %zu\n", npages, align);
	return pages;
fail:
	ceph_put_page_vector(pages, idx, false);
	return ERR_PTR(ret);
}

/*
 * Prepare an open request.  Preallocate ceph_cap to avoid an
 * inopportune ENOMEM later.
 */
static struct ceph_mds_request *
prepare_open_request(struct super_block *sb, int flags, int create_mode)
{
	struct ceph_fs_client *fsc = ceph_sb_to_client(sb);
	struct ceph_mds_client *mdsc = fsc->mdsc;
	struct ceph_mds_request *req;
	int want_auth = USE_ANY_MDS;
	int op = (flags & O_CREAT) ? CEPH_MDS_OP_CREATE : CEPH_MDS_OP_OPEN;

	if (flags & (O_WRONLY|O_RDWR|O_CREAT|O_TRUNC))
		want_auth = USE_AUTH_MDS;

	req = ceph_mdsc_create_request(mdsc, op, want_auth);
	if (IS_ERR(req))
		goto out;
	req->r_fmode = ceph_flags_to_mode(flags);
	req->r_args.open.flags = ceph_flags_sys2wire(flags);
	req->r_args.open.mode = cpu_to_le32(create_mode);
out:
	return req;
}

/*
 * initialize private struct file data.
 * if we fail, clean up by dropping fmode reference on the ceph_inode
 */
static int ceph_init_file(struct inode *inode, struct file *file, int fmode)
{
	struct ceph_file_info *cf;
	int ret = 0;

	switch (inode->i_mode & S_IFMT) {
	case S_IFREG:
		ceph_fscache_register_inode_cookie(inode);
		ceph_fscache_file_set_cookie(inode, file);
	case S_IFDIR:
		dout("init_file %p %p 0%o (regular)\n", inode, file,
		     inode->i_mode);
		cf = kmem_cache_zalloc(ceph_file_cachep, GFP_KERNEL);
		if (!cf) {
			ceph_put_fmode(ceph_inode(inode), fmode); /* clean up */
			return -ENOMEM;
		}
		cf->fmode = fmode;
		cf->next_offset = 2;
		cf->readdir_cache_idx = -1;
		file->private_data = cf;
		BUG_ON(inode->i_fop->release != ceph_release);
		break;

	case S_IFLNK:
		dout("init_file %p %p 0%o (symlink)\n", inode, file,
		     inode->i_mode);
		ceph_put_fmode(ceph_inode(inode), fmode); /* clean up */
		break;

	default:
		dout("init_file %p %p 0%o (special)\n", inode, file,
		     inode->i_mode);
		/*
		 * we need to drop the open ref now, since we don't
		 * have .release set to ceph_release.
		 */
		ceph_put_fmode(ceph_inode(inode), fmode); /* clean up */
		BUG_ON(inode->i_fop->release == ceph_release);

		/* call the proper open fop */
		ret = inode->i_fop->open(inode, file);
	}
	return ret;
}

/*
 * try renew caps after session gets killed.
 */
int ceph_renew_caps(struct inode *inode)
{
	struct ceph_mds_client *mdsc = ceph_sb_to_client(inode->i_sb)->mdsc;
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_mds_request *req;
	int err, flags, wanted;

	spin_lock(&ci->i_ceph_lock);
	wanted = __ceph_caps_file_wanted(ci);
	if (__ceph_is_any_real_caps(ci) &&
	    (!(wanted & CEPH_CAP_ANY_WR) || ci->i_auth_cap)) {
		int issued = __ceph_caps_issued(ci, NULL);
		spin_unlock(&ci->i_ceph_lock);
		dout("renew caps %p want %s issued %s updating mds_wanted\n",
		     inode, ceph_cap_string(wanted), ceph_cap_string(issued));
		ceph_check_caps(ci, 0, NULL);
		return 0;
	}
	spin_unlock(&ci->i_ceph_lock);

	flags = 0;
	if ((wanted & CEPH_CAP_FILE_RD) && (wanted & CEPH_CAP_FILE_WR))
		flags = O_RDWR;
	else if (wanted & CEPH_CAP_FILE_RD)
		flags = O_RDONLY;
	else if (wanted & CEPH_CAP_FILE_WR)
		flags = O_WRONLY;
#ifdef O_LAZY
	if (wanted & CEPH_CAP_FILE_LAZYIO)
		flags |= O_LAZY;
#endif

	req = prepare_open_request(inode->i_sb, flags, 0);
	if (IS_ERR(req)) {
		err = PTR_ERR(req);
		goto out;
	}

	req->r_inode = inode;
	ihold(inode);
	req->r_num_caps = 1;
	req->r_fmode = -1;

	err = ceph_mdsc_do_request(mdsc, NULL, req);
	ceph_mdsc_put_request(req);
out:
	dout("renew caps %p open result=%d\n", inode, err);
	return err < 0 ? err : 0;
}

/*
 * If we already have the requisite capabilities, we can satisfy
 * the open request locally (no need to request new caps from the
 * MDS).  We do, however, need to inform the MDS (asynchronously)
 * if our wanted caps set expands.
 */
int ceph_open(struct inode *inode, struct file *file)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_fs_client *fsc = ceph_sb_to_client(inode->i_sb);
	struct ceph_mds_client *mdsc = fsc->mdsc;
	struct ceph_mds_request *req;
	struct ceph_file_info *cf = file->private_data;
	int err;
	int flags, fmode, wanted;

	if (cf) {
		dout("open file %p is already opened\n", file);
		return 0;
	}

	/* filter out O_CREAT|O_EXCL; vfs did that already.  yuck. */
	flags = file->f_flags & ~(O_CREAT|O_EXCL);
	if (S_ISDIR(inode->i_mode))
		flags = O_DIRECTORY;  /* mds likes to know */

	dout("open inode %p ino %llx.%llx file %p flags %d (%d)\n", inode,
	     ceph_vinop(inode), file, flags, file->f_flags);
	fmode = ceph_flags_to_mode(flags);
	wanted = ceph_caps_for_mode(fmode);

	/* snapped files are read-only */
	if (ceph_snap(inode) != CEPH_NOSNAP && (file->f_mode & FMODE_WRITE))
		return -EROFS;

	/* trivially open snapdir */
	if (ceph_snap(inode) == CEPH_SNAPDIR) {
		spin_lock(&ci->i_ceph_lock);
		__ceph_get_fmode(ci, fmode);
		spin_unlock(&ci->i_ceph_lock);
		return ceph_init_file(inode, file, fmode);
	}

	/*
	 * No need to block if we have caps on the auth MDS (for
	 * write) or any MDS (for read).  Update wanted set
	 * asynchronously.
	 */
	spin_lock(&ci->i_ceph_lock);
	if (__ceph_is_any_real_caps(ci) &&
	    (((fmode & CEPH_FILE_MODE_WR) == 0) || ci->i_auth_cap)) {
		int mds_wanted = __ceph_caps_mds_wanted(ci, true);
		int issued = __ceph_caps_issued(ci, NULL);

		dout("open %p fmode %d want %s issued %s using existing\n",
		     inode, fmode, ceph_cap_string(wanted),
		     ceph_cap_string(issued));
		__ceph_get_fmode(ci, fmode);
		spin_unlock(&ci->i_ceph_lock);

		/* adjust wanted? */
		if ((issued & wanted) != wanted &&
		    (mds_wanted & wanted) != wanted &&
		    ceph_snap(inode) != CEPH_SNAPDIR)
			ceph_check_caps(ci, 0, NULL);

		return ceph_init_file(inode, file, fmode);
	} else if (ceph_snap(inode) != CEPH_NOSNAP &&
		   (ci->i_snap_caps & wanted) == wanted) {
		__ceph_get_fmode(ci, fmode);
		spin_unlock(&ci->i_ceph_lock);
		return ceph_init_file(inode, file, fmode);
	}

	spin_unlock(&ci->i_ceph_lock);

	dout("open fmode %d wants %s\n", fmode, ceph_cap_string(wanted));
	req = prepare_open_request(inode->i_sb, flags, 0);
	if (IS_ERR(req)) {
		err = PTR_ERR(req);
		goto out;
	}
	req->r_inode = inode;
	ihold(inode);

	req->r_num_caps = 1;
	err = ceph_mdsc_do_request(mdsc, NULL, req);
	if (!err)
		err = ceph_init_file(inode, file, req->r_fmode);
	ceph_mdsc_put_request(req);
	dout("open result=%d on %llx.%llx\n", err, ceph_vinop(inode));
out:
	return err;
}


/*
 * Do a lookup + open with a single request.  If we get a non-existent
 * file or symlink, return 1 so the VFS can retry.
 */
int ceph_atomic_open(struct inode *dir, struct dentry *dentry,
		     struct file *file, unsigned flags, umode_t mode,
		     int *opened)
{
	struct ceph_fs_client *fsc = ceph_sb_to_client(dir->i_sb);
	struct ceph_mds_client *mdsc = fsc->mdsc;
	struct ceph_mds_request *req;
	struct dentry *dn;
	struct ceph_acls_info acls = {};
       int mask;
	int err;

	dout("atomic_open %p dentry %p '%pd' %s flags %d mode 0%o\n",
	     dir, dentry, dentry,
	     d_unhashed(dentry) ? "unhashed" : "hashed", flags, mode);

	if (dentry->d_name.len > NAME_MAX)
		return -ENAMETOOLONG;

	if (flags & O_CREAT) {
		err = ceph_pre_init_acls(dir, &mode, &acls);
		if (err < 0)
			return err;
	}

	/* do the open */
	req = prepare_open_request(dir->i_sb, flags, mode);
	if (IS_ERR(req)) {
		err = PTR_ERR(req);
		goto out_acl;
	}
	req->r_dentry = dget(dentry);
	req->r_num_caps = 2;
	if (flags & O_CREAT) {
		req->r_dentry_drop = CEPH_CAP_FILE_SHARED;
		req->r_dentry_unless = CEPH_CAP_FILE_EXCL;
		if (acls.pagelist) {
			req->r_pagelist = acls.pagelist;
			acls.pagelist = NULL;
		}
	}

       mask = CEPH_STAT_CAP_INODE | CEPH_CAP_AUTH_SHARED;
       if (ceph_security_xattr_wanted(dir))
               mask |= CEPH_CAP_XATTR_SHARED;
       req->r_args.open.mask = cpu_to_le32(mask);

	req->r_parent = dir;
	set_bit(CEPH_MDS_R_PARENT_LOCKED, &req->r_req_flags);
	err = ceph_mdsc_do_request(mdsc,
				   (flags & (O_CREAT|O_TRUNC)) ? dir : NULL,
				   req);
	err = ceph_handle_snapdir(req, dentry, err);
	if (err)
		goto out_req;

	if ((flags & O_CREAT) && !req->r_reply_info.head->is_dentry)
		err = ceph_handle_notrace_create(dir, dentry);

	if (d_in_lookup(dentry)) {
		dn = ceph_finish_lookup(req, dentry, err);
		if (IS_ERR(dn))
			err = PTR_ERR(dn);
	} else {
		/* we were given a hashed negative dentry */
		dn = NULL;
	}
	if (err)
		goto out_req;
	if (dn || d_really_is_negative(dentry) || d_is_symlink(dentry)) {
		/* make vfs retry on splice, ENOENT, or symlink */
		dout("atomic_open finish_no_open on dn %p\n", dn);
		err = finish_no_open(file, dn);
	} else {
		dout("atomic_open finish_open on dn %p\n", dn);
		if (req->r_op == CEPH_MDS_OP_CREATE && req->r_reply_info.has_create_ino) {
			ceph_init_inode_acls(d_inode(dentry), &acls);
			*opened |= FILE_CREATED;
		}
		err = finish_open(file, dentry, ceph_open, opened);
	}
out_req:
	if (!req->r_err && req->r_target_inode)
		ceph_put_fmode(ceph_inode(req->r_target_inode), req->r_fmode);
	ceph_mdsc_put_request(req);
out_acl:
	ceph_release_acls_info(&acls);
	dout("atomic_open result=%d\n", err);
	return err;
}

int ceph_release(struct inode *inode, struct file *file)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_file_info *cf = file->private_data;

	dout("release inode %p file %p\n", inode, file);
	ceph_put_fmode(ci, cf->fmode);
	if (cf->last_readdir)
		ceph_mdsc_put_request(cf->last_readdir);
	kfree(cf->last_name);
	kfree(cf->dir_info);
	kmem_cache_free(ceph_file_cachep, cf);

	/* wake up anyone waiting for caps on this inode */
	wake_up_all(&ci->i_cap_wq);
	return 0;
}

enum {
	HAVE_RETRIED = 1,
	CHECK_EOF =    2,
	READ_INLINE =  3,
};

/*
 * Read a range of bytes striped over one or more objects.  Iterate over
 * objects we stripe over.  (That's not atomic, but good enough for now.)
 *
 * If we get a short result from the OSD, check against i_size; we need to
 * only return a short read to the caller if we hit EOF.
 */
static int striped_read(struct inode *inode,
			u64 pos, u64 len,
			struct page **pages, int num_pages,
			int page_align, int *checkeof)
{
	struct ceph_fs_client *fsc = ceph_inode_to_client(inode);
	struct ceph_inode_info *ci = ceph_inode(inode);
	u64 this_len;
	loff_t i_size;
	int page_idx;
	int ret, read = 0;
	bool hit_stripe, was_short;

	/*
	 * we may need to do multiple reads.  not atomic, unfortunately.
	 */
more:
	this_len = len;
	page_idx = (page_align + read) >> PAGE_SHIFT;
	ret = ceph_osdc_readpages(&fsc->client->osdc, ceph_vino(inode),
				  &ci->i_layout, pos, &this_len,
				  ci->i_truncate_seq, ci->i_truncate_size,
				  pages + page_idx, num_pages - page_idx,
				  ((page_align + read) & ~PAGE_MASK));
	if (ret == -ENOENT)
		ret = 0;
	hit_stripe = this_len < len;
	was_short = ret >= 0 && ret < this_len;
	dout("striped_read %llu~%llu (read %u) got %d%s%s\n", pos, len, read,
	     ret, hit_stripe ? " HITSTRIPE" : "", was_short ? " SHORT" : "");

	i_size = i_size_read(inode);
	if (ret >= 0) {
		if (was_short && (pos + ret < i_size)) {
			int zlen = min(this_len - ret, i_size - pos - ret);
			int zoff = page_align + read + ret;
			dout(" zero gap %llu to %llu\n",
			     pos + ret, pos + ret + zlen);
			ceph_zero_page_vector_range(zoff, zlen, pages);
			ret += zlen;
		}

		read += ret;
		pos += ret;
		len -= ret;

		/* hit stripe and need continue*/
		if (len && hit_stripe && pos < i_size)
			goto more;
	}

	if (read > 0) {
		ret = read;
		/* did we bounce off eof? */
		if (pos + len > i_size)
			*checkeof = CHECK_EOF;
	}

	dout("striped_read returns %d\n", ret);
	return ret;
}

/*
 * Completely synchronous read and write methods.  Direct from __user
 * buffer to osd, or directly to user pages (if O_DIRECT).
 *
 * If the read spans object boundary, just do multiple reads.
 */
static ssize_t ceph_sync_read(struct kiocb *iocb, struct iov_iter *to,
			      int *checkeof)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file_inode(file);
	struct page **pages;
	u64 off = iocb->ki_pos;
	int num_pages;
	ssize_t ret;
	size_t len = iov_iter_count(to);

	dout("sync_read on file %p %llu~%u %s\n", file, off, (unsigned)len,
	     (file->f_flags & O_DIRECT) ? "O_DIRECT" : "");

	if (!len)
		return 0;
	/*
	 * flush any page cache pages in this range.  this
	 * will make concurrent normal and sync io slow,
	 * but it will at least behave sensibly when they are
	 * in sequence.
	 */
	ret = filemap_write_and_wait_range(inode->i_mapping, off,
						off + len);
	if (ret < 0)
		return ret;

	if (unlikely(to->type & ITER_PIPE)) {
		size_t page_off;
		ret = iov_iter_get_pages_alloc(to, &pages, len,
					       &page_off);
		if (ret <= 0)
			return -ENOMEM;
		num_pages = DIV_ROUND_UP(ret + page_off, PAGE_SIZE);

		ret = striped_read(inode, off, ret, pages, num_pages,
				   page_off, checkeof);
		if (ret > 0) {
			iov_iter_advance(to, ret);
			off += ret;
		} else {
			iov_iter_advance(to, 0);
		}
		ceph_put_page_vector(pages, num_pages, false);
	} else {
		num_pages = calc_pages_for(off, len);
		pages = ceph_alloc_page_vector(num_pages, GFP_KERNEL);
		if (IS_ERR(pages))
			return PTR_ERR(pages);

		ret = striped_read(inode, off, len, pages, num_pages,
				   (off & ~PAGE_MASK), checkeof);
		if (ret > 0) {
			int l, k = 0;
			size_t left = ret;

			while (left) {
				size_t page_off = off & ~PAGE_MASK;
				size_t copy = min_t(size_t, left,
						    PAGE_SIZE - page_off);
				l = copy_page_to_iter(pages[k++], page_off,
						      copy, to);
				off += l;
				left -= l;
				if (l < copy)
					break;
			}
		}
		ceph_release_page_vector(pages, num_pages);
	}

	if (off > iocb->ki_pos) {
		ret = off - iocb->ki_pos;
		iocb->ki_pos = off;
	}

	dout("sync_read result %zd\n", ret);
	return ret;
}

struct ceph_aio_request {
	struct kiocb *iocb;
	size_t total_len;
	bool write;
	bool should_dirty;
	int error;
	struct list_head osd_reqs;
	unsigned num_reqs;
	atomic_t pending_reqs;
	struct timespec mtime;
	struct ceph_cap_flush *prealloc_cf;
};

struct ceph_aio_work {
	struct work_struct work;
	struct ceph_osd_request *req;
};

static void ceph_aio_retry_work(struct work_struct *work);

static void ceph_aio_complete(struct inode *inode,
			      struct ceph_aio_request *aio_req)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	int ret;

	if (!atomic_dec_and_test(&aio_req->pending_reqs))
		return;

	ret = aio_req->error;
	if (!ret)
		ret = aio_req->total_len;

	dout("ceph_aio_complete %p rc %d\n", inode, ret);

	if (ret >= 0 && aio_req->write) {
		int dirty;

		loff_t endoff = aio_req->iocb->ki_pos + aio_req->total_len;
		if (endoff > i_size_read(inode)) {
			if (ceph_inode_set_size(inode, endoff))
				ceph_check_caps(ci, CHECK_CAPS_AUTHONLY, NULL);
		}

		spin_lock(&ci->i_ceph_lock);
		ci->i_inline_version = CEPH_INLINE_NONE;
		dirty = __ceph_mark_dirty_caps(ci, CEPH_CAP_FILE_WR,
					       &aio_req->prealloc_cf);
		spin_unlock(&ci->i_ceph_lock);
		if (dirty)
			__mark_inode_dirty(inode, dirty);

	}

	ceph_put_cap_refs(ci, (aio_req->write ? CEPH_CAP_FILE_WR :
						CEPH_CAP_FILE_RD));

	aio_req->iocb->ki_complete(aio_req->iocb, ret, 0);

	ceph_free_cap_flush(aio_req->prealloc_cf);
	kfree(aio_req);
}

static void ceph_aio_complete_req(struct ceph_osd_request *req)
{
	int rc = req->r_result;
	struct inode *inode = req->r_inode;
	struct ceph_aio_request *aio_req = req->r_priv;
	struct ceph_osd_data *osd_data = osd_req_op_extent_osd_data(req, 0);
	int num_pages = calc_pages_for((u64)osd_data->alignment,
				       osd_data->length);

	dout("ceph_aio_complete_req %p rc %d bytes %llu\n",
	     inode, rc, osd_data->length);

	if (rc == -EOLDSNAPC) {
		struct ceph_aio_work *aio_work;
		BUG_ON(!aio_req->write);

		aio_work = kmalloc(sizeof(*aio_work), GFP_NOFS);
		if (aio_work) {
			INIT_WORK(&aio_work->work, ceph_aio_retry_work);
			aio_work->req = req;
			queue_work(ceph_inode_to_client(inode)->wb_wq,
				   &aio_work->work);
			return;
		}
		rc = -ENOMEM;
	} else if (!aio_req->write) {
		if (rc == -ENOENT)
			rc = 0;
		if (rc >= 0 && osd_data->length > rc) {
			int zoff = osd_data->alignment + rc;
			int zlen = osd_data->length - rc;
			/*
			 * If read is satisfied by single OSD request,
			 * it can pass EOF. Otherwise read is within
			 * i_size.
			 */
			if (aio_req->num_reqs == 1) {
				loff_t i_size = i_size_read(inode);
				loff_t endoff = aio_req->iocb->ki_pos + rc;
				if (endoff < i_size)
					zlen = min_t(size_t, zlen,
						     i_size - endoff);
				aio_req->total_len = rc + zlen;
			}

			if (zlen > 0)
				ceph_zero_page_vector_range(zoff, zlen,
							    osd_data->pages);
		}
	}

	ceph_put_page_vector(osd_data->pages, num_pages, aio_req->should_dirty);
	ceph_osdc_put_request(req);

	if (rc < 0)
		cmpxchg(&aio_req->error, 0, rc);

	ceph_aio_complete(inode, aio_req);
	return;
}

static void ceph_aio_retry_work(struct work_struct *work)
{
	struct ceph_aio_work *aio_work =
		container_of(work, struct ceph_aio_work, work);
	struct ceph_osd_request *orig_req = aio_work->req;
	struct ceph_aio_request *aio_req = orig_req->r_priv;
	struct inode *inode = orig_req->r_inode;
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_snap_context *snapc;
	struct ceph_osd_request *req;
	int ret;

	spin_lock(&ci->i_ceph_lock);
	if (__ceph_have_pending_cap_snap(ci)) {
		struct ceph_cap_snap *capsnap =
			list_last_entry(&ci->i_cap_snaps,
					struct ceph_cap_snap,
					ci_item);
		snapc = ceph_get_snap_context(capsnap->context);
	} else {
		BUG_ON(!ci->i_head_snapc);
		snapc = ceph_get_snap_context(ci->i_head_snapc);
	}
	spin_unlock(&ci->i_ceph_lock);

	req = ceph_osdc_alloc_request(orig_req->r_osdc, snapc, 2,
			false, GFP_NOFS);
	if (!req) {
		ret = -ENOMEM;
		req = orig_req;
		goto out;
	}

	req->r_flags = /* CEPH_OSD_FLAG_ORDERSNAP | */ CEPH_OSD_FLAG_WRITE;
	ceph_oloc_copy(&req->r_base_oloc, &orig_req->r_base_oloc);
	ceph_oid_copy(&req->r_base_oid, &orig_req->r_base_oid);

	ret = ceph_osdc_alloc_messages(req, GFP_NOFS);
	if (ret) {
		ceph_osdc_put_request(req);
		req = orig_req;
		goto out;
	}

	req->r_ops[0] = orig_req->r_ops[0];

	req->r_mtime = aio_req->mtime;
	req->r_data_offset = req->r_ops[0].extent.offset;

	ceph_osdc_put_request(orig_req);

	req->r_callback = ceph_aio_complete_req;
	req->r_inode = inode;
	req->r_priv = aio_req;
	req->r_abort_on_full = true;

	ret = ceph_osdc_start_request(req->r_osdc, req, false);
out:
	if (ret < 0) {
		req->r_result = ret;
		ceph_aio_complete_req(req);
	}

	ceph_put_snap_context(snapc);
	kfree(aio_work);
}

static ssize_t
ceph_direct_read_write(struct kiocb *iocb, struct iov_iter *iter,
		       struct ceph_snap_context *snapc,
		       struct ceph_cap_flush **pcf)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file_inode(file);
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_fs_client *fsc = ceph_inode_to_client(inode);
	struct ceph_vino vino;
	struct ceph_osd_request *req;
	struct page **pages;
	struct ceph_aio_request *aio_req = NULL;
	int num_pages = 0;
	int flags;
	int ret;
	struct timespec mtime = current_time(inode);
	size_t count = iov_iter_count(iter);
	loff_t pos = iocb->ki_pos;
	bool write = iov_iter_rw(iter) == WRITE;
	bool should_dirty = !write && iter_is_iovec(iter);

	if (write && ceph_snap(file_inode(file)) != CEPH_NOSNAP)
		return -EROFS;

	dout("sync_direct_%s on file %p %lld~%u snapc %p seq %lld\n",
	     (write ? "write" : "read"), file, pos, (unsigned)count,
	     snapc, snapc->seq);

	ret = filemap_write_and_wait_range(inode->i_mapping, pos, pos + count);
	if (ret < 0)
		return ret;

	if (write) {
		int ret2 = invalidate_inode_pages2_range(inode->i_mapping,
					pos >> PAGE_SHIFT,
					(pos + count) >> PAGE_SHIFT);
		if (ret2 < 0)
			dout("invalidate_inode_pages2_range returned %d\n", ret2);

		flags = /* CEPH_OSD_FLAG_ORDERSNAP | */ CEPH_OSD_FLAG_WRITE;
	} else {
		flags = CEPH_OSD_FLAG_READ;
	}

	while (iov_iter_count(iter) > 0) {
		u64 size = dio_get_pagev_size(iter);
		size_t start = 0;
		ssize_t len;

		if (write)
			size = min_t(u64, size, fsc->mount_options->wsize);
		else
			size = min_t(u64, size, fsc->mount_options->rsize);

		vino = ceph_vino(inode);
		req = ceph_osdc_new_request(&fsc->client->osdc, &ci->i_layout,
					    vino, pos, &size, 0,
					    1,
					    write ? CEPH_OSD_OP_WRITE :
						    CEPH_OSD_OP_READ,
					    flags, snapc,
					    ci->i_truncate_seq,
					    ci->i_truncate_size,
					    false);
		if (IS_ERR(req)) {
			ret = PTR_ERR(req);
			break;
		}

		len = size;
		pages = dio_get_pages_alloc(iter, len, &start, &num_pages);
		if (IS_ERR(pages)) {
			ceph_osdc_put_request(req);
			ret = PTR_ERR(pages);
			break;
		}

		/*
		 * To simplify error handling, allow AIO when IO within i_size
		 * or IO can be satisfied by single OSD request.
		 */
		if (pos == iocb->ki_pos && !is_sync_kiocb(iocb) &&
		    (len == count || pos + count <= i_size_read(inode))) {
			aio_req = kzalloc(sizeof(*aio_req), GFP_KERNEL);
			if (aio_req) {
				aio_req->iocb = iocb;
				aio_req->write = write;
				aio_req->should_dirty = should_dirty;
				INIT_LIST_HEAD(&aio_req->osd_reqs);
				if (write) {
					aio_req->mtime = mtime;
					swap(aio_req->prealloc_cf, *pcf);
				}
			}
			/* ignore error */
		}

		if (write) {
			/*
			 * throw out any page cache pages in this range. this
			 * may block.
			 */
			truncate_inode_pages_range(inode->i_mapping, pos,
					(pos+len) | (PAGE_SIZE - 1));

			req->r_mtime = mtime;
		}

		osd_req_op_extent_osd_data_pages(req, 0, pages, len, start,
						 false, false);

		if (aio_req) {
			aio_req->total_len += len;
			aio_req->num_reqs++;
			atomic_inc(&aio_req->pending_reqs);

			req->r_callback = ceph_aio_complete_req;
			req->r_inode = inode;
			req->r_priv = aio_req;
			list_add_tail(&req->r_unsafe_item, &aio_req->osd_reqs);

			pos += len;
			iov_iter_advance(iter, len);
			continue;
		}

		ret = ceph_osdc_start_request(req->r_osdc, req, false);
		if (!ret)
			ret = ceph_osdc_wait_request(&fsc->client->osdc, req);

		size = i_size_read(inode);
		if (!write) {
			if (ret == -ENOENT)
				ret = 0;
			if (ret >= 0 && ret < len && pos + ret < size) {
				int zlen = min_t(size_t, len - ret,
						 size - pos - ret);
				ceph_zero_page_vector_range(start + ret, zlen,
							    pages);
				ret += zlen;
			}
			if (ret >= 0)
				len = ret;
		}

		ceph_put_page_vector(pages, num_pages, should_dirty);

		ceph_osdc_put_request(req);
		if (ret < 0)
			break;

		pos += len;
		iov_iter_advance(iter, len);

		if (!write && pos >= size)
			break;

		if (write && pos > size) {
			if (ceph_inode_set_size(inode, pos))
				ceph_check_caps(ceph_inode(inode),
						CHECK_CAPS_AUTHONLY,
						NULL);
		}
	}

	if (aio_req) {
		LIST_HEAD(osd_reqs);

		if (aio_req->num_reqs == 0) {
			kfree(aio_req);
			return ret;
		}

		ceph_get_cap_refs(ci, write ? CEPH_CAP_FILE_WR :
					      CEPH_CAP_FILE_RD);

		list_splice(&aio_req->osd_reqs, &osd_reqs);
		while (!list_empty(&osd_reqs)) {
			req = list_first_entry(&osd_reqs,
					       struct ceph_osd_request,
					       r_unsafe_item);
			list_del_init(&req->r_unsafe_item);
			if (ret >= 0)
				ret = ceph_osdc_start_request(req->r_osdc,
							      req, false);
			if (ret < 0) {
				req->r_result = ret;
				ceph_aio_complete_req(req);
			}
		}
		return -EIOCBQUEUED;
	}

	if (ret != -EOLDSNAPC && pos > iocb->ki_pos) {
		ret = pos - iocb->ki_pos;
		iocb->ki_pos = pos;
	}
	return ret;
}

/*
 * Synchronous write, straight from __user pointer or user pages.
 *
 * If write spans object boundary, just do multiple writes.  (For a
 * correct atomic write, we should e.g. take write locks on all
 * objects, rollback on failure, etc.)
 */
static ssize_t
ceph_sync_write(struct kiocb *iocb, struct iov_iter *from, loff_t pos,
		struct ceph_snap_context *snapc)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file_inode(file);
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_fs_client *fsc = ceph_inode_to_client(inode);
	struct ceph_vino vino;
	struct ceph_osd_request *req;
	struct page **pages;
	u64 len;
	int num_pages;
	int written = 0;
	int flags;
	int ret;
	bool check_caps = false;
	struct timespec mtime = current_time(inode);
	size_t count = iov_iter_count(from);

	if (ceph_snap(file_inode(file)) != CEPH_NOSNAP)
		return -EROFS;

	dout("sync_write on file %p %lld~%u snapc %p seq %lld\n",
	     file, pos, (unsigned)count, snapc, snapc->seq);

	ret = filemap_write_and_wait_range(inode->i_mapping, pos, pos + count);
	if (ret < 0)
		return ret;

	ret = invalidate_inode_pages2_range(inode->i_mapping,
					    pos >> PAGE_SHIFT,
					    (pos + count) >> PAGE_SHIFT);
	if (ret < 0)
		dout("invalidate_inode_pages2_range returned %d\n", ret);

	flags = /* CEPH_OSD_FLAG_ORDERSNAP | */ CEPH_OSD_FLAG_WRITE;

	while ((len = iov_iter_count(from)) > 0) {
		size_t left;
		int n;

		vino = ceph_vino(inode);
		req = ceph_osdc_new_request(&fsc->client->osdc, &ci->i_layout,
					    vino, pos, &len, 0, 1,
					    CEPH_OSD_OP_WRITE, flags, snapc,
					    ci->i_truncate_seq,
					    ci->i_truncate_size,
					    false);
		if (IS_ERR(req)) {
			ret = PTR_ERR(req);
			break;
		}

		/*
		 * write from beginning of first page,
		 * regardless of io alignment
		 */
		num_pages = (len + PAGE_SIZE - 1) >> PAGE_SHIFT;

		pages = ceph_alloc_page_vector(num_pages, GFP_KERNEL);
		if (IS_ERR(pages)) {
			ret = PTR_ERR(pages);
			goto out;
		}

		left = len;
		for (n = 0; n < num_pages; n++) {
			size_t plen = min_t(size_t, left, PAGE_SIZE);
			ret = copy_page_from_iter(pages[n], 0, plen, from);
			if (ret != plen) {
				ret = -EFAULT;
				break;
			}
			left -= ret;
		}

		if (ret < 0) {
			ceph_release_page_vector(pages, num_pages);
			goto out;
		}

		req->r_inode = inode;

		osd_req_op_extent_osd_data_pages(req, 0, pages, len, 0,
						false, true);

		req->r_mtime = mtime;
		ret = ceph_osdc_start_request(&fsc->client->osdc, req, false);
		if (!ret)
			ret = ceph_osdc_wait_request(&fsc->client->osdc, req);

out:
		ceph_osdc_put_request(req);
		if (ret != 0) {
			ceph_set_error_write(ci);
			break;
		}

		ceph_clear_error_write(ci);
		pos += len;
		written += len;
		if (pos > i_size_read(inode)) {
			check_caps = ceph_inode_set_size(inode, pos);
			if (check_caps)
				ceph_check_caps(ceph_inode(inode),
						CHECK_CAPS_AUTHONLY,
						NULL);
		}

	}

	if (ret != -EOLDSNAPC && written > 0) {
		ret = written;
		iocb->ki_pos = pos;
	}
	return ret;
}

/*
 * Wrap generic_file_aio_read with checks for cap bits on the inode.
 * Atomically grab references, so that those bits are not released
 * back to the MDS mid-read.
 *
 * Hmm, the sync read case isn't actually async... should it be?
 */
static ssize_t ceph_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	struct file *filp = iocb->ki_filp;
	struct ceph_file_info *fi = filp->private_data;
	size_t len = iov_iter_count(to);
	struct inode *inode = file_inode(filp);
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct page *pinned_page = NULL;
	ssize_t ret;
	int want, got = 0;
	int retry_op = 0, read = 0;

again:
	dout("aio_read %p %llx.%llx %llu~%u trying to get caps on %p\n",
	     inode, ceph_vinop(inode), iocb->ki_pos, (unsigned)len, inode);

	if (fi->fmode & CEPH_FILE_MODE_LAZY)
		want = CEPH_CAP_FILE_CACHE | CEPH_CAP_FILE_LAZYIO;
	else
		want = CEPH_CAP_FILE_CACHE;
	ret = ceph_get_caps(ci, CEPH_CAP_FILE_RD, want, -1, &got, &pinned_page);
	if (ret < 0)
		return ret;

	if ((got & (CEPH_CAP_FILE_CACHE|CEPH_CAP_FILE_LAZYIO)) == 0 ||
	    (iocb->ki_flags & IOCB_DIRECT) ||
	    (fi->flags & CEPH_F_SYNC)) {

		dout("aio_sync_read %p %llx.%llx %llu~%u got cap refs on %s\n",
		     inode, ceph_vinop(inode), iocb->ki_pos, (unsigned)len,
		     ceph_cap_string(got));

		if (ci->i_inline_version == CEPH_INLINE_NONE) {
			if (!retry_op && (iocb->ki_flags & IOCB_DIRECT)) {
				ret = ceph_direct_read_write(iocb, to,
							     NULL, NULL);
				if (ret >= 0 && ret < len)
					retry_op = CHECK_EOF;
			} else {
				ret = ceph_sync_read(iocb, to, &retry_op);
			}
		} else {
			retry_op = READ_INLINE;
		}
	} else {
		dout("aio_read %p %llx.%llx %llu~%u got cap refs on %s\n",
		     inode, ceph_vinop(inode), iocb->ki_pos, (unsigned)len,
		     ceph_cap_string(got));
		current->journal_info = filp;
		ret = generic_file_read_iter(iocb, to);
		current->journal_info = NULL;
	}
	dout("aio_read %p %llx.%llx dropping cap refs on %s = %d\n",
	     inode, ceph_vinop(inode), ceph_cap_string(got), (int)ret);
	if (pinned_page) {
		put_page(pinned_page);
		pinned_page = NULL;
	}
	ceph_put_cap_refs(ci, got);
	if (retry_op > HAVE_RETRIED && ret >= 0) {
		int statret;
		struct page *page = NULL;
		loff_t i_size;
		if (retry_op == READ_INLINE) {
			page = __page_cache_alloc(GFP_KERNEL);
			if (!page)
				return -ENOMEM;
		}

		statret = __ceph_do_getattr(inode, page,
					    CEPH_STAT_CAP_INLINE_DATA, !!page);
		if (statret < 0) {
			if (page)
				__free_page(page);
			if (statret == -ENODATA) {
				BUG_ON(retry_op != READ_INLINE);
				goto again;
			}
			return statret;
		}

		i_size = i_size_read(inode);
		if (retry_op == READ_INLINE) {
			BUG_ON(ret > 0 || read > 0);
			if (iocb->ki_pos < i_size &&
			    iocb->ki_pos < PAGE_SIZE) {
				loff_t end = min_t(loff_t, i_size,
						   iocb->ki_pos + len);
				end = min_t(loff_t, end, PAGE_SIZE);
				if (statret < end)
					zero_user_segment(page, statret, end);
				ret = copy_page_to_iter(page,
						iocb->ki_pos & ~PAGE_MASK,
						end - iocb->ki_pos, to);
				iocb->ki_pos += ret;
				read += ret;
			}
			if (iocb->ki_pos < i_size && read < len) {
				size_t zlen = min_t(size_t, len - read,
						    i_size - iocb->ki_pos);
				ret = iov_iter_zero(zlen, to);
				iocb->ki_pos += ret;
				read += ret;
			}
			__free_pages(page, 0);
			return read;
		}

		/* hit EOF or hole? */
		if (retry_op == CHECK_EOF && iocb->ki_pos < i_size &&
		    ret < len) {
			dout("sync_read hit hole, ppos %lld < size %lld"
			     ", reading more\n", iocb->ki_pos, i_size);

			read += ret;
			len -= ret;
			retry_op = HAVE_RETRIED;
			goto again;
		}
	}

	if (ret >= 0)
		ret += read;

	return ret;
}

/*
 * Take cap references to avoid releasing caps to MDS mid-write.
 *
 * If we are synchronous, and write with an old snap context, the OSD
 * may return EOLDSNAPC.  In that case, retry the write.. _after_
 * dropping our cap refs and allowing the pending snap to logically
 * complete _before_ this write occurs.
 *
 * If we are near ENOSPC, write synchronously.
 */
static ssize_t ceph_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	struct ceph_file_info *fi = file->private_data;
	struct inode *inode = file_inode(file);
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_osd_client *osdc =
		&ceph_sb_to_client(inode->i_sb)->client->osdc;
	struct ceph_cap_flush *prealloc_cf;
	ssize_t count, written = 0;
	int err, want, got;
	loff_t pos;

	if (ceph_snap(inode) != CEPH_NOSNAP)
		return -EROFS;

	prealloc_cf = ceph_alloc_cap_flush();
	if (!prealloc_cf)
		return -ENOMEM;

retry_snap:
	inode_lock(inode);

	/* We can write back this queue in page reclaim */
	current->backing_dev_info = inode_to_bdi(inode);

	if (iocb->ki_flags & IOCB_APPEND) {
		err = ceph_do_getattr(inode, CEPH_STAT_CAP_SIZE, false);
		if (err < 0)
			goto out;
	}

	err = generic_write_checks(iocb, from);
	if (err <= 0)
		goto out;

	pos = iocb->ki_pos;
	count = iov_iter_count(from);
	err = file_remove_privs(file);
	if (err)
		goto out;

	err = file_update_time(file);
	if (err)
		goto out;

	if (ci->i_inline_version != CEPH_INLINE_NONE) {
		err = ceph_uninline_data(file, NULL);
		if (err < 0)
			goto out;
	}

	/* FIXME: not complete since it doesn't account for being at quota */
	if (ceph_osdmap_flag(osdc, CEPH_OSDMAP_FULL)) {
		err = -ENOSPC;
		goto out;
	}

	dout("aio_write %p %llx.%llx %llu~%zd getting caps. i_size %llu\n",
	     inode, ceph_vinop(inode), pos, count, i_size_read(inode));
	if (fi->fmode & CEPH_FILE_MODE_LAZY)
		want = CEPH_CAP_FILE_BUFFER | CEPH_CAP_FILE_LAZYIO;
	else
		want = CEPH_CAP_FILE_BUFFER;
	got = 0;
	err = ceph_get_caps(ci, CEPH_CAP_FILE_WR, want, pos + count,
			    &got, NULL);
	if (err < 0)
		goto out;

	dout("aio_write %p %llx.%llx %llu~%zd got cap refs on %s\n",
	     inode, ceph_vinop(inode), pos, count, ceph_cap_string(got));

	if ((got & (CEPH_CAP_FILE_BUFFER|CEPH_CAP_FILE_LAZYIO)) == 0 ||
	    (iocb->ki_flags & IOCB_DIRECT) || (fi->flags & CEPH_F_SYNC) ||
	    (ci->i_ceph_flags & CEPH_I_ERROR_WRITE)) {
		struct ceph_snap_context *snapc;
		struct iov_iter data;
		inode_unlock(inode);

		spin_lock(&ci->i_ceph_lock);
		if (__ceph_have_pending_cap_snap(ci)) {
			struct ceph_cap_snap *capsnap =
					list_last_entry(&ci->i_cap_snaps,
							struct ceph_cap_snap,
							ci_item);
			snapc = ceph_get_snap_context(capsnap->context);
		} else {
			BUG_ON(!ci->i_head_snapc);
			snapc = ceph_get_snap_context(ci->i_head_snapc);
		}
		spin_unlock(&ci->i_ceph_lock);

		/* we might need to revert back to that point */
		data = *from;
		if (iocb->ki_flags & IOCB_DIRECT)
			written = ceph_direct_read_write(iocb, &data, snapc,
							 &prealloc_cf);
		else
			written = ceph_sync_write(iocb, &data, pos, snapc);
		if (written > 0)
			iov_iter_advance(from, written);
		ceph_put_snap_context(snapc);
	} else {
		/*
		 * No need to acquire the i_truncate_mutex. Because
		 * the MDS revokes Fwb caps before sending truncate
		 * message to us. We can't get Fwb cap while there
		 * are pending vmtruncate. So write and vmtruncate
		 * can not run at the same time
		 */
		written = generic_perform_write(file, from, pos);
		if (likely(written >= 0))
			iocb->ki_pos = pos + written;
		inode_unlock(inode);
	}

	if (written >= 0) {
		int dirty;
		spin_lock(&ci->i_ceph_lock);
		ci->i_inline_version = CEPH_INLINE_NONE;
		dirty = __ceph_mark_dirty_caps(ci, CEPH_CAP_FILE_WR,
					       &prealloc_cf);
		spin_unlock(&ci->i_ceph_lock);
		if (dirty)
			__mark_inode_dirty(inode, dirty);
	}

	dout("aio_write %p %llx.%llx %llu~%u  dropping cap refs on %s\n",
	     inode, ceph_vinop(inode), pos, (unsigned)count,
	     ceph_cap_string(got));
	ceph_put_cap_refs(ci, got);

	if (written == -EOLDSNAPC) {
		dout("aio_write %p %llx.%llx %llu~%u" "got EOLDSNAPC, retrying\n",
		     inode, ceph_vinop(inode), pos, (unsigned)count);
		goto retry_snap;
	}

	if (written >= 0) {
		if (ceph_osdmap_flag(osdc, CEPH_OSDMAP_NEARFULL))
			iocb->ki_flags |= IOCB_DSYNC;
		written = generic_write_sync(iocb, written);
	}

	goto out_unlocked;

out:
	inode_unlock(inode);
out_unlocked:
	ceph_free_cap_flush(prealloc_cf);
	current->backing_dev_info = NULL;
	return written ? written : err;
}

/*
 * llseek.  be sure to verify file size on SEEK_END.
 */
static loff_t ceph_llseek(struct file *file, loff_t offset, int whence)
{
	struct inode *inode = file->f_mapping->host;
	loff_t i_size;
	loff_t ret;

	inode_lock(inode);

	if (whence == SEEK_END || whence == SEEK_DATA || whence == SEEK_HOLE) {
		ret = ceph_do_getattr(inode, CEPH_STAT_CAP_SIZE, false);
		if (ret < 0)
			goto out;
	}

	i_size = i_size_read(inode);
	switch (whence) {
	case SEEK_END:
		offset += i_size;
		break;
	case SEEK_CUR:
		/*
		 * Here we special-case the lseek(fd, 0, SEEK_CUR)
		 * position-querying operation.  Avoid rewriting the "same"
		 * f_pos value back to the file because a concurrent read(),
		 * write() or lseek() might have altered it
		 */
		if (offset == 0) {
			ret = file->f_pos;
			goto out;
		}
		offset += file->f_pos;
		break;
	case SEEK_DATA:
		if (offset < 0 || offset >= i_size) {
			ret = -ENXIO;
			goto out;
		}
		break;
	case SEEK_HOLE:
		if (offset < 0 || offset >= i_size) {
			ret = -ENXIO;
			goto out;
		}
		offset = i_size;
		break;
	}

	ret = vfs_setpos(file, offset, inode->i_sb->s_maxbytes);

out:
	inode_unlock(inode);
	return ret;
}

static inline void ceph_zero_partial_page(
	struct inode *inode, loff_t offset, unsigned size)
{
	struct page *page;
	pgoff_t index = offset >> PAGE_SHIFT;

	page = find_lock_page(inode->i_mapping, index);
	if (page) {
		wait_on_page_writeback(page);
		zero_user(page, offset & (PAGE_SIZE - 1), size);
		unlock_page(page);
		put_page(page);
	}
}

static void ceph_zero_pagecache_range(struct inode *inode, loff_t offset,
				      loff_t length)
{
	loff_t nearly = round_up(offset, PAGE_SIZE);
	if (offset < nearly) {
		loff_t size = nearly - offset;
		if (length < size)
			size = length;
		ceph_zero_partial_page(inode, offset, size);
		offset += size;
		length -= size;
	}
	if (length >= PAGE_SIZE) {
		loff_t size = round_down(length, PAGE_SIZE);
		truncate_pagecache_range(inode, offset, offset + size - 1);
		offset += size;
		length -= size;
	}
	if (length)
		ceph_zero_partial_page(inode, offset, length);
}

static int ceph_zero_partial_object(struct inode *inode,
				    loff_t offset, loff_t *length)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_fs_client *fsc = ceph_inode_to_client(inode);
	struct ceph_osd_request *req;
	int ret = 0;
	loff_t zero = 0;
	int op;

	if (!length) {
		op = offset ? CEPH_OSD_OP_DELETE : CEPH_OSD_OP_TRUNCATE;
		length = &zero;
	} else {
		op = CEPH_OSD_OP_ZERO;
	}

	req = ceph_osdc_new_request(&fsc->client->osdc, &ci->i_layout,
					ceph_vino(inode),
					offset, length,
					0, 1, op,
					CEPH_OSD_FLAG_WRITE,
					NULL, 0, 0, false);
	if (IS_ERR(req)) {
		ret = PTR_ERR(req);
		goto out;
	}

	req->r_mtime = inode->i_mtime;
	ret = ceph_osdc_start_request(&fsc->client->osdc, req, false);
	if (!ret) {
		ret = ceph_osdc_wait_request(&fsc->client->osdc, req);
		if (ret == -ENOENT)
			ret = 0;
	}
	ceph_osdc_put_request(req);

out:
	return ret;
}

static int ceph_zero_objects(struct inode *inode, loff_t offset, loff_t length)
{
	int ret = 0;
	struct ceph_inode_info *ci = ceph_inode(inode);
	s32 stripe_unit = ci->i_layout.stripe_unit;
	s32 stripe_count = ci->i_layout.stripe_count;
	s32 object_size = ci->i_layout.object_size;
	u64 object_set_size = object_size * stripe_count;
	u64 nearly, t;

	/* round offset up to next period boundary */
	nearly = offset + object_set_size - 1;
	t = nearly;
	nearly -= do_div(t, object_set_size);

	while (length && offset < nearly) {
		loff_t size = length;
		ret = ceph_zero_partial_object(inode, offset, &size);
		if (ret < 0)
			return ret;
		offset += size;
		length -= size;
	}
	while (length >= object_set_size) {
		int i;
		loff_t pos = offset;
		for (i = 0; i < stripe_count; ++i) {
			ret = ceph_zero_partial_object(inode, pos, NULL);
			if (ret < 0)
				return ret;
			pos += stripe_unit;
		}
		offset += object_set_size;
		length -= object_set_size;
	}
	while (length) {
		loff_t size = length;
		ret = ceph_zero_partial_object(inode, offset, &size);
		if (ret < 0)
			return ret;
		offset += size;
		length -= size;
	}
	return ret;
}

static long ceph_fallocate(struct file *file, int mode,
				loff_t offset, loff_t length)
{
	struct ceph_file_info *fi = file->private_data;
	struct inode *inode = file_inode(file);
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_osd_client *osdc =
		&ceph_inode_to_client(inode)->client->osdc;
	struct ceph_cap_flush *prealloc_cf;
	int want, got = 0;
	int dirty;
	int ret = 0;
	loff_t endoff = 0;
	loff_t size;

	if (mode & ~(FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE))
		return -EOPNOTSUPP;

	if (!S_ISREG(inode->i_mode))
		return -EOPNOTSUPP;

	prealloc_cf = ceph_alloc_cap_flush();
	if (!prealloc_cf)
		return -ENOMEM;

	inode_lock(inode);

	if (ceph_snap(inode) != CEPH_NOSNAP) {
		ret = -EROFS;
		goto unlock;
	}

	if (ceph_osdmap_flag(osdc, CEPH_OSDMAP_FULL) &&
	    !(mode & FALLOC_FL_PUNCH_HOLE)) {
		ret = -ENOSPC;
		goto unlock;
	}

	if (ci->i_inline_version != CEPH_INLINE_NONE) {
		ret = ceph_uninline_data(file, NULL);
		if (ret < 0)
			goto unlock;
	}

	size = i_size_read(inode);
	if (!(mode & FALLOC_FL_KEEP_SIZE)) {
		endoff = offset + length;
		ret = inode_newsize_ok(inode, endoff);
		if (ret)
			goto unlock;
	}

	if (fi->fmode & CEPH_FILE_MODE_LAZY)
		want = CEPH_CAP_FILE_BUFFER | CEPH_CAP_FILE_LAZYIO;
	else
		want = CEPH_CAP_FILE_BUFFER;

	ret = ceph_get_caps(ci, CEPH_CAP_FILE_WR, want, endoff, &got, NULL);
	if (ret < 0)
		goto unlock;

	if (mode & FALLOC_FL_PUNCH_HOLE) {
		if (offset < size)
			ceph_zero_pagecache_range(inode, offset, length);
		ret = ceph_zero_objects(inode, offset, length);
	} else if (endoff > size) {
		truncate_pagecache_range(inode, size, -1);
		if (ceph_inode_set_size(inode, endoff))
			ceph_check_caps(ceph_inode(inode),
				CHECK_CAPS_AUTHONLY, NULL);
	}

	if (!ret) {
		spin_lock(&ci->i_ceph_lock);
		ci->i_inline_version = CEPH_INLINE_NONE;
		dirty = __ceph_mark_dirty_caps(ci, CEPH_CAP_FILE_WR,
					       &prealloc_cf);
		spin_unlock(&ci->i_ceph_lock);
		if (dirty)
			__mark_inode_dirty(inode, dirty);
	}

	ceph_put_cap_refs(ci, got);
unlock:
	inode_unlock(inode);
	ceph_free_cap_flush(prealloc_cf);
	return ret;
}

const struct file_operations ceph_file_fops = {
	.open = ceph_open,
	.release = ceph_release,
	.llseek = ceph_llseek,
	.read_iter = ceph_read_iter,
	.write_iter = ceph_write_iter,
	.mmap = ceph_mmap,
	.fsync = ceph_fsync,
	.lock = ceph_lock,
	.flock = ceph_flock,
	.splice_read = generic_file_splice_read,
	.splice_write = iter_file_splice_write,
	.unlocked_ioctl = ceph_ioctl,
	.compat_ioctl	= ceph_ioctl,
	.fallocate	= ceph_fallocate,
};

