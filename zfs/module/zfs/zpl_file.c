/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2011, Lawrence Livermore National Security, LLC.
 * Copyright (c) 2015 by Chunwei Chen. All rights reserved.
 */


#ifdef CONFIG_COMPAT
#include <linux/compat.h>
#endif
#include <sys/dmu_objset.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_vnops.h>
#include <sys/zfs_znode.h>
#include <sys/zpl.h>


static int
zpl_open(struct inode *ip, struct file *filp)
{
	cred_t *cr = CRED();
	int error;
	fstrans_cookie_t cookie;

	error = generic_file_open(ip, filp);
	if (error)
		return (error);

	crhold(cr);
	cookie = spl_fstrans_mark();
	error = -zfs_open(ip, filp->f_mode, filp->f_flags, cr);
	spl_fstrans_unmark(cookie);
	crfree(cr);
	ASSERT3S(error, <=, 0);

	return (error);
}

static int
zpl_release(struct inode *ip, struct file *filp)
{
	cred_t *cr = CRED();
	int error;
	fstrans_cookie_t cookie;

	cookie = spl_fstrans_mark();
	if (ITOZ(ip)->z_atime_dirty)
		zfs_mark_inode_dirty(ip);

	crhold(cr);
	error = -zfs_close(ip, filp->f_flags, cr);
	spl_fstrans_unmark(cookie);
	crfree(cr);
	ASSERT3S(error, <=, 0);

	return (error);
}

static int
zpl_iterate(struct file *filp, struct dir_context *ctx)
{
	struct dentry *dentry = filp->f_path.dentry;
	cred_t *cr = CRED();
	int error;
	fstrans_cookie_t cookie;

	crhold(cr);
	cookie = spl_fstrans_mark();
	error = -zfs_readdir(dentry->d_inode, ctx, cr);
	spl_fstrans_unmark(cookie);
	crfree(cr);
	ASSERT3S(error, <=, 0);

	return (error);
}

#if !defined(HAVE_VFS_ITERATE) && !defined(HAVE_VFS_ITERATE_SHARED)
static int
zpl_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct dir_context ctx = DIR_CONTEXT_INIT(dirent, filldir, filp->f_pos);
	int error;

	error = zpl_iterate(filp, &ctx);
	filp->f_pos = ctx.pos;

	return (error);
}
#endif /* HAVE_VFS_ITERATE */

#if defined(HAVE_FSYNC_WITH_DENTRY)
/*
 * Linux 2.6.x - 2.6.34 API,
 * Through 2.6.34 the nfsd kernel server would pass a NULL 'file struct *'
 * to the fops->fsync() hook.  For this reason, we must be careful not to
 * use filp unconditionally.
 */
static int
zpl_fsync(struct file *filp, struct dentry *dentry, int datasync)
{
	cred_t *cr = CRED();
	int error;
	fstrans_cookie_t cookie;

	crhold(cr);
	cookie = spl_fstrans_mark();
	error = -zfs_fsync(dentry->d_inode, datasync, cr);
	spl_fstrans_unmark(cookie);
	crfree(cr);
	ASSERT3S(error, <=, 0);

	return (error);
}

#ifdef HAVE_FILE_AIO_FSYNC
static int
zpl_aio_fsync(struct kiocb *kiocb, int datasync)
{
	struct file *filp = kiocb->ki_filp;
	return (zpl_fsync(filp, filp->f_path.dentry, datasync));
}
#endif

#elif defined(HAVE_FSYNC_WITHOUT_DENTRY)
/*
 * Linux 2.6.35 - 3.0 API,
 * As of 2.6.35 the dentry argument to the fops->fsync() hook was deemed
 * redundant.  The dentry is still accessible via filp->f_path.dentry,
 * and we are guaranteed that filp will never be NULL.
 */
static int
zpl_fsync(struct file *filp, int datasync)
{
	struct inode *inode = filp->f_mapping->host;
	cred_t *cr = CRED();
	int error;
	fstrans_cookie_t cookie;

	crhold(cr);
	cookie = spl_fstrans_mark();
	error = -zfs_fsync(inode, datasync, cr);
	spl_fstrans_unmark(cookie);
	crfree(cr);
	ASSERT3S(error, <=, 0);

	return (error);
}

