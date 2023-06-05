// SPDX-License-Identifier: GPL-2.0
#include <linux/ceph/ceph_debug.h>
#include <linux/ceph/striper.h>

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/writeback.h>
#include <linux/falloc.h>
#include <linux/iversion.h>
#include <linux/ktime.h>

#include "super.h"
#include "mds_client.h"
#include "cache.h"
#include "io.h"
#include "metric.h"

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

	flags &= ~O_ACCMODE;

#define ceph_sys2wire(a) if (flags & a) { wire_flags |= CEPH_##a; flags &= ~a; }

	ceph_sys2wire(O_CREAT);
	ceph_sys2wire(O_EXCL);
	ceph_sys2wire(O_TRUNC);
	ceph_sys2wire(O_DIRECTORY);
	ceph_sys2wire(O_NOFOLLOW);

#undef ceph_sys2wire

	if (flags)
		dout("unused open flags: %x\n", flags);

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
 * How many pages to get in one call to iov_iter_get_pages().  This
 * determines the size of the on-stack array used as a buffer.
 */
#define ITER_GET_BVECS_PAGES	64

static ssize_t __iter_get_bvecs(struct iov_iter *iter, size_t maxsize,
				struct bio_vec *bvecs)
{
	size_t size = 0;
	int bvec_idx = 0;

	if (maxsize > iov_iter_count(iter))
		maxsize = iov_iter_count(iter);

	while (size < maxsize) {
		struct page *pages[ITER_GET_BVECS_PAGES];
		ssize_t bytes;
		size_t start;
		int idx = 0;

		bytes = iov_iter_get_pages2(iter, pages, maxsize - size,
					   ITER_GET_BVECS_PAGES, &start);
		if (bytes < 0)
			return size ?: bytes;

		size += bytes;

		for ( ; bytes; idx++, bvec_idx++) {
			int len = min_t(int, bytes, PAGE_SIZE - start);

			bvec_set_page(&bvecs[bvec_idx], pages[idx], len, start);
			bytes -= len;
			start = 0;
		}
	}

	return size;
}

/*
 * iov_iter_get_pages() only considers one iov_iter segment, no matter
 * what maxsize or maxpages are given.  For ITER_BVEC that is a single
 * page.
 *
 * Attempt to get up to @maxsize bytes worth of pages from @iter.
 * Return the number of bytes in the created bio_vec array, or an error.
 */
static ssize_t iter_get_bvecs_alloc(struct iov_iter *iter, size_t maxsize,
				    struct bio_vec **bvecs, int *num_bvecs)
{
	struct bio_vec *bv;
	size_t orig_count = iov_iter_count(iter);
	ssize_t bytes;
	int npages;

	iov_iter_truncate(iter, maxsize);
	npages = iov_iter_npages(iter, INT_MAX);
	iov_iter_reexpand(iter, orig_count);

	/*
	 * __iter_get_bvecs() may populate only part of the array -- zero it
	 * out.
	 */
	bv = kvmalloc_array(npages, sizeof(*bv), GFP_KERNEL | __GFP_ZERO);
	if (!bv)
		return -ENOMEM;

	bytes = __iter_get_bvecs(iter, maxsize, bv);
	if (bytes < 0) {
		/*
		 * No pages were pinned -- just free the array.
		 */
		kvfree(bv);
		return bytes;
	}

	*bvecs = bv;
	*num_bvecs = npages;
	return bytes;
}

static void put_bvecs(struct bio_vec *bvecs, int num_bvecs, bool should_dirty)
{
	int i;

	for (i = 0; i < num_bvecs; i++) {
		if (bvecs[i].bv_page) {
			if (should_dirty)
				set_page_dirty_lock(bvecs[i].bv_page);
			put_page(bvecs[i].bv_page);
		}
	}
	kvfree(bvecs);
}

/*
 * Prepare an open request.  Preallocate ceph_cap to avoid an
 * inopportune ENOMEM later.
 */
static struct ceph_mds_request *
prepare_open_request(struct super_block *sb, int flags, int create_mode)
{
	struct ceph_mds_client *mdsc = ceph_sb_to_mdsc(sb);
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

static int ceph_init_file_info(struct inode *inode, struct file *file,
					int fmode, bool isdir)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_mount_options *opt =
		ceph_inode_to_client(&ci->netfs.inode)->mount_options;
	struct ceph_file_info *fi;
	int ret;

	dout("%s %p %p 0%o (%s)\n", __func__, inode, file,
			inode->i_mode, isdir ? "dir" : "regular");
	BUG_ON(inode->i_fop->release != ceph_release);

	if (isdir) {
		struct ceph_dir_file_info *dfi =
			kmem_cache_zalloc(ceph_dir_file_cachep, GFP_KERNEL);
		if (!dfi)
			return -ENOMEM;

		file->private_data = dfi;
		fi = &dfi->file_info;
		dfi->next_offset = 2;
		dfi->readdir_cache_idx = -1;
	} else {
		fi = kmem_cache_zalloc(ceph_file_cachep, GFP_KERNEL);
		if (!fi)
			return -ENOMEM;

		if (opt->flags & CEPH_MOUNT_OPT_NOPAGECACHE)
			fi->flags |= CEPH_F_SYNC;

		file->private_data = fi;
	}

	ceph_get_fmode(ci, fmode, 1);
	fi->fmode = fmode;

	spin_lock_init(&fi->rw_contexts_lock);
	INIT_LIST_HEAD(&fi->rw_contexts);
	fi->filp_gen = READ_ONCE(ceph_inode_to_client(inode)->filp_gen);

	if ((file->f_mode & FMODE_WRITE) && ceph_has_inline_data(ci)) {
		ret = ceph_uninline_data(file);
		if (ret < 0)
			goto error;
	}

	return 0;

error:
	ceph_fscache_unuse_cookie(inode, file->f_mode & FMODE_WRITE);
	ceph_put_fmode(ci, fi->fmode, 1);
	kmem_cache_free(ceph_file_cachep, fi);
	/* wake up anyone waiting for caps on this inode */
	wake_up_all(&ci->i_cap_wq);
	return ret;
}

/*
 * initialize private struct file data.
 * if we fail, clean up by dropping fmode reference on the ceph_inode
 */
static int ceph_init_file(struct inode *inode, struct file *file, int fmode)
{
	int ret = 0;

	switch (inode->i_mode & S_IFMT) {
	case S_IFREG:
		ceph_fscache_use_cookie(inode, file->f_mode & FMODE_WRITE);
		fallthrough;
	case S_IFDIR:
		ret = ceph_init_file_info(inode, file, fmode,
						S_ISDIR(inode->i_mode));
		break;

	case S_IFLNK:
		dout("init_file %p %p 0%o (symlink)\n", inode, file,
		     inode->i_mode);
		break;

	default:
		dout("init_file %p %p 0%o (special)\n", inode, file,
		     inode->i_mode);
		/*
		 * we need to drop the open ref now, since we don't
		 * have .release set to ceph_release.
		 */
		BUG_ON(inode->i_fop->release == ceph_release);

		/* call the proper open fop */
		ret = inode->i_fop->open(inode, file);
	}
	return ret;
}

/*
 * try renew caps after session gets killed.
 */
