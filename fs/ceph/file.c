#include <linux/ceph/ceph_debug.h>

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/writeback.h>
#include <linux/aio.h>
#include <linux/falloc.h>

#include "super.h"
#include "mds_client.h"
#include "cache.h"

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
	req->r_args.open.flags = cpu_to_le32(flags);
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
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_fs_client *fsc = ceph_sb_to_client(inode->i_sb);
	struct ceph_mds_client *mdsc = fsc->mdsc;

	switch (inode->i_mode & S_IFMT) {
	case S_IFREG:
		/* First file open request creates the cookie, we want to keep
		 * this cookie around for the filetime of the inode as not to
		 * have to worry about fscache register / revoke / operation
		 * races.
		 *
		 * Also, if we know the operation is going to invalidate data
		 * (non readonly) just nuke the cache right away.
		 */
		ceph_fscache_register_inode_cookie(mdsc->fsc, ci);
		if ((fmode & CEPH_FILE_MODE_WR))
			ceph_fscache_invalidate(inode);
	case S_IFDIR:
		dout("init_file %p %p 0%o (regular)\n", inode, file,
		     inode->i_mode);
		cf = kmem_cache_alloc(ceph_file_cachep, GFP_NOFS | __GFP_ZERO);
		if (cf == NULL) {
			ceph_put_fmode(ceph_inode(inode), fmode); /* clean up */
			return -ENOMEM;
		}
		cf->fmode = fmode;
		cf->next_offset = 2;
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
	struct inode *parent_inode = NULL;
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
		int mds_wanted = __ceph_caps_mds_wanted(ci);
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
	if (flags & O_CREAT)
		parent_inode = ceph_get_dentry_parent_inode(file->f_path.dentry);
	err = ceph_mdsc_do_request(mdsc, parent_inode, req);
	iput(parent_inode);
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
	int err;

	dout("atomic_open %p dentry %p '%pd' %s flags %d mode 0%o\n",
	     dir, dentry, dentry,
	     d_unhashed(dentry) ? "unhashed" : "hashed", flags, mode);

	if (dentry->d_name.len > NAME_MAX)
		return -ENAMETOOLONG;

	err = ceph_init_dentry(dentry);
	if (err < 0)
		return err;

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
	req->r_locked_dir = dir;           /* caller holds dir->i_mutex */
	err = ceph_mdsc_do_request(mdsc,
				   (flags & (O_CREAT|O_TRUNC)) ? dir : NULL,
				   req);
	if (err)
		goto out_req;

	err = ceph_handle_snapdir(req, dentry, err);
	if (err == 0 && (flags & O_CREAT) && !req->r_reply_info.head->is_dentry)
		err = ceph_handle_notrace_create(dir, dentry);

	if (d_unhashed(dentry)) {
		dn = ceph_finish_lookup(req, dentry, err);
		if (IS_ERR(dn))
			err = PTR_ERR(dn);
	} else {
		/* we were given a hashed negative dentry */
		dn = NULL;
	}
	if (err)
		goto out_req;
	if (dn || dentry->d_inode == NULL || S_ISLNK(dentry->d_inode->i_mode)) {
		/* make vfs retry on splice, ENOENT, or symlink */
		dout("atomic_open finish_no_open on dn %p\n", dn);
		err = finish_no_open(file, dn);
	} else {
		dout("atomic_open finish_open on dn %p\n", dn);
		if (req->r_op == CEPH_MDS_OP_CREATE && req->r_reply_info.has_create_ino) {
			ceph_init_inode_acls(dentry->d_inode, &acls);
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
	dput(cf->dentry);
	kmem_cache_free(ceph_file_cachep, cf);

	/* wake up anyone waiting for caps on this inode */
	wake_up_all(&ci->i_cap_wq);
	return 0;
}

enum {
	CHECK_EOF = 1,
	READ_INLINE = 2,
};

/*
 * Read a range of bytes striped over one or more objects.  Iterate over
 * objects we stripe over.  (That's not atomic, but good enough for now.)
 *
 * If we get a short result from the OSD, check against i_size; we need to
 * only return a short read to the caller if we hit EOF.
 */
static int striped_read(struct inode *inode,
			u64 off, u64 len,
			struct page **pages, int num_pages,
			int *checkeof, bool o_direct,
			unsigned long buf_align)
{
	struct ceph_fs_client *fsc = ceph_inode_to_client(inode);
	struct ceph_inode_info *ci = ceph_inode(inode);
	u64 pos, this_len, left;
	int io_align, page_align;
	int pages_left;
	int read;
	struct page **page_pos;
	int ret;
	bool hit_stripe, was_short;

	/*
	 * we may need to do multiple reads.  not atomic, unfortunately.
	 */
	pos = off;
	left = len;
	page_pos = pages;
	pages_left = num_pages;
	read = 0;
	io_align = off & ~PAGE_MASK;

more:
	if (o_direct)
		page_align = (pos - io_align + buf_align) & ~PAGE_MASK;
	else
		page_align = pos & ~PAGE_MASK;
	this_len = left;
	ret = ceph_osdc_readpages(&fsc->client->osdc, ceph_vino(inode),
				  &ci->i_layout, pos, &this_len,
				  ci->i_truncate_seq,
				  ci->i_truncate_size,
				  page_pos, pages_left, page_align);
	if (ret == -ENOENT)
		ret = 0;
	hit_stripe = this_len < left;
	was_short = ret >= 0 && ret < this_len;
	dout("striped_read %llu~%llu (read %u) got %d%s%s\n", pos, left, read,
	     ret, hit_stripe ? " HITSTRIPE" : "", was_short ? " SHORT" : "");

	if (ret >= 0) {
		int didpages;
		if (was_short && (pos + ret < inode->i_size)) {
			u64 tmp = min(this_len - ret,
					inode->i_size - pos - ret);
			dout(" zero gap %llu to %llu\n",
				pos + ret, pos + ret + tmp);
			ceph_zero_page_vector_range(page_align + read + ret,
							tmp, pages);
			ret += tmp;
		}

		didpages = (page_align + ret) >> PAGE_CACHE_SHIFT;
		pos += ret;
		read = pos - off;
		left -= ret;
		page_pos += didpages;
		pages_left -= didpages;

		/* hit stripe and need continue*/
		if (left && hit_stripe && pos < inode->i_size)
			goto more;
	}

	if (read > 0) {
		ret = read;
		/* did we bounce off eof? */
		if (pos + left > inode->i_size)
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
static ssize_t ceph_sync_read(struct kiocb *iocb, struct iov_iter *i,
				int *checkeof)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file_inode(file);
	struct page **pages;
	u64 off = iocb->ki_pos;
	int num_pages, ret;
	size_t len = iov_iter_count(i);

	dout("sync_read on file %p %llu~%u %s\n", file, off,
	     (unsigned)len,
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

	if (file->f_flags & O_DIRECT) {
		while (iov_iter_count(i)) {
			size_t start;
			ssize_t n;

			n = iov_iter_get_pages_alloc(i, &pages, INT_MAX, &start);
			if (n < 0)
				return n;

			num_pages = (n + start + PAGE_SIZE - 1) / PAGE_SIZE;

			ret = striped_read(inode, off, n,
					   pages, num_pages, checkeof,
					   1, start);

			ceph_put_page_vector(pages, num_pages, true);

			if (ret <= 0)
				break;
			off += ret;
			iov_iter_advance(i, ret);
			if (ret < n)
				break;
		}
	} else {
		num_pages = calc_pages_for(off, len);
		pages = ceph_alloc_page_vector(num_pages, GFP_NOFS);
		if (IS_ERR(pages))
			return PTR_ERR(pages);
		ret = striped_read(inode, off, len, pages,
					num_pages, checkeof, 0, 0);
		if (ret > 0) {
			int l, k = 0;
			size_t left = ret;

			while (left) {
				size_t page_off = off & ~PAGE_MASK;
				size_t copy = min_t(size_t,
						    PAGE_SIZE - page_off, left);
				l = copy_page_to_iter(pages[k++], page_off,
						      copy, i);
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

	dout("sync_read result %d\n", ret);
	return ret;
}

/*
 * Write commit request unsafe callback, called to tell us when a
 * request is unsafe (that is, in flight--has been handed to the
 * messenger to send to its target osd).  It is called again when
 * we've received a response message indicating the request is
 * "safe" (its CEPH_OSD_FLAG_ONDISK flag is set), or when a request
 * is completed early (and unsuccessfully) due to a timeout or
 * interrupt.
 *
 * This is used if we requested both an ACK and ONDISK commit reply
 * from the OSD.
 */
static void ceph_sync_write_unsafe(struct ceph_osd_request *req, bool unsafe)
{
	struct ceph_inode_info *ci = ceph_inode(req->r_inode);

	dout("%s %p tid %llu %ssafe\n", __func__, req, req->r_tid,
		unsafe ? "un" : "");
	if (unsafe) {
		ceph_get_cap_refs(ci, CEPH_CAP_FILE_WR);
		spin_lock(&ci->i_unsafe_lock);
		list_add_tail(&req->r_unsafe_item,
			      &ci->i_unsafe_writes);
		spin_unlock(&ci->i_unsafe_lock);
	} else {
		spin_lock(&ci->i_unsafe_lock);
		list_del_init(&req->r_unsafe_item);
		spin_unlock(&ci->i_unsafe_lock);
		ceph_put_cap_refs(ci, CEPH_CAP_FILE_WR);
	}
}


/*
 * Synchronous write, straight from __user pointer or user pages.
 *
 * If write spans object boundary, just do multiple writes.  (For a
 * correct atomic write, we should e.g. take write locks on all
 * objects, rollback on failure, etc.)
 */
static ssize_t
ceph_sync_direct_write(struct kiocb *iocb, struct iov_iter *from, loff_t pos)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file_inode(file);
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_fs_client *fsc = ceph_inode_to_client(inode);
	struct ceph_snap_context *snapc;
	struct ceph_vino vino;
	struct ceph_osd_request *req;
	struct page **pages;
	int num_pages;
	int written = 0;
	int flags;
	int check_caps = 0;
	int ret;
	struct timespec mtime = CURRENT_TIME;
	size_t count = iov_iter_count(from);

	if (ceph_snap(file_inode(file)) != CEPH_NOSNAP)
		return -EROFS;

	dout("sync_direct_write on file %p %lld~%u\n", file, pos,
	     (unsigned)count);

	ret = filemap_write_and_wait_range(inode->i_mapping, pos, pos + count);
	if (ret < 0)
		return ret;

	ret = invalidate_inode_pages2_range(inode->i_mapping,
					    pos >> PAGE_CACHE_SHIFT,
					    (pos + count) >> PAGE_CACHE_SHIFT);
	if (ret < 0)
		dout("invalidate_inode_pages2_range returned %d\n", ret);

	flags = CEPH_OSD_FLAG_ORDERSNAP |
		CEPH_OSD_FLAG_ONDISK |
		CEPH_OSD_FLAG_WRITE;

	while (iov_iter_count(from) > 0) {
		u64 len = iov_iter_single_seg_count(from);
		size_t start;
		ssize_t n;

		snapc = ci->i_snap_realm->cached_context;
		vino = ceph_vino(inode);
		req = ceph_osdc_new_request(&fsc->client->osdc, &ci->i_layout,
					    vino, pos, &len, 0,
					    2,/*include a 'startsync' command*/
					    CEPH_OSD_OP_WRITE, flags, snapc,
					    ci->i_truncate_seq,
					    ci->i_truncate_size,
					    false);
		if (IS_ERR(req)) {
			ret = PTR_ERR(req);
			break;
		}

		osd_req_op_init(req, 1, CEPH_OSD_OP_STARTSYNC);

		n = iov_iter_get_pages_alloc(from, &pages, len, &start);
		if (unlikely(n < 0)) {
			ret = n;
			ceph_osdc_put_request(req);
			break;
		}

		num_pages = (n + start + PAGE_SIZE - 1) / PAGE_SIZE;
		/*
		 * throw out any page cache pages in this range. this
		 * may block.
		 */
		truncate_inode_pages_range(inode->i_mapping, pos,
				   (pos+n) | (PAGE_CACHE_SIZE-1));
		osd_req_op_extent_osd_data_pages(req, 0, pages, n, start,
						false, false);

		/* BUG_ON(vino.snap != CEPH_NOSNAP); */
		ceph_osdc_build_request(req, pos, snapc, vino.snap, &mtime);

		ret = ceph_osdc_start_request(&fsc->client->osdc, req, false);
		if (!ret)
			ret = ceph_osdc_wait_request(&fsc->client->osdc, req);

		ceph_put_page_vector(pages, num_pages, false);

		ceph_osdc_put_request(req);
		if (ret)
			break;
		pos += n;
		written += n;
		iov_iter_advance(from, n);

		if (pos > i_size_read(inode)) {
			check_caps = ceph_inode_set_size(inode, pos);
			if (check_caps)
				ceph_check_caps(ceph_inode(inode),
						CHECK_CAPS_AUTHONLY,
						NULL);
		}
	}

	if (ret != -EOLDSNAPC && written > 0) {
		iocb->ki_pos = pos;
		ret = written;
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
ceph_sync_write(struct kiocb *iocb, struct iov_iter *from, loff_t pos)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file_inode(file);
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_fs_client *fsc = ceph_inode_to_client(inode);
	struct ceph_snap_context *snapc;
	struct ceph_vino vino;
	struct ceph_osd_request *req;
	struct page **pages;
	u64 len;
	int num_pages;
	int written = 0;
	int flags;
	int check_caps = 0;
	int ret;
	struct timespec mtime = CURRENT_TIME;
	size_t count = iov_iter_count(from);

	if (ceph_snap(file_inode(file)) != CEPH_NOSNAP)
		return -EROFS;

	dout("sync_write on file %p %lld~%u\n", file, pos, (unsigned)count);

	ret = filemap_write_and_wait_range(inode->i_mapping, pos, pos + count);
	if (ret < 0)
		return ret;

	ret = invalidate_inode_pages2_range(inode->i_mapping,
					    pos >> PAGE_CACHE_SHIFT,
					    (pos + count) >> PAGE_CACHE_SHIFT);
	if (ret < 0)
		dout("invalidate_inode_pages2_range returned %d\n", ret);

	flags = CEPH_OSD_FLAG_ORDERSNAP |
		CEPH_OSD_FLAG_ONDISK |
		CEPH_OSD_FLAG_WRITE |
		CEPH_OSD_FLAG_ACK;

	while ((len = iov_iter_count(from)) > 0) {
		size_t left;
		int n;

		snapc = ci->i_snap_realm->cached_context;
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
		num_pages = (len + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;

		pages = ceph_alloc_page_vector(num_pages, GFP_NOFS);
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

		/* get a second commit callback */
		req->r_unsafe_callback = ceph_sync_write_unsafe;
		req->r_inode = inode;

		osd_req_op_extent_osd_data_pages(req, 0, pages, len, 0,
						false, true);

		/* BUG_ON(vino.snap != CEPH_NOSNAP); */
		ceph_osdc_build_request(req, pos, snapc, vino.snap, &mtime);

		ret = ceph_osdc_start_request(&fsc->client->osdc, req, false);
		if (!ret)
			ret = ceph_osdc_wait_request(&fsc->client->osdc, req);

out:
		ceph_osdc_put_request(req);
		if (ret == 0) {
			pos += len;
			written += len;

			if (pos > i_size_read(inode)) {
				check_caps = ceph_inode_set_size(inode, pos);
				if (check_caps)
					ceph_check_caps(ceph_inode(inode),
							CHECK_CAPS_AUTHONLY,
							NULL);
			}
		} else
			break;
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
	    (iocb->ki_filp->f_flags & O_DIRECT) ||
	    (fi->flags & CEPH_F_SYNC)) {

		dout("aio_sync_read %p %llx.%llx %llu~%u got cap refs on %s\n",
		     inode, ceph_vinop(inode), iocb->ki_pos, (unsigned)len,
		     ceph_cap_string(got));

		if (ci->i_inline_version == CEPH_INLINE_NONE) {
			/* hmm, this isn't really async... */
			ret = ceph_sync_read(iocb, to, &retry_op);
		} else {
			retry_op = READ_INLINE;
		}
	} else {
		dout("aio_read %p %llx.%llx %llu~%u got cap refs on %s\n",
		     inode, ceph_vinop(inode), iocb->ki_pos, (unsigned)len,
		     ceph_cap_string(got));

		ret = generic_file_read_iter(iocb, to);
	}
	dout("aio_read %p %llx.%llx dropping cap refs on %s = %d\n",
	     inode, ceph_vinop(inode), ceph_cap_string(got), (int)ret);
	if (pinned_page) {
		page_cache_release(pinned_page);
		pinned_page = NULL;
	}
	ceph_put_cap_refs(ci, got);
	if (retry_op && ret >= 0) {
		int statret;
		struct page *page = NULL;
		loff_t i_size;
		if (retry_op == READ_INLINE) {
			page = __page_cache_alloc(GFP_NOFS);
			if (!page)
				return -ENOMEM;
		}

		statret = __ceph_do_getattr(inode, page,
					    CEPH_STAT_CAP_INLINE_DATA, !!page);
		if (statret < 0) {
			 __free_page(page);
			if (statret == -ENODATA) {
				BUG_ON(retry_op != READ_INLINE);
				goto again;
			}
			return statret;
		}

		i_size = i_size_read(inode);
		if (retry_op == READ_INLINE) {
			/* does not support inline data > PAGE_SIZE */
			if (i_size > PAGE_CACHE_SIZE) {
				ret = -EIO;
			} else if (iocb->ki_pos < i_size) {
				loff_t end = min_t(loff_t, i_size,
						   iocb->ki_pos + len);
				if (statret < end)
					zero_user_segment(page, statret, end);
				ret = copy_page_to_iter(page,
						iocb->ki_pos & ~PAGE_MASK,
						end - iocb->ki_pos, to);
				iocb->ki_pos += ret;
			} else {
				ret = 0;
			}
			__free_pages(page, 0);
			return ret;
		}

		/* hit EOF or hole? */
		if (retry_op == CHECK_EOF && iocb->ki_pos < i_size &&
			ret < len) {
			dout("sync_read hit hole, ppos %lld < size %lld"
			     ", reading more\n", iocb->ki_pos,
			     inode->i_size);

			read += ret;
			len -= ret;
			retry_op = 0;
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
	ssize_t count = iov_iter_count(from), written = 0;
	int err, want, got;
	loff_t pos = iocb->ki_pos;

	if (ceph_snap(inode) != CEPH_NOSNAP)
		return -EROFS;

	mutex_lock(&inode->i_mutex);

	/* We can write back this queue in page reclaim */
	current->backing_dev_info = inode_to_bdi(inode);

	err = generic_write_checks(file, &pos, &count, S_ISBLK(inode->i_mode));
	if (err)
		goto out;

	if (count == 0)
		goto out;
	iov_iter_truncate(from, count);

	err = file_remove_suid(file);
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

retry_snap:
	if (ceph_osdmap_flag(osdc->osdmap, CEPH_OSDMAP_FULL)) {
		err = -ENOSPC;
		goto out;
	}

	dout("aio_write %p %llx.%llx %llu~%zd getting caps. i_size %llu\n",
	     inode, ceph_vinop(inode), pos, count, inode->i_size);
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
	    (file->f_flags & O_DIRECT) || (fi->flags & CEPH_F_SYNC)) {
		struct iov_iter data;
		mutex_unlock(&inode->i_mutex);
		/* we might need to revert back to that point */
		data = *from;
		if (file->f_flags & O_DIRECT)
			written = ceph_sync_direct_write(iocb, &data, pos);
		else
			written = ceph_sync_write(iocb, &data, pos);
		if (written == -EOLDSNAPC) {
			dout("aio_write %p %llx.%llx %llu~%u"
				"got EOLDSNAPC, retrying\n",
				inode, ceph_vinop(inode),
				pos, (unsigned)count);
			mutex_lock(&inode->i_mutex);
			goto retry_snap;
		}
		if (written > 0)
			iov_iter_advance(from, written);
	} else {
		loff_t old_size = inode->i_size;
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
		if (inode->i_size > old_size)
			ceph_fscache_update_objectsize(inode);
		mutex_unlock(&inode->i_mutex);
	}

	if (written >= 0) {
		int dirty;
		spin_lock(&ci->i_ceph_lock);
		ci->i_inline_version = CEPH_INLINE_NONE;
		dirty = __ceph_mark_dirty_caps(ci, CEPH_CAP_FILE_WR);
		spin_unlock(&ci->i_ceph_lock);
		if (dirty)
			__mark_inode_dirty(inode, dirty);
	}

	dout("aio_write %p %llx.%llx %llu~%u  dropping cap refs on %s\n",
	     inode, ceph_vinop(inode), pos, (unsigned)count,
	     ceph_cap_string(got));
	ceph_put_cap_refs(ci, got);

	if (written >= 0 &&
	    ((file->f_flags & O_SYNC) || IS_SYNC(file->f_mapping->host) ||
	     ceph_osdmap_flag(osdc->osdmap, CEPH_OSDMAP_NEARFULL))) {
		err = vfs_fsync_range(file, pos, pos + written - 1, 1);
		if (err < 0)
			written = err;
	}

	goto out_unlocked;

out:
	mutex_unlock(&inode->i_mutex);
out_unlocked:
	current->backing_dev_info = NULL;
	return written ? written : err;
}

/*
 * llseek.  be sure to verify file size on SEEK_END.
 */
static loff_t ceph_llseek(struct file *file, loff_t offset, int whence)
{
	struct inode *inode = file->f_mapping->host;
	int ret;

	mutex_lock(&inode->i_mutex);

	if (whence == SEEK_END || whence == SEEK_DATA || whence == SEEK_HOLE) {
		ret = ceph_do_getattr(inode, CEPH_STAT_CAP_SIZE, false);
		if (ret < 0) {
			offset = ret;
			goto out;
		}
	}

	switch (whence) {
	case SEEK_END:
		offset += inode->i_size;
		break;
	case SEEK_CUR:
		/*
		 * Here we special-case the lseek(fd, 0, SEEK_CUR)
		 * position-querying operation.  Avoid rewriting the "same"
		 * f_pos value back to the file because a concurrent read(),
		 * write() or lseek() might have altered it
		 */
		if (offset == 0) {
			offset = file->f_pos;
			goto out;
		}
		offset += file->f_pos;
		break;
	case SEEK_DATA:
		if (offset >= inode->i_size) {
			ret = -ENXIO;
			goto out;
		}
		break;
	case SEEK_HOLE:
		if (offset >= inode->i_size) {
			ret = -ENXIO;
			goto out;
		}
		offset = inode->i_size;
		break;
	}

	offset = vfs_setpos(file, offset, inode->i_sb->s_maxbytes);

out:
	mutex_unlock(&inode->i_mutex);
	return offset;
}

static inline void ceph_zero_partial_page(
	struct inode *inode, loff_t offset, unsigned size)
{
	struct page *page;
	pgoff_t index = offset >> PAGE_CACHE_SHIFT;

	page = find_lock_page(inode->i_mapping, index);
	if (page) {
		wait_on_page_writeback(page);
		zero_user(page, offset & (PAGE_CACHE_SIZE - 1), size);
		unlock_page(page);
		page_cache_release(page);
	}
}

static void ceph_zero_pagecache_range(struct inode *inode, loff_t offset,
				      loff_t length)
{
	loff_t nearly = round_up(offset, PAGE_CACHE_SIZE);
	if (offset < nearly) {
		loff_t size = nearly - offset;
		if (length < size)
			size = length;
		ceph_zero_partial_page(inode, offset, size);
		offset += size;
		length -= size;
	}
	if (length >= PAGE_CACHE_SIZE) {
		loff_t size = round_down(length, PAGE_CACHE_SIZE);
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
					CEPH_OSD_FLAG_WRITE |
					CEPH_OSD_FLAG_ONDISK,
					NULL, 0, 0, false);
	if (IS_ERR(req)) {
		ret = PTR_ERR(req);
		goto out;
	}

	ceph_osdc_build_request(req, offset, NULL, ceph_vino(inode).snap,
				&inode->i_mtime);

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
	s32 stripe_unit = ceph_file_layout_su(ci->i_layout);
	s32 stripe_count = ceph_file_layout_stripe_count(ci->i_layout);
	s32 object_size = ceph_file_layout_object_size(ci->i_layout);
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
	int want, got = 0;
	int dirty;
	int ret = 0;
	loff_t endoff = 0;
	loff_t size;

	if (mode & ~(FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE))
		return -EOPNOTSUPP;

	if (!S_ISREG(inode->i_mode))
		return -EOPNOTSUPP;

	mutex_lock(&inode->i_mutex);

	if (ceph_snap(inode) != CEPH_NOSNAP) {
		ret = -EROFS;
		goto unlock;
	}

	if (ceph_osdmap_flag(osdc->osdmap, CEPH_OSDMAP_FULL) &&
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
	if (!(mode & FALLOC_FL_KEEP_SIZE))
		endoff = offset + length;

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
		dirty = __ceph_mark_dirty_caps(ci, CEPH_CAP_FILE_WR);
		spin_unlock(&ci->i_ceph_lock);
		if (dirty)
			__mark_inode_dirty(inode, dirty);
	}

	ceph_put_cap_refs(ci, got);
unlock:
	mutex_unlock(&inode->i_mutex);
	return ret;
}

const struct file_operations ceph_file_fops = {
	.open = ceph_open,
	.release = ceph_release,
	.llseek = ceph_llseek,
	.read = new_sync_read,
	.write = new_sync_write,
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