#ifdef HAVE_FILE_AIO_FSYNC
static int
zpl_aio_fsync(struct kiocb *kiocb, int datasync)
{
	return (zpl_fsync(kiocb->ki_filp, datasync));
}
#endif

#elif defined(HAVE_FSYNC_RANGE)
/*
 * Linux 3.1 - 3.x API,
 * As of 3.1 the responsibility to call filemap_write_and_wait_range() has
 * been pushed down in to the .fsync() vfs hook.  Additionally, the i_mutex
 * lock is no longer held by the caller, for zfs we don't require the lock
 * to be held so we don't acquire it.
 */
static int
zpl_fsync(struct file *filp, loff_t start, loff_t end, int datasync)
{
	struct inode *inode = filp->f_mapping->host;
	cred_t *cr = CRED();
	int error;
	fstrans_cookie_t cookie;

	error = filemap_write_and_wait_range(inode->i_mapping, start, end);
	if (error)
		return (error);

	crhold(cr);
	cookie = spl_fstrans_mark();
	error = -zfs_fsync(inode, datasync, cr);
	spl_fstrans_unmark(cookie);
	crfree(cr);
	ASSERT3S(error, <=, 0);

	return (error);
}

#ifdef HAVE_FILE_AIO_FSYNC
static int
zpl_aio_fsync(struct kiocb *kiocb, int datasync)
{
	return (zpl_fsync(kiocb->ki_filp, kiocb->ki_pos, -1, datasync));
}
#endif

#else
#error "Unsupported fops->fsync() implementation"
#endif

static ssize_t
zpl_read_common_iovec(struct inode *ip, const struct iovec *iovp, size_t count,
    unsigned long nr_segs, loff_t *ppos, uio_seg_t segment, int flags,
    cred_t *cr, size_t skip)
{
	ssize_t read;
	uio_t uio;
	int error;
	fstrans_cookie_t cookie;

	uio.uio_iov = iovp;
	uio.uio_skip = skip;
	uio.uio_resid = count;
	uio.uio_iovcnt = nr_segs;
	uio.uio_loffset = *ppos;
	uio.uio_limit = MAXOFFSET_T;
	uio.uio_segflg = segment;

	cookie = spl_fstrans_mark();
	error = -zfs_read(ip, &uio, flags, cr);
	spl_fstrans_unmark(cookie);
	if (error < 0)
		return (error);

	read = count - uio.uio_resid;
	*ppos += read;
	task_io_account_read(read);

	return (read);
}

inline ssize_t
zpl_read_common(struct inode *ip, const char *buf, size_t len, loff_t *ppos,
    uio_seg_t segment, int flags, cred_t *cr)
{
	struct iovec iov;

	iov.iov_base = (void *)buf;
	iov.iov_len = len;

	return (zpl_read_common_iovec(ip, &iov, len, 1, ppos, segment,
	    flags, cr, 0));
}

static ssize_t
zpl_iter_read_common(struct kiocb *kiocb, const struct iovec *iovp,
    unsigned long nr_segs, size_t count, uio_seg_t seg, size_t skip)
{
	cred_t *cr = CRED();
	struct file *filp = kiocb->ki_filp;
	ssize_t read;

	crhold(cr);
	read = zpl_read_common_iovec(filp->f_mapping->host, iovp, count,
	    nr_segs, &kiocb->ki_pos, seg, filp->f_flags, cr, skip);
	crfree(cr);

	file_accessed(filp);
	return (read);
}