int ceph_renew_caps(struct inode *inode, int fmode)
{
	struct ceph_mds_client *mdsc = ceph_sb_to_mdsc(inode->i_sb);
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_mds_request *req;
	int err, flags, wanted;

	spin_lock(&ci->i_ceph_lock);
	__ceph_touch_fmode(ci, mdsc, fmode);
	wanted = __ceph_caps_file_wanted(ci);
	if (__ceph_is_any_real_caps(ci) &&
	    (!(wanted & CEPH_CAP_ANY_WR) || ci->i_auth_cap)) {
		int issued = __ceph_caps_issued(ci, NULL);
		spin_unlock(&ci->i_ceph_lock);
		dout("renew caps %p want %s issued %s updating mds_wanted\n",
		     inode, ceph_cap_string(wanted), ceph_cap_string(issued));
		ceph_check_caps(ci, 0);
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
	struct ceph_file_info *fi = file->private_data;
	int err;
	int flags, fmode, wanted;

	if (fi) {
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
		__ceph_touch_fmode(ci, mdsc, fmode);
		spin_unlock(&ci->i_ceph_lock);

		/* adjust wanted? */
		if ((issued & wanted) != wanted &&
		    (mds_wanted & wanted) != wanted &&
		    ceph_snap(inode) != CEPH_SNAPDIR)
			ceph_check_caps(ci, 0);

		return ceph_init_file(inode, file, fmode);
	} else if (ceph_snap(inode) != CEPH_NOSNAP &&
		   (ci->i_snap_caps & wanted) == wanted) {
		__ceph_touch_fmode(ci, mdsc, fmode);
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

/* Clone the layout from a synchronous create, if the dir now has Dc caps */
static void
cache_file_layout(struct inode *dst, struct inode *src)
{
	struct ceph_inode_info *cdst = ceph_inode(dst);
	struct ceph_inode_info *csrc = ceph_inode(src);

	spin_lock(&cdst->i_ceph_lock);
	if ((__ceph_caps_issued(cdst, NULL) & CEPH_CAP_DIR_CREATE) &&
	    !ceph_file_layout_is_valid(&cdst->i_cached_layout)) {
		memcpy(&cdst->i_cached_layout, &csrc->i_layout,
			sizeof(cdst->i_cached_layout));
		rcu_assign_pointer(cdst->i_cached_layout.pool_ns,
				   ceph_try_get_string(csrc->i_layout.pool_ns));
	}
	spin_unlock(&cdst->i_ceph_lock);
}

/*
 * Try to set up an async create. We need caps, a file layout, and inode number,
 * and either a lease on the dentry or complete dir info. If any of those
 * criteria are not satisfied, then return false and the caller can go
 * synchronous.
 */
static int try_prep_async_create(struct inode *dir, struct dentry *dentry,
				 struct ceph_file_layout *lo, u64 *pino)
{
	struct ceph_inode_info *ci = ceph_inode(dir);
	struct ceph_dentry_info *di = ceph_dentry(dentry);
	int got = 0, want = CEPH_CAP_FILE_EXCL | CEPH_CAP_DIR_CREATE;
	u64 ino;

	spin_lock(&ci->i_ceph_lock);
	/* No auth cap means no chance for Dc caps */
	if (!ci->i_auth_cap)
		goto no_async;

	/* Any delegated inos? */
	if (xa_empty(&ci->i_auth_cap->session->s_delegated_inos))
		goto no_async;

	if (!ceph_file_layout_is_valid(&ci->i_cached_layout))
		goto no_async;

	if ((__ceph_caps_issued(ci, NULL) & want) != want)
		goto no_async;

	if (d_in_lookup(dentry)) {
		if (!__ceph_dir_is_complete(ci))
			goto no_async;
		spin_lock(&dentry->d_lock);
		di->lease_shared_gen = atomic_read(&ci->i_shared_gen);
		spin_unlock(&dentry->d_lock);
	} else if (atomic_read(&ci->i_shared_gen) !=
		   READ_ONCE(di->lease_shared_gen)) {
		goto no_async;
	}

	ino = ceph_get_deleg_ino(ci->i_auth_cap->session);
	if (!ino)
		goto no_async;

	*pino = ino;
	ceph_take_cap_refs(ci, want, false);
	memcpy(lo, &ci->i_cached_layout, sizeof(*lo));
	rcu_assign_pointer(lo->pool_ns,
			   ceph_try_get_string(ci->i_cached_layout.pool_ns));
	got = want;
no_async:
	spin_unlock(&ci->i_ceph_lock);
	return got;
}

static void restore_deleg_ino(struct inode *dir, u64 ino)
{
	struct ceph_inode_info *ci = ceph_inode(dir);
	struct ceph_mds_session *s = NULL;

	spin_lock(&ci->i_ceph_lock);
	if (ci->i_auth_cap)
		s = ceph_get_mds_session(ci->i_auth_cap->session);
	spin_unlock(&ci->i_ceph_lock);
	if (s) {
		int err = ceph_restore_deleg_ino(s, ino);
		if (err)
			pr_warn("ceph: unable to restore delegated ino 0x%llx to session: %d\n",
				ino, err);
		ceph_put_mds_session(s);
	}
}

static void wake_async_create_waiters(struct inode *inode,
				      struct ceph_mds_session *session)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	bool check_cap = false;

	spin_lock(&ci->i_ceph_lock);
	if (ci->i_ceph_flags & CEPH_I_ASYNC_CREATE) {
		ci->i_ceph_flags &= ~CEPH_I_ASYNC_CREATE;
		wake_up_bit(&ci->i_ceph_flags, CEPH_ASYNC_CREATE_BIT);

		if (ci->i_ceph_flags & CEPH_I_ASYNC_CHECK_CAPS) {
			ci->i_ceph_flags &= ~CEPH_I_ASYNC_CHECK_CAPS;
			check_cap = true;
		}
	}
	ceph_kick_flushing_inode_caps(session, ci);
	spin_unlock(&ci->i_ceph_lock);

	if (check_cap)
		ceph_check_caps(ci, CHECK_CAPS_FLUSH);
}

static void ceph_async_create_cb(struct ceph_mds_client *mdsc,
                                 struct ceph_mds_request *req)
{
	struct dentry *dentry = req->r_dentry;
	struct inode *dinode = d_inode(dentry);
	struct inode *tinode = req->r_target_inode;
	int result = req->r_err ? req->r_err :
			le32_to_cpu(req->r_reply_info.head->result);

	WARN_ON_ONCE(dinode && tinode && dinode != tinode);

	/* MDS changed -- caller must resubmit */
	if (result == -EJUKEBOX)
		goto out;

	mapping_set_error(req->r_parent->i_mapping, result);

	if (result) {
		int pathlen = 0;
		u64 base = 0;
		char *path = ceph_mdsc_build_path(req->r_dentry, &pathlen,
						  &base, 0);

		pr_warn("async create failure path=(%llx)%s result=%d!\n",
			base, IS_ERR(path) ? "<<bad>>" : path, result);
		ceph_mdsc_free_path(path, pathlen);

		ceph_dir_clear_complete(req->r_parent);
		if (!d_unhashed(dentry))
			d_drop(dentry);

		if (dinode) {
			mapping_set_error(dinode->i_mapping, result);
			ceph_inode_shutdown(dinode);
			wake_async_create_waiters(dinode, req->r_session);
		}
	}

	if (tinode) {
		u64 ino = ceph_vino(tinode).ino;

		if (req->r_deleg_ino != ino)
			pr_warn("%s: inode number mismatch! err=%d deleg_ino=0x%llx target=0x%llx\n",
				__func__, req->r_err, req->r_deleg_ino, ino);

		mapping_set_error(tinode->i_mapping, result);
		wake_async_create_waiters(tinode, req->r_session);
	} else if (!result) {
		pr_warn("%s: no req->r_target_inode for 0x%llx\n", __func__,
			req->r_deleg_ino);
	}
out:
	ceph_mdsc_release_dir_caps(req);
}

static int ceph_finish_async_create(struct inode *dir, struct dentry *dentry,
				    struct file *file, umode_t mode,
				    struct ceph_mds_request *req,
				    struct ceph_acl_sec_ctx *as_ctx,
				    struct ceph_file_layout *lo)
{
	int ret;
	char xattr_buf[4];
	struct ceph_mds_reply_inode in = { };
	struct ceph_mds_reply_info_in iinfo = { .in = &in };
	struct ceph_inode_info *ci = ceph_inode(dir);
	struct ceph_dentry_info *di = ceph_dentry(dentry);
	struct inode *inode;
	struct timespec64 now;
	struct ceph_string *pool_ns;
	struct ceph_mds_client *mdsc = ceph_sb_to_mdsc(dir->i_sb);
	struct ceph_vino vino = { .ino = req->r_deleg_ino,
				  .snap = CEPH_NOSNAP };

	ktime_get_real_ts64(&now);

	inode = ceph_get_inode(dentry->d_sb, vino);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	iinfo.inline_version = CEPH_INLINE_NONE;
	iinfo.change_attr = 1;
	ceph_encode_timespec64(&iinfo.btime, &now);

	if (req->r_pagelist) {
		iinfo.xattr_len = req->r_pagelist->length;
		iinfo.xattr_data = req->r_pagelist->mapped_tail;
	} else {
		/* fake it */
		iinfo.xattr_len = ARRAY_SIZE(xattr_buf);
		iinfo.xattr_data = xattr_buf;
		memset(iinfo.xattr_data, 0, iinfo.xattr_len);
	}

	in.ino = cpu_to_le64(vino.ino);
	in.snapid = cpu_to_le64(CEPH_NOSNAP);
	in.version = cpu_to_le64(1);	// ???
	in.cap.caps = in.cap.wanted = cpu_to_le32(CEPH_CAP_ALL_FILE);
	in.cap.cap_id = cpu_to_le64(1);
	in.cap.realm = cpu_to_le64(ci->i_snap_realm->ino);
	in.cap.flags = CEPH_CAP_FLAG_AUTH;
	in.ctime = in.mtime = in.atime = iinfo.btime;
	in.truncate_seq = cpu_to_le32(1);
	in.truncate_size = cpu_to_le64(-1ULL);
	in.xattr_version = cpu_to_le64(1);
	in.uid = cpu_to_le32(from_kuid(&init_user_ns, current_fsuid()));
	if (dir->i_mode & S_ISGID) {
		in.gid = cpu_to_le32(from_kgid(&init_user_ns, dir->i_gid));

		/* Directories always inherit the setgid bit. */
		if (S_ISDIR(mode))
			mode |= S_ISGID;
	} else {
		in.gid = cpu_to_le32(from_kgid(&init_user_ns, current_fsgid()));
	}
	in.mode = cpu_to_le32((u32)mode);

	in.nlink = cpu_to_le32(1);
	in.max_size = cpu_to_le64(lo->stripe_unit);

	ceph_file_layout_to_legacy(lo, &in.layout);
	/* lo is private, so pool_ns can't change */
	pool_ns = rcu_dereference_raw(lo->pool_ns);
	if (pool_ns) {
		iinfo.pool_ns_len = pool_ns->len;
		iinfo.pool_ns_data = pool_ns->str;
	}

	down_read(&mdsc->snap_rwsem);
	ret = ceph_fill_inode(inode, NULL, &iinfo, NULL, req->r_session,
			      req->r_fmode, NULL);
	up_read(&mdsc->snap_rwsem);
	if (ret) {
		dout("%s failed to fill inode: %d\n", __func__, ret);
		ceph_dir_clear_complete(dir);
		if (!d_unhashed(dentry))
			d_drop(dentry);
		if (inode->i_state & I_NEW)
			discard_new_inode(inode);
	} else {
		struct dentry *dn;

		dout("%s d_adding new inode 0x%llx to 0x%llx/%s\n", __func__,
			vino.ino, ceph_ino(dir), dentry->d_name.name);
		ceph_dir_clear_ordered(dir);
		ceph_init_inode_acls(inode, as_ctx);
		if (inode->i_state & I_NEW) {
			/*
			 * If it's not I_NEW, then someone created this before
			 * we got here. Assume the server is aware of it at
			 * that point and don't worry about setting
			 * CEPH_I_ASYNC_CREATE.
			 */
			ceph_inode(inode)->i_ceph_flags = CEPH_I_ASYNC_CREATE;
			unlock_new_inode(inode);
		}
		if (d_in_lookup(dentry) || d_really_is_negative(dentry)) {
			if (!d_unhashed(dentry))
				d_drop(dentry);
			dn = d_splice_alias(inode, dentry);
			WARN_ON_ONCE(dn && dn != dentry);
		}
		file->f_mode |= FMODE_CREATED;
		ret = finish_open(file, dentry, ceph_open);
	}

	spin_lock(&dentry->d_lock);
	di->flags &= ~CEPH_DENTRY_ASYNC_CREATE;
	wake_up_bit(&di->flags, CEPH_DENTRY_ASYNC_CREATE_BIT);
	spin_unlock(&dentry->d_lock);

	return ret;
}

/*
 * Do a lookup + open with a single request.  If we get a non-existent
 * file or symlink, return 1 so the VFS can retry.
 */
int ceph_atomic_open(struct inode *dir, struct dentry *dentry,
		     struct file *file, unsigned flags, umode_t mode)
{
	struct ceph_fs_client *fsc = ceph_sb_to_client(dir->i_sb);
	struct ceph_mds_client *mdsc = fsc->mdsc;
	struct ceph_mds_request *req;
	struct dentry *dn;
	struct ceph_acl_sec_ctx as_ctx = {};
	bool try_async = ceph_test_mount_opt(fsc, ASYNC_DIROPS);
	int mask;
	int err;

	dout("atomic_open %p dentry %p '%pd' %s flags %d mode 0%o\n",
	     dir, dentry, dentry,
	     d_unhashed(dentry) ? "unhashed" : "hashed", flags, mode);

	if (dentry->d_name.len > NAME_MAX)
		return -ENAMETOOLONG;

	err = ceph_wait_on_conflict_unlink(dentry);
	if (err)
		return err;
	/*
	 * Do not truncate the file, since atomic_open is called before the
	 * permission check. The caller will do the truncation afterward.
	 */
	flags &= ~O_TRUNC;

	if (flags & O_CREAT) {
		if (ceph_quota_is_max_files_exceeded(dir))
			return -EDQUOT;
		err = ceph_pre_init_acls(dir, &mode, &as_ctx);
		if (err < 0)
			return err;
		err = ceph_security_init_secctx(dentry, mode, &as_ctx);
		if (err < 0)
			goto out_ctx;
		/* Async create can't handle more than a page of xattrs */
		if (as_ctx.pagelist &&
		    !list_is_singular(&as_ctx.pagelist->head))
			try_async = false;
	} else if (!d_in_lookup(dentry)) {
		/* If it's not being looked up, it's negative */
		return -ENOENT;
	}
retry:
	/* do the open */
	req = prepare_open_request(dir->i_sb, flags, mode);
	if (IS_ERR(req)) {
		err = PTR_ERR(req);
		goto out_ctx;
	}
	req->r_dentry = dget(dentry);
	req->r_num_caps = 2;
	mask = CEPH_STAT_CAP_INODE | CEPH_CAP_AUTH_SHARED;
	if (ceph_security_xattr_wanted(dir))
		mask |= CEPH_CAP_XATTR_SHARED;
	req->r_args.open.mask = cpu_to_le32(mask);
	req->r_parent = dir;
	ihold(dir);

	if (flags & O_CREAT) {
		struct ceph_file_layout lo;

		req->r_dentry_drop = CEPH_CAP_FILE_SHARED | CEPH_CAP_AUTH_EXCL |
				     CEPH_CAP_XATTR_EXCL;
		req->r_dentry_unless = CEPH_CAP_FILE_EXCL;
		if (as_ctx.pagelist) {
			req->r_pagelist = as_ctx.pagelist;
			as_ctx.pagelist = NULL;
		}
		if (try_async &&
		    (req->r_dir_caps =
		      try_prep_async_create(dir, dentry, &lo,
					    &req->r_deleg_ino))) {
			struct ceph_dentry_info *di = ceph_dentry(dentry);

			set_bit(CEPH_MDS_R_ASYNC, &req->r_req_flags);
			req->r_args.open.flags |= cpu_to_le32(CEPH_O_EXCL);
			req->r_callback = ceph_async_create_cb;

			spin_lock(&dentry->d_lock);
			di->flags |= CEPH_DENTRY_ASYNC_CREATE;
			spin_unlock(&dentry->d_lock);

			err = ceph_mdsc_submit_request(mdsc, dir, req);
			if (!err) {
				err = ceph_finish_async_create(dir, dentry,
							file, mode, req,
							&as_ctx, &lo);
			} else if (err == -EJUKEBOX) {
				restore_deleg_ino(dir, req->r_deleg_ino);
				ceph_mdsc_put_request(req);
				try_async = false;
				ceph_put_string(rcu_dereference_raw(lo.pool_ns));
				goto retry;
			}
			ceph_put_string(rcu_dereference_raw(lo.pool_ns));
			goto out_req;
		}
	}

	set_bit(CEPH_MDS_R_PARENT_LOCKED, &req->r_req_flags);
	err = ceph_mdsc_do_request(mdsc, (flags & O_CREAT) ? dir : NULL, req);
	if (err == -ENOENT) {
		dentry = ceph_handle_snapdir(req, dentry);
		if (IS_ERR(dentry)) {
			err = PTR_ERR(dentry);
			goto out_req;
		}
		err = 0;
	}

	if (!err && (flags & O_CREAT) && !req->r_reply_info.head->is_dentry)
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
			struct inode *newino = d_inode(dentry);

			cache_file_layout(dir, newino);
			ceph_init_inode_acls(newino, &as_ctx);
			file->f_mode |= FMODE_CREATED;
		}
		err = finish_open(file, dentry, ceph_open);
	}
out_req:
	ceph_mdsc_put_request(req);
out_ctx:
	ceph_release_acl_sec_ctx(&as_ctx);
	dout("atomic_open result=%d\n", err);
	return err;
}

int ceph_release(struct inode *inode, struct file *file)
{
	struct ceph_inode_info *ci = ceph_inode(inode);

	if (S_ISDIR(inode->i_mode)) {
		struct ceph_dir_file_info *dfi = file->private_data;
		dout("release inode %p dir file %p\n", inode, file);
		WARN_ON(!list_empty(&dfi->file_info.rw_contexts));

		ceph_put_fmode(ci, dfi->file_info.fmode, 1);

		if (dfi->last_readdir)
			ceph_mdsc_put_request(dfi->last_readdir);
		kfree(dfi->last_name);
		kfree(dfi->dir_info);
		kmem_cache_free(ceph_dir_file_cachep, dfi);
	} else {
		struct ceph_file_info *fi = file->private_data;
		dout("release inode %p regular file %p\n", inode, file);
		WARN_ON(!list_empty(&fi->rw_contexts));

		ceph_fscache_unuse_cookie(inode, file->f_mode & FMODE_WRITE);
		ceph_put_fmode(ci, fi->fmode, 1);

		kmem_cache_free(ceph_file_cachep, fi);
	}

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
 * Completely synchronous read and write methods.  Direct from __user
 * buffer to osd, or directly to user pages (if O_DIRECT).
 *
 * If the read spans object boundary, just do multiple reads.  (That's not
 * atomic, but good enough for now.)
 *
 * If we get a short result from the OSD, check against i_size; we need to
 * only return a short read to the caller if we hit EOF.
 */
static ssize_t ceph_sync_read(struct kiocb *iocb, struct iov_iter *to,
			      int *retry_op)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file_inode(file);
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_fs_client *fsc = ceph_inode_to_client(inode);
	struct ceph_osd_client *osdc = &fsc->client->osdc;
	ssize_t ret;
	u64 off = iocb->ki_pos;
	u64 len = iov_iter_count(to);
	u64 i_size = i_size_read(inode);

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
	ret = filemap_write_and_wait_range(inode->i_mapping,
					   off, off + len - 1);
	if (ret < 0)
		return ret;

	ret = 0;
	while ((len = iov_iter_count(to)) > 0) {
		struct ceph_osd_request *req;
		struct page **pages;
		int num_pages;
		size_t page_off;
		bool more;
		int idx;
		size_t left;

		req = ceph_osdc_new_request(osdc, &ci->i_layout,
					ci->i_vino, off, &len, 0, 1,
					CEPH_OSD_OP_READ, CEPH_OSD_FLAG_READ,
					NULL, ci->i_truncate_seq,
					ci->i_truncate_size, false);
		if (IS_ERR(req)) {
			ret = PTR_ERR(req);
			break;
		}

		more = len < iov_iter_count(to);

		num_pages = calc_pages_for(off, len);
		page_off = off & ~PAGE_MASK;
		pages = ceph_alloc_page_vector(num_pages, GFP_KERNEL);
		if (IS_ERR(pages)) {
			ceph_osdc_put_request(req);
			ret = PTR_ERR(pages);
			break;
		}

		osd_req_op_extent_osd_data_pages(req, 0, pages, len, page_off,
						 false, false);
		ceph_osdc_start_request(osdc, req);
		ret = ceph_osdc_wait_request(osdc, req);

		ceph_update_read_metrics(&fsc->mdsc->metric,
					 req->r_start_latency,
					 req->r_end_latency,
					 len, ret);

		ceph_osdc_put_request(req);

		i_size = i_size_read(inode);
		dout("sync_read %llu~%llu got %zd i_size %llu%s\n",
		     off, len, ret, i_size, (more ? " MORE" : ""));

		if (ret == -ENOENT)
			ret = 0;
		if (ret >= 0 && ret < len && (off + ret < i_size)) {
			int zlen = min(len - ret, i_size - off - ret);
			int zoff = page_off + ret;
			dout("sync_read zero gap %llu~%llu\n",
                             off + ret, off + ret + zlen);
			ceph_zero_page_vector_range(zoff, zlen, pages);
			ret += zlen;
		}

		idx = 0;
		left = ret > 0 ? ret : 0;
		while (left > 0) {
			size_t len, copied;
			page_off = off & ~PAGE_MASK;
			len = min_t(size_t, left, PAGE_SIZE - page_off);
			SetPageUptodate(pages[idx]);
			copied = copy_page_to_iter(pages[idx++],
						   page_off, len, to);
			off += copied;
			left -= copied;
			if (copied < len) {
				ret = -EFAULT;
				break;
			}
		}
		ceph_release_page_vector(pages, num_pages);

		if (ret < 0) {
			if (ret == -EBLOCKLISTED)
				fsc->blocklisted = true;
			break;
		}

		if (off >= i_size || !more)
			break;
	}

	if (off > iocb->ki_pos) {
		if (off >= i_size) {
			*retry_op = CHECK_EOF;
			ret = i_size - iocb->ki_pos;
			iocb->ki_pos = i_size;
		} else {
			ret = off - iocb->ki_pos;
			iocb->ki_pos = off;
		}
	}

	dout("sync_read result %zd retry_op %d\n", ret, *retry_op);
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
	struct timespec64 mtime;
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

	if (aio_req->iocb->ki_flags & IOCB_DIRECT)
		inode_dio_end(inode);

	ret = aio_req->error;
	if (!ret)
		ret = aio_req->total_len;

	dout("ceph_aio_complete %p rc %d\n", inode, ret);

	if (ret >= 0 && aio_req->write) {
		int dirty;

		loff_t endoff = aio_req->iocb->ki_pos + aio_req->total_len;
		if (endoff > i_size_read(inode)) {
			if (ceph_inode_set_size(inode, endoff))
				ceph_check_caps(ci, CHECK_CAPS_AUTHONLY);
		}

		spin_lock(&ci->i_ceph_lock);
		dirty = __ceph_mark_dirty_caps(ci, CEPH_CAP_FILE_WR,
					       &aio_req->prealloc_cf);
		spin_unlock(&ci->i_ceph_lock);
		if (dirty)
			__mark_inode_dirty(inode, dirty);

	}

	ceph_put_cap_refs(ci, (aio_req->write ? CEPH_CAP_FILE_WR :
						CEPH_CAP_FILE_RD));

	aio_req->iocb->ki_complete(aio_req->iocb, ret);

	ceph_free_cap_flush(aio_req->prealloc_cf);
	kfree(aio_req);
}