#if defined(HAVE_VFS_RW_ITERATE)
static ssize_t
zpl_iter_read(struct kiocb *kiocb, struct iov_iter *to)
{
	ssize_t ret;
	uio_seg_t seg = UIO_USERSPACE;
	if (to->type & ITER_KVEC)
		seg = UIO_SYSSPACE;
	if (to->type & ITER_BVEC)
		seg = UIO_BVEC;
	ret = zpl_iter_read_common(kiocb, to->iov, to->nr_segs,
	    iov_iter_count(to), seg, to->iov_offset);
	if (ret > 0)
		iov_iter_advance(to, ret);
	return (ret);
}
#else
static ssize_t
zpl_aio_read(struct kiocb *kiocb, const struct iovec *iovp,
    unsigned long nr_segs, loff_t pos)
{
	ssize_t ret;
	size_t count;

	ret = generic_segment_checks(iovp, &nr_segs, &count, VERIFY_WRITE);
	if (ret)
		return (ret);

	return (zpl_iter_read_common(kiocb, iovp, nr_segs, count,
	    UIO_USERSPACE, 0));
}
#endif /* HAVE_VFS_RW_ITERATE */

static ssize_t
zpl_write_common_iovec(struct inode *ip, const struct iovec *iovp, size_t count,
    unsigned long nr_segs, loff_t *ppos, uio_seg_t segment, int flags,
    cred_t *cr, size_t skip)
{
	ssize_t wrote;
	uio_t uio;
	int error;
	fstrans_cookie_t cookie;

	if (flags & O_APPEND)
		*ppos = i_size_read(ip);

	uio.uio_iov = iovp;
	uio.uio_skip = skip;
	uio.uio_resid = count;
	uio.uio_iovcnt = nr_segs;
	uio.uio_loffset = *ppos;
	uio.uio_limit = MAXOFFSET_T;
	uio.uio_segflg = segment;

	cookie = spl_fstrans_mark();
	error = -zfs_write(ip, &uio, flags, cr);
	spl_fstrans_unmark(cookie);
	if (error < 0)
		return (error);

	wrote = count - uio.uio_resid;
	*ppos += wrote;
	task_io_account_write(wrote);

	return (wrote);
}

inline ssize_t
zpl_write_common(struct inode *ip, const char *buf, size_t len, loff_t *ppos,
    uio_seg_t segment, int flags, cred_t *cr)
{
	struct iovec iov;

	iov.iov_base = (void *)buf;
	iov.iov_len = len;

	return (zpl_write_common_iovec(ip, &iov, len, 1, ppos, segment,
	    flags, cr, 0));
}

static ssize_t
zpl_iter_write_common(struct kiocb *kiocb, const struct iovec *iovp,
    unsigned long nr_segs, size_t count, uio_seg_t seg, size_t skip)
{
	cred_t *cr = CRED();
	struct file *filp = kiocb->ki_filp;
	ssize_t wrote;

	crhold(cr);
	wrote = zpl_write_common_iovec(filp->f_mapping->host, iovp, count,
	    nr_segs, &kiocb->ki_pos, seg, filp->f_flags, cr, skip);
	crfree(cr);

	return (wrote);
}

#if defined(HAVE_VFS_RW_ITERATE)
static ssize_t
zpl_iter_write(struct kiocb *kiocb, struct iov_iter *from)
{
	size_t count;
	ssize_t ret;
	uio_seg_t seg = UIO_USERSPACE;

#ifndef HAVE_GENERIC_WRITE_CHECKS_KIOCB
	struct file *file = kiocb->ki_filp;
	struct address_space *mapping = file->f_mapping;
	struct inode *ip = mapping->host;
	int isblk = S_ISBLK(ip->i_mode);

	count = iov_iter_count(from);
	ret = generic_write_checks(file, &kiocb->ki_pos, &count, isblk);
	if (ret)
		return (ret);
#else
	/*
	 * XXX - ideally this check should be in the same lock region with
	 * write operations, so that there's no TOCTTOU race when doing
	 * append and someone else grow the file.
	 */
	ret = generic_write_checks(kiocb, from);
	if (ret <= 0)
		return (ret);
	count = ret;
#endif

	if (from->type & ITER_KVEC)
		seg = UIO_SYSSPACE;
	if (from->type & ITER_BVEC)
		seg = UIO_BVEC;

	ret = zpl_iter_write_common(kiocb, from->iov, from->nr_segs,
	    count, seg, from->iov_offset);
	if (ret > 0)
		iov_iter_advance(from, ret);

	return (ret);
}
#else
static ssize_t
zpl_aio_write(struct kiocb *kiocb, const struct iovec *iovp,
    unsigned long nr_segs, loff_t pos)
{
	struct file *file = kiocb->ki_filp;
	struct address_space *mapping = file->f_mapping;
	struct inode *ip = mapping->host;
	int isblk = S_ISBLK(ip->i_mode);
	size_t count;
	ssize_t ret;

	ret = generic_segment_checks(iovp, &nr_segs, &count, VERIFY_READ);
	if (ret)
		return (ret);

	ret = generic_write_checks(file, &pos, &count, isblk);
	if (ret)
		return (ret);

	return (zpl_iter_write_common(kiocb, iovp, nr_segs, count,
	    UIO_USERSPACE, 0));
}
#endif /* HAVE_VFS_RW_ITERATE */

static loff_t
zpl_llseek(struct file *filp, loff_t offset, int whence)
{
#if defined(SEEK_HOLE) && defined(SEEK_DATA)
	fstrans_cookie_t cookie;

	if (whence == SEEK_DATA || whence == SEEK_HOLE) {
		struct inode *ip = filp->f_mapping->host;
		loff_t maxbytes = ip->i_sb->s_maxbytes;
		loff_t error;

		spl_inode_lock_shared(ip);
		cookie = spl_fstrans_mark();
		error = -zfs_holey(ip, whence, &offset);
		spl_fstrans_unmark(cookie);
		if (error == 0)
			error = lseek_execute(filp, ip, offset, maxbytes);
		spl_inode_unlock_shared(ip);

		return (error);
	}
#endif /* SEEK_HOLE && SEEK_DATA */

	return (generic_file_llseek(filp, offset, whence));
}

/*
 * It's worth taking a moment to describe how mmap is implemented
 * for zfs because it differs considerably from other Linux filesystems.
 * However, this issue is handled the same way under OpenSolaris.
 *
 * The issue is that by design zfs bypasses the Linux page cache and
 * leaves all caching up to the ARC.  This has been shown to work
 * well for the common read(2)/write(2) case.  However, mmap(2)
 * is problem because it relies on being tightly integrated with the
 * page cache.  To handle this we cache mmap'ed files twice, once in
 * the ARC and a second time in the page cache.  The code is careful
 * to keep both copies synchronized.
 *
 * When a file with an mmap'ed region is written to using write(2)
 * both the data in the ARC and existing pages in the page cache
 * are updated.  For a read(2) data will be read first from the page
 * cache then the ARC if needed.  Neither a write(2) or read(2) will
 * will ever result in new pages being added to the page cache.
 *
 * New pages are added to the page cache only via .readpage() which
 * is called when the vfs needs to read a page off disk to back the
 * virtual memory region.  These pages may be modified without
 * notifying the ARC and will be written out periodically via
 * .writepage().  This will occur due to either a sync or the usual
 * page aging behavior.  Note because a read(2) of a mmap'ed file
 * will always check the page cache first even when the ARC is out
 * of date correct data will still be returned.
 *
 * While this implementation ensures correct behavior it does have
 * have some drawbacks.  The most obvious of which is that it
 * increases the required memory footprint when access mmap'ed
 * files.  It also adds additional complexity to the code keeping
 * both caches synchronized.
 *
 * Longer term it may be possible to cleanly resolve this wart by
 * mapping page cache pages directly on to the ARC buffers.  The
 * Linux address space operations are flexible enough to allow
 * selection of which pages back a particular index.  The trick
 * would be working out the details of which subsystem is in
 * charge, the ARC, the page cache, or both.  It may also prove
 * helpful to move the ARC buffers to a scatter-gather lists
 * rather than a vmalloc'ed region.
 */
static int
zpl_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct inode *ip = filp->f_mapping->host;
	znode_t *zp = ITOZ(ip);
	int error;
	fstrans_cookie_t cookie;

	cookie = spl_fstrans_mark();
	error = -zfs_map(ip, vma->vm_pgoff, (caddr_t *)vma->vm_start,
	    (size_t)(vma->vm_end - vma->vm_start), vma->vm_flags);
	spl_fstrans_unmark(cookie);
	if (error)
		return (error);

	error = generic_file_mmap(filp, vma);
	if (error)
		return (error);

	mutex_enter(&zp->z_lock);
	zp->z_is_mapped = 1;
	mutex_exit(&zp->z_lock);

	return (error);
}