static void ceph_aio_complete_req(struct ceph_osd_request *req)
{
	int rc = req->r_result;
	struct inode *inode = req->r_inode;
	struct ceph_aio_request *aio_req = req->r_priv;
	struct ceph_osd_data *osd_data = osd_req_op_extent_osd_data(req, 0);
	struct ceph_client_metric *metric = &ceph_sb_to_mdsc(inode->i_sb)->metric;
	unsigned int len = osd_data->bvec_pos.iter.bi_size;

	BUG_ON(osd_data->type != CEPH_OSD_DATA_TYPE_BVECS);
	BUG_ON(!osd_data->num_bvecs);

	dout("ceph_aio_complete_req %p rc %d bytes %u\n", inode, rc, len);

	if (rc == -EOLDSNAPC) {
		struct ceph_aio_work *aio_work;
		BUG_ON(!aio_req->write);

		aio_work = kmalloc(sizeof(*aio_work), GFP_NOFS);
		if (aio_work) {
			INIT_WORK(&aio_work->work, ceph_aio_retry_work);
			aio_work->req = req;
			queue_work(ceph_inode_to_client(inode)->inode_wq,
				   &aio_work->work);
			return;
		}
		rc = -ENOMEM;
	} else if (!aio_req->write) {
		if (rc == -ENOENT)
			rc = 0;
		if (rc >= 0 && len > rc) {
			struct iov_iter i;
			int zlen = len - rc;

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

			iov_iter_bvec(&i, ITER_DEST, osd_data->bvec_pos.bvecs,
				      osd_data->num_bvecs, len);
			iov_iter_advance(&i, rc);
			iov_iter_zero(zlen, &i);
		}
	}

	/* r_start_latency == 0 means the request was not submitted */
	if (req->r_start_latency) {
		if (aio_req->write)
			ceph_update_write_metrics(metric, req->r_start_latency,
						  req->r_end_latency, len, rc);
		else
			ceph_update_read_metrics(metric, req->r_start_latency,
						 req->r_end_latency, len, rc);
	}

	put_bvecs(osd_data->bvec_pos.bvecs, osd_data->num_bvecs,
		  aio_req->should_dirty);
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

	req = ceph_osdc_alloc_request(orig_req->r_osdc, snapc, 1,
			false, GFP_NOFS);
	if (!req) {
		ret = -ENOMEM;
		req = orig_req;
		goto out;
	}

	req->r_flags = /* CEPH_OSD_FLAG_ORDERSNAP | */ CEPH_OSD_FLAG_WRITE;
	ceph_oloc_copy(&req->r_base_oloc, &orig_req->r_base_oloc);
	ceph_oid_copy(&req->r_base_oid, &orig_req->r_base_oid);

	req->r_ops[0] = orig_req->r_ops[0];

	req->r_mtime = aio_req->mtime;
	req->r_data_offset = req->r_ops[0].extent.offset;

	ret = ceph_osdc_alloc_messages(req, GFP_NOFS);
	if (ret) {
		ceph_osdc_put_request(req);
		req = orig_req;
		goto out;
	}

	ceph_osdc_put_request(orig_req);

	req->r_callback = ceph_aio_complete_req;
	req->r_inode = inode;
	req->r_priv = aio_req;

	ceph_osdc_start_request(req->r_osdc, req);
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
	struct ceph_client_metric *metric = &fsc->mdsc->metric;
	struct ceph_vino vino;
	struct ceph_osd_request *req;
	struct bio_vec *bvecs;
	struct ceph_aio_request *aio_req = NULL;
	int num_pages = 0;
	int flags;
	int ret = 0;
	struct timespec64 mtime = current_time(inode);
	size_t count = iov_iter_count(iter);
	loff_t pos = iocb->ki_pos;
	bool write = iov_iter_rw(iter) == WRITE;
	bool should_dirty = !write && user_backed_iter(iter);

	if (write && ceph_snap(file_inode(file)) != CEPH_NOSNAP)
		return -EROFS;

	dout("sync_direct_%s on file %p %lld~%u snapc %p seq %lld\n",
	     (write ? "write" : "read"), file, pos, (unsigned)count,
	     snapc, snapc ? snapc->seq : 0);

	if (write) {
		int ret2;

		ceph_fscache_invalidate(inode, true);

		ret2 = invalidate_inode_pages2_range(inode->i_mapping,
					pos >> PAGE_SHIFT,
					(pos + count - 1) >> PAGE_SHIFT);
		if (ret2 < 0)
			dout("invalidate_inode_pages2_range returned %d\n", ret2);

		flags = /* CEPH_OSD_FLAG_ORDERSNAP | */ CEPH_OSD_FLAG_WRITE;
	} else {
		flags = CEPH_OSD_FLAG_READ;
	}

	while (iov_iter_count(iter) > 0) {
		u64 size = iov_iter_count(iter);
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

		len = iter_get_bvecs_alloc(iter, size, &bvecs, &num_pages);
		if (len < 0) {
			ceph_osdc_put_request(req);
			ret = len;
			break;
		}
		if (len != size)
			osd_req_op_extent_update(req, 0, len);

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
						   PAGE_ALIGN(pos + len) - 1);

			req->r_mtime = mtime;
		}

		osd_req_op_extent_osd_data_bvecs(req, 0, bvecs, num_pages, len);

		if (aio_req) {
			aio_req->total_len += len;
			aio_req->num_reqs++;
			atomic_inc(&aio_req->pending_reqs);

			req->r_callback = ceph_aio_complete_req;
			req->r_inode = inode;
			req->r_priv = aio_req;
			list_add_tail(&req->r_private_item, &aio_req->osd_reqs);

			pos += len;
			continue;
		}

		ceph_osdc_start_request(req->r_osdc, req);
		ret = ceph_osdc_wait_request(&fsc->client->osdc, req);

		if (write)
			ceph_update_write_metrics(metric, req->r_start_latency,
						  req->r_end_latency, len, ret);
		else
			ceph_update_read_metrics(metric, req->r_start_latency,
						 req->r_end_latency, len, ret);

		size = i_size_read(inode);
		if (!write) {
			if (ret == -ENOENT)
				ret = 0;
			if (ret >= 0 && ret < len && pos + ret < size) {
				struct iov_iter i;
				int zlen = min_t(size_t, len - ret,
						 size - pos - ret);

				iov_iter_bvec(&i, ITER_DEST, bvecs, num_pages, len);
				iov_iter_advance(&i, ret);
				iov_iter_zero(zlen, &i);
				ret += zlen;
			}
			if (ret >= 0)
				len = ret;
		}

		put_bvecs(bvecs, num_pages, should_dirty);
		ceph_osdc_put_request(req);
		if (ret < 0)
			break;

		pos += len;
		if (!write && pos >= size)
			break;

		if (write && pos > size) {
			if (ceph_inode_set_size(inode, pos))
				ceph_check_caps(ceph_inode(inode),
						CHECK_CAPS_AUTHONLY);
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
		inode_dio_begin(inode);
		while (!list_empty(&osd_reqs)) {
			req = list_first_entry(&osd_reqs,
					       struct ceph_osd_request,
					       r_private_item);
			list_del_init(&req->r_private_item);
			if (ret >= 0)
				ceph_osdc_start_request(req->r_osdc, req);
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
	struct timespec64 mtime = current_time(inode);
	size_t count = iov_iter_count(from);

	if (ceph_snap(file_inode(file)) != CEPH_NOSNAP)
		return -EROFS;

	dout("sync_write on file %p %lld~%u snapc %p seq %lld\n",
	     file, pos, (unsigned)count, snapc, snapc->seq);

	ret = filemap_write_and_wait_range(inode->i_mapping,
					   pos, pos + count - 1);
	if (ret < 0)
		return ret;

	ceph_fscache_invalidate(inode, false);
	ret = invalidate_inode_pages2_range(inode->i_mapping,
					    pos >> PAGE_SHIFT,
					    (pos + count - 1) >> PAGE_SHIFT);
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
		ceph_osdc_start_request(&fsc->client->osdc, req);
		ret = ceph_osdc_wait_request(&fsc->client->osdc, req);

		ceph_update_write_metrics(&fsc->mdsc->metric, req->r_start_latency,
					  req->r_end_latency, len, ret);
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
						CHECK_CAPS_AUTHONLY);
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
	bool direct_lock = iocb->ki_flags & IOCB_DIRECT;
	ssize_t ret;
	int want = 0, got = 0;
	int retry_op = 0, read = 0;

again:
	dout("aio_read %p %llx.%llx %llu~%u trying to get caps on %p\n",
	     inode, ceph_vinop(inode), iocb->ki_pos, (unsigned)len, inode);

	if (ceph_inode_is_shutdown(inode))
		return -ESTALE;

	if (direct_lock)
		ceph_start_io_direct(inode);
	else
		ceph_start_io_read(inode);

	if (!(fi->flags & CEPH_F_SYNC) && !direct_lock)
		want |= CEPH_CAP_FILE_CACHE;
	if (fi->fmode & CEPH_FILE_MODE_LAZY)
		want |= CEPH_CAP_FILE_LAZYIO;

	ret = ceph_get_caps(filp, CEPH_CAP_FILE_RD, want, -1, &got);
	if (ret < 0) {
		if (direct_lock)
			ceph_end_io_direct(inode);
		else
			ceph_end_io_read(inode);
		return ret;
	}

	if ((got & (CEPH_CAP_FILE_CACHE|CEPH_CAP_FILE_LAZYIO)) == 0 ||
	    (iocb->ki_flags & IOCB_DIRECT) ||
	    (fi->flags & CEPH_F_SYNC)) {

		dout("aio_sync_read %p %llx.%llx %llu~%u got cap refs on %s\n",
		     inode, ceph_vinop(inode), iocb->ki_pos, (unsigned)len,
		     ceph_cap_string(got));

		if (!ceph_has_inline_data(ci)) {
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
		CEPH_DEFINE_RW_CONTEXT(rw_ctx, got);
		dout("aio_read %p %llx.%llx %llu~%u got cap refs on %s\n",
		     inode, ceph_vinop(inode), iocb->ki_pos, (unsigned)len,
		     ceph_cap_string(got));
		ceph_add_rw_context(fi, &rw_ctx);
		ret = generic_file_read_iter(iocb, to);
		ceph_del_rw_context(fi, &rw_ctx);
	}

	dout("aio_read %p %llx.%llx dropping cap refs on %s = %d\n",
	     inode, ceph_vinop(inode), ceph_cap_string(got), (int)ret);
	ceph_put_cap_refs(ci, got);

	if (direct_lock)
		ceph_end_io_direct(inode);
	else
		ceph_end_io_read(inode);

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
	struct ceph_fs_client *fsc = ceph_inode_to_client(inode);
	struct ceph_osd_client *osdc = &fsc->client->osdc;
	struct ceph_cap_flush *prealloc_cf;
	ssize_t count, written = 0;
	int err, want = 0, got;
	bool direct_lock = false;
	u32 map_flags;
	u64 pool_flags;
	loff_t pos;
	loff_t limit = max(i_size_read(inode), fsc->max_file_size);

	if (ceph_inode_is_shutdown(inode))
		return -ESTALE;

	if (ceph_snap(inode) != CEPH_NOSNAP)
		return -EROFS;

	prealloc_cf = ceph_alloc_cap_flush();
	if (!prealloc_cf)
		return -ENOMEM;

	if ((iocb->ki_flags & (IOCB_DIRECT | IOCB_APPEND)) == IOCB_DIRECT)
		direct_lock = true;

retry_snap:
	if (direct_lock)
		ceph_start_io_direct(inode);
	else
		ceph_start_io_write(inode);

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
	if (unlikely(pos >= limit)) {
		err = -EFBIG;
		goto out;
	} else {
		iov_iter_truncate(from, limit - pos);
	}

	count = iov_iter_count(from);
	if (ceph_quota_is_max_bytes_exceeded(inode, pos + count)) {
		err = -EDQUOT;
		goto out;
	}

	down_read(&osdc->lock);
	map_flags = osdc->osdmap->flags;
	pool_flags = ceph_pg_pool_flags(osdc->osdmap, ci->i_layout.pool_id);
	up_read(&osdc->lock);
	if ((map_flags & CEPH_OSDMAP_FULL) ||
	    (pool_flags & CEPH_POOL_FLAG_FULL)) {
		err = -ENOSPC;
		goto out;
	}

	err = file_remove_privs(file);
	if (err)
		goto out;

	dout("aio_write %p %llx.%llx %llu~%zd getting caps. i_size %llu\n",
	     inode, ceph_vinop(inode), pos, count, i_size_read(inode));
	if (!(fi->flags & CEPH_F_SYNC) && !direct_lock)
		want |= CEPH_CAP_FILE_BUFFER;
	if (fi->fmode & CEPH_FILE_MODE_LAZY)
		want |= CEPH_CAP_FILE_LAZYIO;
	got = 0;
	err = ceph_get_caps(file, CEPH_CAP_FILE_WR, want, pos + count, &got);
	if (err < 0)
		goto out;

	err = file_update_time(file);
	if (err)
		goto out_caps;

	inode_inc_iversion_raw(inode);

	dout("aio_write %p %llx.%llx %llu~%zd got cap refs on %s\n",
	     inode, ceph_vinop(inode), pos, count, ceph_cap_string(got));

	if ((got & (CEPH_CAP_FILE_BUFFER|CEPH_CAP_FILE_LAZYIO)) == 0 ||
	    (iocb->ki_flags & IOCB_DIRECT) || (fi->flags & CEPH_F_SYNC) ||
	    (ci->i_ceph_flags & CEPH_I_ERROR_WRITE)) {
		struct ceph_snap_context *snapc;
		struct iov_iter data;

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
		if (direct_lock)
			ceph_end_io_direct(inode);
		else
			ceph_end_io_write(inode);
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
		written = generic_perform_write(iocb, from);
		if (likely(written >= 0))
			iocb->ki_pos = pos + written;
		ceph_end_io_write(inode);
	}

	if (written >= 0) {
		int dirty;

		spin_lock(&ci->i_ceph_lock);
		dirty = __ceph_mark_dirty_caps(ci, CEPH_CAP_FILE_WR,
					       &prealloc_cf);
		spin_unlock(&ci->i_ceph_lock);
		if (dirty)
			__mark_inode_dirty(inode, dirty);
		if (ceph_quota_is_max_bytes_approaching(inode, iocb->ki_pos))
			ceph_check_caps(ci, CHECK_CAPS_FLUSH);
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
		if ((map_flags & CEPH_OSDMAP_NEARFULL) ||
		    (pool_flags & CEPH_POOL_FLAG_NEARFULL))
			iocb->ki_flags |= IOCB_DSYNC;
		written = generic_write_sync(iocb, written);
	}

	goto out_unlocked;
out_caps:
	ceph_put_cap_refs(ci, got);
out:
	if (direct_lock)
		ceph_end_io_direct(inode);
	else
		ceph_end_io_write(inode);
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
	if (whence == SEEK_END || whence == SEEK_DATA || whence == SEEK_HOLE) {
		struct inode *inode = file_inode(file);
		int ret;

		ret = ceph_do_getattr(inode, CEPH_STAT_CAP_SIZE, false);
		if (ret < 0)
			return ret;
	}
	return generic_file_llseek(file, offset, whence);
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

	if (ceph_inode_is_shutdown(inode))
		return -EIO;

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
	ceph_osdc_start_request(&fsc->client->osdc, req);
	ret = ceph_osdc_wait_request(&fsc->client->osdc, req);
	if (ret == -ENOENT)
		ret = 0;
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
	struct ceph_cap_flush *prealloc_cf;
	int want, got = 0;
	int dirty;
	int ret = 0;
	loff_t endoff = 0;
	loff_t size;

	dout("%s %p %llx.%llx mode %x, offset %llu length %llu\n", __func__,
	     inode, ceph_vinop(inode), mode, offset, length);

	if (mode != (FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE))
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

	size = i_size_read(inode);

	/* Are we punching a hole beyond EOF? */
	if (offset >= size)
		goto unlock;
	if ((offset + length) > size)
		length = size - offset;

	if (fi->fmode & CEPH_FILE_MODE_LAZY)
		want = CEPH_CAP_FILE_BUFFER | CEPH_CAP_FILE_LAZYIO;
	else
		want = CEPH_CAP_FILE_BUFFER;

	ret = ceph_get_caps(file, CEPH_CAP_FILE_WR, want, endoff, &got);
	if (ret < 0)
		goto unlock;

	ret = file_modified(file);
	if (ret)
		goto put_caps;

	filemap_invalidate_lock(inode->i_mapping);
	ceph_fscache_invalidate(inode, false);
	ceph_zero_pagecache_range(inode, offset, length);
	ret = ceph_zero_objects(inode, offset, length);

	if (!ret) {
		spin_lock(&ci->i_ceph_lock);
		dirty = __ceph_mark_dirty_caps(ci, CEPH_CAP_FILE_WR,
					       &prealloc_cf);
		spin_unlock(&ci->i_ceph_lock);
		if (dirty)
			__mark_inode_dirty(inode, dirty);
	}
	filemap_invalidate_unlock(inode->i_mapping);

put_caps:
	ceph_put_cap_refs(ci, got);
unlock:
	inode_unlock(inode);
	ceph_free_cap_flush(prealloc_cf);
	return ret;
}

/*
 * This function tries to get FILE_WR capabilities for dst_ci and FILE_RD for
 * src_ci.  Two attempts are made to obtain both caps, and an error is return if
 * this fails; zero is returned on success.
 */
static int get_rd_wr_caps(struct file *src_filp, int *src_got,
			  struct file *dst_filp,
			  loff_t dst_endoff, int *dst_got)
{
	int ret = 0;
	bool retrying = false;

retry_caps:
	ret = ceph_get_caps(dst_filp, CEPH_CAP_FILE_WR, CEPH_CAP_FILE_BUFFER,
			    dst_endoff, dst_got);
	if (ret < 0)
		return ret;

	/*
	 * Since we're already holding the FILE_WR capability for the dst file,
	 * we would risk a deadlock by using ceph_get_caps.  Thus, we'll do some
	 * retry dance instead to try to get both capabilities.
	 */
	ret = ceph_try_get_caps(file_inode(src_filp),
				CEPH_CAP_FILE_RD, CEPH_CAP_FILE_SHARED,
				false, src_got);
	if (ret <= 0) {
		/* Start by dropping dst_ci caps and getting src_ci caps */
		ceph_put_cap_refs(ceph_inode(file_inode(dst_filp)), *dst_got);
		if (retrying) {
			if (!ret)
				/* ceph_try_get_caps masks EAGAIN */
				ret = -EAGAIN;
			return ret;
		}
		ret = ceph_get_caps(src_filp, CEPH_CAP_FILE_RD,
				    CEPH_CAP_FILE_SHARED, -1, src_got);
		if (ret < 0)
			return ret;
		/*... drop src_ci caps too, and retry */
		ceph_put_cap_refs(ceph_inode(file_inode(src_filp)), *src_got);
		retrying = true;
		goto retry_caps;
	}
	return ret;
}

static void put_rd_wr_caps(struct ceph_inode_info *src_ci, int src_got,
			   struct ceph_inode_info *dst_ci, int dst_got)
{
	ceph_put_cap_refs(src_ci, src_got);
	ceph_put_cap_refs(dst_ci, dst_got);
}

/*
 * This function does several size-related checks, returning an error if:
 *  - source file is smaller than off+len
 *  - destination file size is not OK (inode_newsize_ok())
 *  - max bytes quotas is exceeded
 */
static int is_file_size_ok(struct inode *src_inode, struct inode *dst_inode,
			   loff_t src_off, loff_t dst_off, size_t len)
{
	loff_t size, endoff;

	size = i_size_read(src_inode);
	/*
	 * Don't copy beyond source file EOF.  Instead of simply setting length
	 * to (size - src_off), just drop to VFS default implementation, as the
	 * local i_size may be stale due to other clients writing to the source
	 * inode.
	 */
	if (src_off + len > size) {
		dout("Copy beyond EOF (%llu + %zu > %llu)\n",
		     src_off, len, size);
		return -EOPNOTSUPP;
	}
	size = i_size_read(dst_inode);

	endoff = dst_off + len;
	if (inode_newsize_ok(dst_inode, endoff))
		return -EOPNOTSUPP;

	if (ceph_quota_is_max_bytes_exceeded(dst_inode, endoff))
		return -EDQUOT;

	return 0;
}

static struct ceph_osd_request *
ceph_alloc_copyfrom_request(struct ceph_osd_client *osdc,
			    u64 src_snapid,
			    struct ceph_object_id *src_oid,
			    struct ceph_object_locator *src_oloc,
			    struct ceph_object_id *dst_oid,
			    struct ceph_object_locator *dst_oloc,
			    u32 truncate_seq, u64 truncate_size)
{
	struct ceph_osd_request *req;
	int ret;
	u32 src_fadvise_flags =
		CEPH_OSD_OP_FLAG_FADVISE_SEQUENTIAL |
		CEPH_OSD_OP_FLAG_FADVISE_NOCACHE;
	u32 dst_fadvise_flags =
		CEPH_OSD_OP_FLAG_FADVISE_SEQUENTIAL |
		CEPH_OSD_OP_FLAG_FADVISE_DONTNEED;

	req = ceph_osdc_alloc_request(osdc, NULL, 1, false, GFP_KERNEL);
	if (!req)
		return ERR_PTR(-ENOMEM);

	req->r_flags = CEPH_OSD_FLAG_WRITE;

	ceph_oloc_copy(&req->r_t.base_oloc, dst_oloc);
	ceph_oid_copy(&req->r_t.base_oid, dst_oid);

	ret = osd_req_op_copy_from_init(req, src_snapid, 0,
					src_oid, src_oloc,
					src_fadvise_flags,
					dst_fadvise_flags,
					truncate_seq,
					truncate_size,
					CEPH_OSD_COPY_FROM_FLAG_TRUNCATE_SEQ);
	if (ret)
		goto out;

	ret = ceph_osdc_alloc_messages(req, GFP_KERNEL);
	if (ret)
		goto out;

	return req;

out:
	ceph_osdc_put_request(req);
	return ERR_PTR(ret);
}

static ssize_t ceph_do_objects_copy(struct ceph_inode_info *src_ci, u64 *src_off,
				    struct ceph_inode_info *dst_ci, u64 *dst_off,
				    struct ceph_fs_client *fsc,
				    size_t len, unsigned int flags)
{
	struct ceph_object_locator src_oloc, dst_oloc;
	struct ceph_object_id src_oid, dst_oid;
	struct ceph_osd_client *osdc;
	struct ceph_osd_request *req;
	size_t bytes = 0;
	u64 src_objnum, src_objoff, dst_objnum, dst_objoff;
	u32 src_objlen, dst_objlen;
	u32 object_size = src_ci->i_layout.object_size;
	int ret;

	src_oloc.pool = src_ci->i_layout.pool_id;
	src_oloc.pool_ns = ceph_try_get_string(src_ci->i_layout.pool_ns);
	dst_oloc.pool = dst_ci->i_layout.pool_id;
	dst_oloc.pool_ns = ceph_try_get_string(dst_ci->i_layout.pool_ns);
	osdc = &fsc->client->osdc;

	while (len >= object_size) {
		ceph_calc_file_object_mapping(&src_ci->i_layout, *src_off,
					      object_size, &src_objnum,
					      &src_objoff, &src_objlen);
		ceph_calc_file_object_mapping(&dst_ci->i_layout, *dst_off,
					      object_size, &dst_objnum,
					      &dst_objoff, &dst_objlen);
		ceph_oid_init(&src_oid);
		ceph_oid_printf(&src_oid, "%llx.%08llx",
				src_ci->i_vino.ino, src_objnum);
		ceph_oid_init(&dst_oid);
		ceph_oid_printf(&dst_oid, "%llx.%08llx",
				dst_ci->i_vino.ino, dst_objnum);
		/* Do an object remote copy */
		req = ceph_alloc_copyfrom_request(osdc, src_ci->i_vino.snap,
						  &src_oid, &src_oloc,
						  &dst_oid, &dst_oloc,
						  dst_ci->i_truncate_seq,
						  dst_ci->i_truncate_size);
		if (IS_ERR(req))
			ret = PTR_ERR(req);
		else {
			ceph_osdc_start_request(osdc, req);
			ret = ceph_osdc_wait_request(osdc, req);
			ceph_update_copyfrom_metrics(&fsc->mdsc->metric,
						     req->r_start_latency,
						     req->r_end_latency,
						     object_size, ret);
			ceph_osdc_put_request(req);
		}
		if (ret) {
			if (ret == -EOPNOTSUPP) {
				fsc->have_copy_from2 = false;
				pr_notice("OSDs don't support copy-from2; disabling copy offload\n");
			}
			dout("ceph_osdc_copy_from returned %d\n", ret);
			if (!bytes)
				bytes = ret;
			goto out;
		}
		len -= object_size;
		bytes += object_size;
		*src_off += object_size;
		*dst_off += object_size;
	}

out:
	ceph_oloc_destroy(&src_oloc);
	ceph_oloc_destroy(&dst_oloc);
	return bytes;
}

static ssize_t __ceph_copy_file_range(struct file *src_file, loff_t src_off,
				      struct file *dst_file, loff_t dst_off,
				      size_t len, unsigned int flags)
{
	struct inode *src_inode = file_inode(src_file);
	struct inode *dst_inode = file_inode(dst_file);
	struct ceph_inode_info *src_ci = ceph_inode(src_inode);
	struct ceph_inode_info *dst_ci = ceph_inode(dst_inode);
	struct ceph_cap_flush *prealloc_cf;
	struct ceph_fs_client *src_fsc = ceph_inode_to_client(src_inode);
	loff_t size;
	ssize_t ret = -EIO, bytes;
	u64 src_objnum, dst_objnum, src_objoff, dst_objoff;
	u32 src_objlen, dst_objlen;
	int src_got = 0, dst_got = 0, err, dirty;

	if (src_inode->i_sb != dst_inode->i_sb) {
		struct ceph_fs_client *dst_fsc = ceph_inode_to_client(dst_inode);

		if (ceph_fsid_compare(&src_fsc->client->fsid,
				      &dst_fsc->client->fsid)) {
			dout("Copying files across clusters: src: %pU dst: %pU\n",
			     &src_fsc->client->fsid, &dst_fsc->client->fsid);
			return -EXDEV;
		}
	}
	if (ceph_snap(dst_inode) != CEPH_NOSNAP)
		return -EROFS;

	/*
	 * Some of the checks below will return -EOPNOTSUPP, which will force a
	 * fallback to the default VFS copy_file_range implementation.  This is
	 * desirable in several cases (for ex, the 'len' is smaller than the
	 * size of the objects, or in cases where that would be more
	 * efficient).
	 */

	if (ceph_test_mount_opt(src_fsc, NOCOPYFROM))
		return -EOPNOTSUPP;

	if (!src_fsc->have_copy_from2)
		return -EOPNOTSUPP;

	/*
	 * Striped file layouts require that we copy partial objects, but the
	 * OSD copy-from operation only supports full-object copies.  Limit
	 * this to non-striped file layouts for now.
	 */
	if ((src_ci->i_layout.stripe_unit != dst_ci->i_layout.stripe_unit) ||
	    (src_ci->i_layout.stripe_count != 1) ||
	    (dst_ci->i_layout.stripe_count != 1) ||
	    (src_ci->i_layout.object_size != dst_ci->i_layout.object_size)) {
		dout("Invalid src/dst files layout\n");
		return -EOPNOTSUPP;
	}

	if (len < src_ci->i_layout.object_size)
		return -EOPNOTSUPP; /* no remote copy will be done */

	prealloc_cf = ceph_alloc_cap_flush();
	if (!prealloc_cf)
		return -ENOMEM;

	/* Start by sync'ing the source and destination files */
	ret = file_write_and_wait_range(src_file, src_off, (src_off + len));
	if (ret < 0) {
		dout("failed to write src file (%zd)\n", ret);
		goto out;
	}
	ret = file_write_and_wait_range(dst_file, dst_off, (dst_off + len));
	if (ret < 0) {
		dout("failed to write dst file (%zd)\n", ret);
		goto out;
	}

	/*
	 * We need FILE_WR caps for dst_ci and FILE_RD for src_ci as other
	 * clients may have dirty data in their caches.  And OSDs know nothing
	 * about caps, so they can't safely do the remote object copies.
	 */
	err = get_rd_wr_caps(src_file, &src_got,
			     dst_file, (dst_off + len), &dst_got);
	if (err < 0) {
		dout("get_rd_wr_caps returned %d\n", err);
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = is_file_size_ok(src_inode, dst_inode, src_off, dst_off, len);
	if (ret < 0)
		goto out_caps;

	/* Drop dst file cached pages */
	ceph_fscache_invalidate(dst_inode, false);
	ret = invalidate_inode_pages2_range(dst_inode->i_mapping,
					    dst_off >> PAGE_SHIFT,
					    (dst_off + len) >> PAGE_SHIFT);
	if (ret < 0) {
		dout("Failed to invalidate inode pages (%zd)\n", ret);
		ret = 0; /* XXX */
	}
	ceph_calc_file_object_mapping(&src_ci->i_layout, src_off,
				      src_ci->i_layout.object_size,
				      &src_objnum, &src_objoff, &src_objlen);
	ceph_calc_file_object_mapping(&dst_ci->i_layout, dst_off,
				      dst_ci->i_layout.object_size,
				      &dst_objnum, &dst_objoff, &dst_objlen);
	/* object-level offsets need to the same */
	if (src_objoff != dst_objoff) {
		ret = -EOPNOTSUPP;
		goto out_caps;
	}

	/*
	 * Do a manual copy if the object offset isn't object aligned.
	 * 'src_objlen' contains the bytes left until the end of the object,
	 * starting at the src_off
	 */
	if (src_objoff) {
		dout("Initial partial copy of %u bytes\n", src_objlen);

		/*
		 * we need to temporarily drop all caps as we'll be calling
		 * {read,write}_iter, which will get caps again.
		 */
		put_rd_wr_caps(src_ci, src_got, dst_ci, dst_got);
		ret = do_splice_direct(src_file, &src_off, dst_file,
				       &dst_off, src_objlen, flags);
		/* Abort on short copies or on error */
		if (ret < src_objlen) {
			dout("Failed partial copy (%zd)\n", ret);
			goto out;
		}
		len -= ret;
		err = get_rd_wr_caps(src_file, &src_got,
				     dst_file, (dst_off + len), &dst_got);
		if (err < 0)
			goto out;
		err = is_file_size_ok(src_inode, dst_inode,
				      src_off, dst_off, len);
		if (err < 0)
			goto out_caps;
	}

	size = i_size_read(dst_inode);
	bytes = ceph_do_objects_copy(src_ci, &src_off, dst_ci, &dst_off,
				     src_fsc, len, flags);
	if (bytes <= 0) {
		if (!ret)
			ret = bytes;
		goto out_caps;
	}
	dout("Copied %zu bytes out of %zu\n", bytes, len);
	len -= bytes;
	ret += bytes;

	file_update_time(dst_file);
	inode_inc_iversion_raw(dst_inode);

	if (dst_off > size) {
		/* Let the MDS know about dst file size change */
		if (ceph_inode_set_size(dst_inode, dst_off) ||
		    ceph_quota_is_max_bytes_approaching(dst_inode, dst_off))
			ceph_check_caps(dst_ci, CHECK_CAPS_AUTHONLY | CHECK_CAPS_FLUSH);
	}
	/* Mark Fw dirty */
	spin_lock(&dst_ci->i_ceph_lock);
	dirty = __ceph_mark_dirty_caps(dst_ci, CEPH_CAP_FILE_WR, &prealloc_cf);
	spin_unlock(&dst_ci->i_ceph_lock);
	if (dirty)
		__mark_inode_dirty(dst_inode, dirty);

out_caps:
	put_rd_wr_caps(src_ci, src_got, dst_ci, dst_got);

	/*
	 * Do the final manual copy if we still have some bytes left, unless
	 * there were errors in remote object copies (len >= object_size).
	 */
	if (len && (len < src_ci->i_layout.object_size)) {
		dout("Final partial copy of %zu bytes\n", len);
		bytes = do_splice_direct(src_file, &src_off, dst_file,
					 &dst_off, len, flags);
		if (bytes > 0)
			ret += bytes;
		else
			dout("Failed partial copy (%zd)\n", bytes);
	}

out:
	ceph_free_cap_flush(prealloc_cf);

	return ret;
}

static ssize_t ceph_copy_file_range(struct file *src_file, loff_t src_off,
				    struct file *dst_file, loff_t dst_off,
				    size_t len, unsigned int flags)
{
	ssize_t ret;

	ret = __ceph_copy_file_range(src_file, src_off, dst_file, dst_off,
				     len, flags);

	if (ret == -EOPNOTSUPP || ret == -EXDEV)
		ret = generic_copy_file_range(src_file, src_off, dst_file,
					      dst_off, len, flags);
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
	.setlease = simple_nosetlease,
	.flock = ceph_flock,
	.splice_read = generic_file_splice_read,
	.splice_write = iter_file_splice_write,
	.unlocked_ioctl = ceph_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
	.fallocate	= ceph_fallocate,
	.copy_file_range = ceph_copy_file_range,
};