/*
 * Populate a page with data for the Linux page cache.  This function is
 * only used to support mmap(2).  There will be an identical copy of the
 * data in the ARC which is kept up to date via .write() and .writepage().
 *
 * Current this function relies on zpl_read_common() and the O_DIRECT
 * flag to read in a page.  This works but the more correct way is to
 * update zfs_fillpage() to be Linux friendly and use that interface.
 */
static int
zpl_readpage(struct file *filp, struct page *pp)
{
	struct inode *ip;
	struct page *pl[1];
	int error = 0;
	fstrans_cookie_t cookie;

	ASSERT(PageLocked(pp));
	ip = pp->mapping->host;
	pl[0] = pp;

	cookie = spl_fstrans_mark();
	error = -zfs_getpage(ip, pl, 1);
	spl_fstrans_unmark(cookie);

	if (error) {
		SetPageError(pp);
		ClearPageUptodate(pp);
	} else {
		ClearPageError(pp);
		SetPageUptodate(pp);
		flush_dcache_page(pp);
	}

	unlock_page(pp);
	return (error);
}

/*
 * Populate a set of pages with data for the Linux page cache.  This
 * function will only be called for read ahead and never for demand
 * paging.  For simplicity, the code relies on read_cache_pages() to
 * correctly lock each page for IO and call zpl_readpage().
 */
static int
zpl_readpages(struct file *filp, struct address_space *mapping,
	struct list_head *pages, unsigned nr_pages)
{
	return (read_cache_pages(mapping, pages,
	    (filler_t *)zpl_readpage, filp));
}

int
zpl_putpage(struct page *pp, struct writeback_control *wbc, void *data)
{
	struct address_space *mapping = data;
	fstrans_cookie_t cookie;

	ASSERT(PageLocked(pp));
	ASSERT(!PageWriteback(pp));

	cookie = spl_fstrans_mark();
	(void) zfs_putpage(mapping->host, pp, wbc);
	spl_fstrans_unmark(cookie);

	return (0);
}

static int
zpl_writepages(struct address_space *mapping, struct writeback_control *wbc)
{
	znode_t		*zp = ITOZ(mapping->host);
	zfs_sb_t	*zsb = ITOZSB(mapping->host);
	enum writeback_sync_modes sync_mode;
	int result;

	ZFS_ENTER(zsb);
	if (zsb->z_os->os_sync == ZFS_SYNC_ALWAYS)
		wbc->sync_mode = WB_SYNC_ALL;
	ZFS_EXIT(zsb);
	sync_mode = wbc->sync_mode;

	/*
	 * We don't want to run write_cache_pages() in SYNC mode here, because
	 * that would make putpage() wait for a single page to be committed to
	 * disk every single time, resulting in atrocious performance. Instead
	 * we run it once in non-SYNC mode so that the ZIL gets all the data,
	 * and then we commit it all in one go.
	 */
	wbc->sync_mode = WB_SYNC_NONE;
	result = write_cache_pages(mapping, wbc, zpl_putpage, mapping);
	if (sync_mode != wbc->sync_mode) {
		ZFS_ENTER(zsb);
		ZFS_VERIFY_ZP(zp);
		if (zsb->z_log != NULL)
			zil_commit(zsb->z_log, zp->z_id);
		ZFS_EXIT(zsb);

		/*
		 * We need to call write_cache_pages() again (we can't just
		 * return after the commit) because the previous call in
		 * non-SYNC mode does not guarantee that we got all the dirty
		 * pages (see the implementation of write_cache_pages() for
		 * details). That being said, this is a no-op in most cases.
		 */
		wbc->sync_mode = sync_mode;
		result = write_cache_pages(mapping, wbc, zpl_putpage, mapping);
	}
	return (result);
}

/*
 * Write out dirty pages to the ARC, this function is only required to
 * support mmap(2).  Mapped pages may be dirtied by memory operations
 * which never call .write().  These dirty pages are kept in sync with
 * the ARC buffers via this hook.
 */
static int
zpl_writepage(struct page *pp, struct writeback_control *wbc)
{
	if (ITOZSB(pp->mapping->host)->z_os->os_sync == ZFS_SYNC_ALWAYS)
		wbc->sync_mode = WB_SYNC_ALL;

	return (zpl_putpage(pp, wbc, pp->mapping));
}

/*
 * The only flag combination which matches the behavior of zfs_space()
 * is FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE.  The FALLOC_FL_PUNCH_HOLE
 * flag was introduced in the 2.6.38 kernel.
 */
#if defined(HAVE_FILE_FALLOCATE) || defined(HAVE_INODE_FALLOCATE)
long
zpl_fallocate_common(struct inode *ip, int mode, loff_t offset, loff_t len)
{
	int error = -EOPNOTSUPP;

#if defined(FALLOC_FL_PUNCH_HOLE) && defined(FALLOC_FL_KEEP_SIZE)
	cred_t *cr = CRED();
	flock64_t bf;
	loff_t olen;
	fstrans_cookie_t cookie;

	if (mode != (FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE))
		return (error);

	if (offset < 0 || len <= 0)
		return (-EINVAL);

	spl_inode_lock(ip);
	olen = i_size_read(ip);

	if (offset > olen) {
		spl_inode_unlock(ip);
		return (0);
	}
	if (offset + len > olen)
		len = olen - offset;
	bf.l_type = F_WRLCK;
	bf.l_whence = 0;
	bf.l_start = offset;
	bf.l_len = len;
	bf.l_pid = 0;

	crhold(cr);
	cookie = spl_fstrans_mark();
	error = -zfs_space(ip, F_FREESP, &bf, FWRITE, offset, cr);
	spl_fstrans_unmark(cookie);
	spl_inode_unlock(ip);

	crfree(cr);
#endif /* defined(FALLOC_FL_PUNCH_HOLE) && defined(FALLOC_FL_KEEP_SIZE) */

	ASSERT3S(error, <=, 0);
	return (error);
}
#endif /* defined(HAVE_FILE_FALLOCATE) || defined(HAVE_INODE_FALLOCATE) */

#ifdef HAVE_FILE_FALLOCATE
static long
zpl_fallocate(struct file *filp, int mode, loff_t offset, loff_t len)
{
	return zpl_fallocate_common(filp->f_path.dentry->d_inode,
	    mode, offset, len);
}
#endif /* HAVE_FILE_FALLOCATE */

/*
 * Map zfs file z_pflags (xvattr_t) to linux file attributes. Only file
 * attributes common to both Linux and Solaris are mapped.
 */
static int
zpl_ioctl_getflags(struct file *filp, void __user *arg)
{
	struct inode *ip = file_inode(filp);
	unsigned int ioctl_flags = 0;
	uint64_t zfs_flags = ITOZ(ip)->z_pflags;
	int error;

	if (zfs_flags & ZFS_IMMUTABLE)
		ioctl_flags |= FS_IMMUTABLE_FL;

	if (zfs_flags & ZFS_APPENDONLY)
		ioctl_flags |= FS_APPEND_FL;

	if (zfs_flags & ZFS_NODUMP)
		ioctl_flags |= FS_NODUMP_FL;

	ioctl_flags &= FS_FL_USER_VISIBLE;

	error = copy_to_user(arg, &ioctl_flags, sizeof (ioctl_flags));

	return (error);
}

/*
 * fchange() is a helper macro to detect if we have been asked to change a
 * flag. This is ugly, but the requirement that we do this is a consequence of
 * how the Linux file attribute interface was designed. Another consequence is
 * that concurrent modification of files suffers from a TOCTOU race. Neither
 * are things we can fix without modifying the kernel-userland interface, which
 * is outside of our jurisdiction.
 */

#define	fchange(f0, f1, b0, b1) (!((f0) & (b0)) != !((f1) & (b1)))

static int
zpl_ioctl_setflags(struct file *filp, void __user *arg)
{
	struct inode	*ip = file_inode(filp);
	uint64_t	zfs_flags = ITOZ(ip)->z_pflags;
	unsigned int	ioctl_flags;
	cred_t		*cr = CRED();
	xvattr_t	xva;
	xoptattr_t	*xoap;
	int		error;
	fstrans_cookie_t cookie;

	if (copy_from_user(&ioctl_flags, arg, sizeof (ioctl_flags)))
		return (-EFAULT);

	if ((ioctl_flags & ~(FS_IMMUTABLE_FL | FS_APPEND_FL | FS_NODUMP_FL)))
		return (-EOPNOTSUPP);

	if ((ioctl_flags & ~(FS_FL_USER_MODIFIABLE)))
		return (-EACCES);

	if ((fchange(ioctl_flags, zfs_flags, FS_IMMUTABLE_FL, ZFS_IMMUTABLE) ||
	    fchange(ioctl_flags, zfs_flags, FS_APPEND_FL, ZFS_APPENDONLY)) &&
	    !capable(CAP_LINUX_IMMUTABLE))
		return (-EACCES);

	if (!zpl_inode_owner_or_capable(ip))
		return (-EACCES);

	xva_init(&xva);
	xoap = xva_getxoptattr(&xva);

	XVA_SET_REQ(&xva, XAT_IMMUTABLE);
	if (ioctl_flags & FS_IMMUTABLE_FL)
		xoap->xoa_immutable = B_TRUE;

	XVA_SET_REQ(&xva, XAT_APPENDONLY);
	if (ioctl_flags & FS_APPEND_FL)
		xoap->xoa_appendonly = B_TRUE;

	XVA_SET_REQ(&xva, XAT_NODUMP);
	if (ioctl_flags & FS_NODUMP_FL)
		xoap->xoa_nodump = B_TRUE;

	crhold(cr);
	cookie = spl_fstrans_mark();
	error = -zfs_setattr(ip, (vattr_t *)&xva, 0, cr);
	spl_fstrans_unmark(cookie);
	crfree(cr);

	return (error);
}

static long
zpl_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case FS_IOC_GETFLAGS:
		return (zpl_ioctl_getflags(filp, (void *)arg));
	case FS_IOC_SETFLAGS:
		return (zpl_ioctl_setflags(filp, (void *)arg));
	default:
		return (-ENOTTY);
	}
}

#ifdef CONFIG_COMPAT
static long
zpl_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case FS_IOC32_GETFLAGS:
		cmd = FS_IOC_GETFLAGS;
		break;
	case FS_IOC32_SETFLAGS:
		cmd = FS_IOC_SETFLAGS;
		break;
	default:
		return (-ENOTTY);
	}
	return (zpl_ioctl(filp, cmd, (unsigned long)compat_ptr(arg)));
}
#endif /* CONFIG_COMPAT */


const struct address_space_operations zpl_address_space_operations = {
	.readpages	= zpl_readpages,
	.readpage	= zpl_readpage,
	.writepage	= zpl_writepage,
	.writepages	= zpl_writepages,
};

const struct file_operations zpl_file_operations = {
	.open		= zpl_open,
	.release	= zpl_release,
	.llseek		= zpl_llseek,
#ifdef HAVE_VFS_RW_ITERATE
	.read_iter	= zpl_iter_read,
	.write_iter	= zpl_iter_write,
#else
	.aio_read	= zpl_aio_read,
	.aio_write	= zpl_aio_write,
#endif
	.mmap		= zpl_mmap,
	.fsync		= zpl_fsync,
#ifdef HAVE_FILE_AIO_FSYNC
	.aio_fsync	= zpl_aio_fsync,
#endif
#ifdef HAVE_FILE_FALLOCATE
	.fallocate	= zpl_fallocate,
#endif /* HAVE_FILE_FALLOCATE */
	.unlocked_ioctl	= zpl_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= zpl_compat_ioctl,
#endif
};

const struct file_operations zpl_dir_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
#ifdef HAVE_VFS_ITERATE_SHARED
	.iterate_shared	= zpl_iterate,
#elif defined(HAVE_VFS_ITERATE)
	.iterate	= zpl_iterate,
#else
	.readdir	= zpl_readdir,
#endif
	.fsync		= zpl_fsync,
	.unlocked_ioctl = zpl_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = zpl_compat_ioctl,
#endif
};
