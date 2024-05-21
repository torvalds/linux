// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/fs/nfs/file.c
 *
 *  Copyright (C) 1992  Rick Sladkey
 *
 *  Changes Copyright (C) 1994 by Florian La Roche
 *   - Do not copy data too often around in the kernel.
 *   - In nfs_file_read the return value of kmalloc wasn't checked.
 *   - Put in a better version of read look-ahead buffering. Original idea
 *     and implementation by Wai S Kok elekokws@ee.nus.sg.
 *
 *  Expire cache on write to a file by Wai S Kok (Oct 1994).
 *
 *  Total rewrite of read side for new NFS buffer cache.. Linus.
 *
 *  nfs regular file handling functions
 */

#include <linux/module.h>
#include <linux/time.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_mount.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/gfp.h>
#include <linux/swap.h>

#include <linux/uaccess.h>
#include <linux/filelock.h>

#include "delegation.h"
#include "internal.h"
#include "iostat.h"
#include "fscache.h"
#include "pnfs.h"

#include "nfstrace.h"

#define NFSDBG_FACILITY		NFSDBG_FILE

static const struct vm_operations_struct nfs_file_vm_ops;

int nfs_check_flags(int flags)
{
	if ((flags & (O_APPEND | O_DIRECT)) == (O_APPEND | O_DIRECT))
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL_GPL(nfs_check_flags);

/*
 * Open file
 */
static int
nfs_file_open(struct inode *inode, struct file *filp)
{
	int res;

	dprintk("NFS: open file(%pD2)\n", filp);

	nfs_inc_stats(inode, NFSIOS_VFSOPEN);
	res = nfs_check_flags(filp->f_flags);
	if (res)
		return res;

	res = nfs_open(inode, filp);
	if (res == 0)
		filp->f_mode |= FMODE_CAN_ODIRECT;
	return res;
}

int
nfs_file_release(struct inode *inode, struct file *filp)
{
	dprintk("NFS: release(%pD2)\n", filp);

	nfs_inc_stats(inode, NFSIOS_VFSRELEASE);
	nfs_file_clear_open_context(filp);
	nfs_fscache_release_file(inode, filp);
	return 0;
}
EXPORT_SYMBOL_GPL(nfs_file_release);

/**
 * nfs_revalidate_file_size - Revalidate the file size
 * @inode: pointer to inode struct
 * @filp: pointer to struct file
 *
 * Revalidates the file length. This is basically a wrapper around
 * nfs_revalidate_inode() that takes into account the fact that we may
 * have cached writes (in which case we don't care about the server's
 * idea of what the file length is), or O_DIRECT (in which case we
 * shouldn't trust the cache).
 */
static int nfs_revalidate_file_size(struct inode *inode, struct file *filp)
{
	struct nfs_server *server = NFS_SERVER(inode);

	if (filp->f_flags & O_DIRECT)
		goto force_reval;
	if (nfs_check_cache_invalid(inode, NFS_INO_INVALID_SIZE))
		goto force_reval;
	return 0;
force_reval:
	return __nfs_revalidate_inode(server, inode);
}

loff_t nfs_file_llseek(struct file *filp, loff_t offset, int whence)
{
	dprintk("NFS: llseek file(%pD2, %lld, %d)\n",
			filp, offset, whence);

	/*
	 * whence == SEEK_END || SEEK_DATA || SEEK_HOLE => we must revalidate
	 * the cached file length
	 */
	if (whence != SEEK_SET && whence != SEEK_CUR) {
		struct inode *inode = filp->f_mapping->host;

		int retval = nfs_revalidate_file_size(inode, filp);
		if (retval < 0)
			return (loff_t)retval;
	}

	return generic_file_llseek(filp, offset, whence);
}
EXPORT_SYMBOL_GPL(nfs_file_llseek);

/*
 * Flush all dirty pages, and check for write errors.
 */
static int
nfs_file_flush(struct file *file, fl_owner_t id)
{
	struct inode	*inode = file_inode(file);
	errseq_t since;

	dprintk("NFS: flush(%pD2)\n", file);

	nfs_inc_stats(inode, NFSIOS_VFSFLUSH);
	if ((file->f_mode & FMODE_WRITE) == 0)
		return 0;

	/* Flush writes to the server and return any errors */
	since = filemap_sample_wb_err(file->f_mapping);
	nfs_wb_all(inode);
	return filemap_check_wb_err(file->f_mapping, since);
}

ssize_t
nfs_file_read(struct kiocb *iocb, struct iov_iter *to)
{
	struct inode *inode = file_inode(iocb->ki_filp);
	ssize_t result;

	if (iocb->ki_flags & IOCB_DIRECT)
		return nfs_file_direct_read(iocb, to, false);

	dprintk("NFS: read(%pD2, %zu@%lu)\n",
		iocb->ki_filp,
		iov_iter_count(to), (unsigned long) iocb->ki_pos);

	nfs_start_io_read(inode);
	result = nfs_revalidate_mapping(inode, iocb->ki_filp->f_mapping);
	if (!result) {
		result = generic_file_read_iter(iocb, to);
		if (result > 0)
			nfs_add_stats(inode, NFSIOS_NORMALREADBYTES, result);
	}
	nfs_end_io_read(inode);
	return result;
}
EXPORT_SYMBOL_GPL(nfs_file_read);

ssize_t
nfs_file_splice_read(struct file *in, loff_t *ppos, struct pipe_inode_info *pipe,
		     size_t len, unsigned int flags)
{
	struct inode *inode = file_inode(in);
	ssize_t result;

	dprintk("NFS: splice_read(%pD2, %zu@%llu)\n", in, len, *ppos);

	nfs_start_io_read(inode);
	result = nfs_revalidate_mapping(inode, in->f_mapping);
	if (!result) {
		result = filemap_splice_read(in, ppos, pipe, len, flags);
		if (result > 0)
			nfs_add_stats(inode, NFSIOS_NORMALREADBYTES, result);
	}
	nfs_end_io_read(inode);
	return result;
}
EXPORT_SYMBOL_GPL(nfs_file_splice_read);

int
nfs_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct inode *inode = file_inode(file);
	int	status;

	dprintk("NFS: mmap(%pD2)\n", file);

	/* Note: generic_file_mmap() returns ENOSYS on nommu systems
	 *       so we call that before revalidating the mapping
	 */
	status = generic_file_mmap(file, vma);
	if (!status) {
		vma->vm_ops = &nfs_file_vm_ops;
		status = nfs_revalidate_mapping(inode, file->f_mapping);
	}
	return status;
}
EXPORT_SYMBOL_GPL(nfs_file_mmap);

/*
 * Flush any dirty pages for this process, and check for write errors.
 * The return status from this call provides a reliable indication of
 * whether any write errors occurred for this process.
 */
static int
nfs_file_fsync_commit(struct file *file, int datasync)
{
	struct inode *inode = file_inode(file);
	int ret, ret2;

	dprintk("NFS: fsync file(%pD2) datasync %d\n", file, datasync);

	nfs_inc_stats(inode, NFSIOS_VFSFSYNC);
	ret = nfs_commit_inode(inode, FLUSH_SYNC);
	ret2 = file_check_and_advance_wb_err(file);
	if (ret2 < 0)
		return ret2;
	return ret;
}

int
nfs_file_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	struct inode *inode = file_inode(file);
	struct nfs_inode *nfsi = NFS_I(inode);
	long save_nredirtied = atomic_long_read(&nfsi->redirtied_pages);
	long nredirtied;
	int ret;

	trace_nfs_fsync_enter(inode);

	for (;;) {
		ret = file_write_and_wait_range(file, start, end);
		if (ret != 0)
			break;
		ret = nfs_file_fsync_commit(file, datasync);
		if (ret != 0)
			break;
		ret = pnfs_sync_inode(inode, !!datasync);
		if (ret != 0)
			break;
		nredirtied = atomic_long_read(&nfsi->redirtied_pages);
		if (nredirtied == save_nredirtied)
			break;
		save_nredirtied = nredirtied;
	}

	trace_nfs_fsync_exit(inode, ret);
	return ret;
}
EXPORT_SYMBOL_GPL(nfs_file_fsync);

/*
 * Decide whether a read/modify/write cycle may be more efficient
 * then a modify/write/read cycle when writing to a page in the
 * page cache.
 *
 * Some pNFS layout drivers can only read/write at a certain block
 * granularity like all block devices and therefore we must perform
 * read/modify/write whenever a page hasn't read yet and the data
 * to be written there is not aligned to a block boundary and/or
 * smaller than the block size.
 *
 * The modify/write/read cycle may occur if a page is read before
 * being completely filled by the writer.  In this situation, the
 * page must be completely written to stable storage on the server
 * before it can be refilled by reading in the page from the server.
 * This can lead to expensive, small, FILE_SYNC mode writes being
 * done.
 *
 * It may be more efficient to read the page first if the file is
 * open for reading in addition to writing, the page is not marked
 * as Uptodate, it is not dirty or waiting to be committed,
 * indicating that it was previously allocated and then modified,
 * that there were valid bytes of data in that range of the file,
 * and that the new data won't completely replace the old data in
 * that range of the file.
 */
static bool nfs_folio_is_full_write(struct folio *folio, loff_t pos,
				    unsigned int len)
{
	unsigned int pglen = nfs_folio_length(folio);
	unsigned int offset = offset_in_folio(folio, pos);
	unsigned int end = offset + len;

	return !pglen || (end >= pglen && !offset);
}

static bool nfs_want_read_modify_write(struct file *file, struct folio *folio,
				       loff_t pos, unsigned int len)
{
	/*
	 * Up-to-date pages, those with ongoing or full-page write
	 * don't need read/modify/write
	 */
	if (folio_test_uptodate(folio) || folio_test_private(folio) ||
	    nfs_folio_is_full_write(folio, pos, len))
		return false;

	if (pnfs_ld_read_whole_page(file_inode(file)))
		return true;
	/* Open for reading too? */
	if (file->f_mode & FMODE_READ)
		return true;
	return false;
}

/*
 * This does the "real" work of the write. We must allocate and lock the
 * page to be sent back to the generic routine, which then copies the
 * data from user space.
 *
 * If the writer ends up delaying the write, the writer needs to
 * increment the page use counts until he is done with the page.
 */
static int nfs_write_begin(struct file *file, struct address_space *mapping,
			   loff_t pos, unsigned len, struct page **pagep,
			   void **fsdata)
{
	struct folio *folio;
	int once_thru = 0;
	int ret;

	dfprintk(PAGECACHE, "NFS: write_begin(%pD2(%lu), %u@%lld)\n",
		file, mapping->host->i_ino, len, (long long) pos);

start:
	folio = __filemap_get_folio(mapping, pos >> PAGE_SHIFT, FGP_WRITEBEGIN,
				    mapping_gfp_mask(mapping));
	if (IS_ERR(folio))
		return PTR_ERR(folio);
	*pagep = &folio->page;

	ret = nfs_flush_incompatible(file, folio);
	if (ret) {
		folio_unlock(folio);
		folio_put(folio);
	} else if (!once_thru &&
		   nfs_want_read_modify_write(file, folio, pos, len)) {
		once_thru = 1;
		ret = nfs_read_folio(file, folio);
		folio_put(folio);
		if (!ret)
			goto start;
	}
	return ret;
}

static int nfs_write_end(struct file *file, struct address_space *mapping,
			 loff_t pos, unsigned len, unsigned copied,
			 struct page *page, void *fsdata)
{
	struct nfs_open_context *ctx = nfs_file_open_context(file);
	struct folio *folio = page_folio(page);
	unsigned offset = offset_in_folio(folio, pos);
	int status;

	dfprintk(PAGECACHE, "NFS: write_end(%pD2(%lu), %u@%lld)\n",
		file, mapping->host->i_ino, len, (long long) pos);

	/*
	 * Zero any uninitialised parts of the page, and then mark the page
	 * as up to date if it turns out that we're extending the file.
	 */
	if (!folio_test_uptodate(folio)) {
		size_t fsize = folio_size(folio);
		unsigned pglen = nfs_folio_length(folio);
		unsigned end = offset + copied;

		if (pglen == 0) {
			folio_zero_segments(folio, 0, offset, end, fsize);
			folio_mark_uptodate(folio);
		} else if (end >= pglen) {
			folio_zero_segment(folio, end, fsize);
			if (offset == 0)
				folio_mark_uptodate(folio);
		} else
			folio_zero_segment(folio, pglen, fsize);
	}

	status = nfs_update_folio(file, folio, offset, copied);

	folio_unlock(folio);
	folio_put(folio);

	if (status < 0)
		return status;
	NFS_I(mapping->host)->write_io += copied;

	if (nfs_ctx_key_to_expire(ctx, mapping->host))
		nfs_wb_all(mapping->host);

	return copied;
}

/*
 * Partially or wholly invalidate a page
 * - Release the private state associated with a page if undergoing complete
 *   page invalidation
 * - Called if either PG_private or PG_fscache is set on the page
 * - Caller holds page lock
 */
static void nfs_invalidate_folio(struct folio *folio, size_t offset,
				size_t length)
{
	struct inode *inode = folio_file_mapping(folio)->host;
	dfprintk(PAGECACHE, "NFS: invalidate_folio(%lu, %zu, %zu)\n",
		 folio->index, offset, length);

	if (offset != 0 || length < folio_size(folio))
		return;
	/* Cancel any unstarted writes on this page */
	nfs_wb_folio_cancel(inode, folio);
	folio_wait_private_2(folio); /* [DEPRECATED] */
	trace_nfs_invalidate_folio(inode, folio);
}

/*
 * Attempt to release the private state associated with a folio
 * - Called if either private or fscache flags are set on the folio
 * - Caller holds folio lock
 * - Return true (may release folio) or false (may not)
 */
static bool nfs_release_folio(struct folio *folio, gfp_t gfp)
{
	dfprintk(PAGECACHE, "NFS: release_folio(%p)\n", folio);

	/* If the private flag is set, then the folio is not freeable */
	if (folio_test_private(folio)) {
		if ((current_gfp_context(gfp) & GFP_KERNEL) != GFP_KERNEL ||
		    current_is_kswapd())
			return false;
		if (nfs_wb_folio(folio_file_mapping(folio)->host, folio) < 0)
			return false;
	}
	return nfs_fscache_release_folio(folio, gfp);
}

static void nfs_check_dirty_writeback(struct folio *folio,
				bool *dirty, bool *writeback)
{
	struct nfs_inode *nfsi;
	struct address_space *mapping = folio->mapping;

	/*
	 * Check if an unstable folio is currently being committed and
	 * if so, have the VM treat it as if the folio is under writeback
	 * so it will not block due to folios that will shortly be freeable.
	 */
	nfsi = NFS_I(mapping->host);
	if (atomic_read(&nfsi->commit_info.rpcs_out)) {
		*writeback = true;
		return;
	}

	/*
	 * If the private flag is set, then the folio is not freeable
	 * and as the inode is not being committed, it's not going to
	 * be cleaned in the near future so treat it as dirty
	 */
	if (folio_test_private(folio))
		*dirty = true;
}

/*
 * Attempt to clear the private state associated with a page when an error
 * occurs that requires the cached contents of an inode to be written back or
 * destroyed
 * - Called if either PG_private or fscache is set on the page
 * - Caller holds page lock
 * - Return 0 if successful, -error otherwise
 */
static int nfs_launder_folio(struct folio *folio)
{
	struct inode *inode = folio->mapping->host;
	int ret;

	dfprintk(PAGECACHE, "NFS: launder_folio(%ld, %llu)\n",
		inode->i_ino, folio_pos(folio));

	folio_wait_private_2(folio); /* [DEPRECATED] */
	ret = nfs_wb_folio(inode, folio);
	trace_nfs_launder_folio_done(inode, folio, ret);
	return ret;
}

static int nfs_swap_activate(struct swap_info_struct *sis, struct file *file,
						sector_t *span)
{
	unsigned long blocks;
	long long isize;
	int ret;
	struct inode *inode = file_inode(file);
	struct rpc_clnt *clnt = NFS_CLIENT(inode);
	struct nfs_client *cl = NFS_SERVER(inode)->nfs_client;

	spin_lock(&inode->i_lock);
	blocks = inode->i_blocks;
	isize = inode->i_size;
	spin_unlock(&inode->i_lock);
	if (blocks*512 < isize) {
		pr_warn("swap activate: swapfile has holes\n");
		return -EINVAL;
	}

	ret = rpc_clnt_swap_activate(clnt);
	if (ret)
		return ret;
	ret = add_swap_extent(sis, 0, sis->max, 0);
	if (ret < 0) {
		rpc_clnt_swap_deactivate(clnt);
		return ret;
	}

	*span = sis->pages;

	if (cl->rpc_ops->enable_swap)
		cl->rpc_ops->enable_swap(inode);

	sis->flags |= SWP_FS_OPS;
	return ret;
}

static void nfs_swap_deactivate(struct file *file)
{
	struct inode *inode = file_inode(file);
	struct rpc_clnt *clnt = NFS_CLIENT(inode);
	struct nfs_client *cl = NFS_SERVER(inode)->nfs_client;

	rpc_clnt_swap_deactivate(clnt);
	if (cl->rpc_ops->disable_swap)
		cl->rpc_ops->disable_swap(file_inode(file));
}

const struct address_space_operations nfs_file_aops = {
	.read_folio = nfs_read_folio,
	.readahead = nfs_readahead,
	.dirty_folio = filemap_dirty_folio,
	.writepages = nfs_writepages,
	.write_begin = nfs_write_begin,
	.write_end = nfs_write_end,
	.invalidate_folio = nfs_invalidate_folio,
	.release_folio = nfs_release_folio,
	.migrate_folio = nfs_migrate_folio,
	.launder_folio = nfs_launder_folio,
	.is_dirty_writeback = nfs_check_dirty_writeback,
	.error_remove_folio = generic_error_remove_folio,
	.swap_activate = nfs_swap_activate,
	.swap_deactivate = nfs_swap_deactivate,
	.swap_rw = nfs_swap_rw,
};

/*
 * Notification that a PTE pointing to an NFS page is about to be made
 * writable, implying that someone is about to modify the page through a
 * shared-writable mapping
 */
static vm_fault_t nfs_vm_page_mkwrite(struct vm_fault *vmf)
{
	struct file *filp = vmf->vma->vm_file;
	struct inode *inode = file_inode(filp);
	unsigned pagelen;
	vm_fault_t ret = VM_FAULT_NOPAGE;
	struct address_space *mapping;
	struct folio *folio = page_folio(vmf->page);

	dfprintk(PAGECACHE, "NFS: vm_page_mkwrite(%pD2(%lu), offset %lld)\n",
		 filp, filp->f_mapping->host->i_ino,
		 (long long)folio_pos(folio));

	sb_start_pagefault(inode->i_sb);

	/* make sure the cache has finished storing the page */
	if (folio_test_private_2(folio) && /* [DEPRECATED] */
	    folio_wait_private_2_killable(folio) < 0) {
		ret = VM_FAULT_RETRY;
		goto out;
	}

	wait_on_bit_action(&NFS_I(inode)->flags, NFS_INO_INVALIDATING,
			   nfs_wait_bit_killable,
			   TASK_KILLABLE|TASK_FREEZABLE_UNSAFE);

	folio_lock(folio);
	mapping = folio_file_mapping(folio);
	if (mapping != inode->i_mapping)
		goto out_unlock;

	folio_wait_writeback(folio);

	pagelen = nfs_folio_length(folio);
	if (pagelen == 0)
		goto out_unlock;

	ret = VM_FAULT_LOCKED;
	if (nfs_flush_incompatible(filp, folio) == 0 &&
	    nfs_update_folio(filp, folio, 0, pagelen) == 0)
		goto out;

	ret = VM_FAULT_SIGBUS;
out_unlock:
	folio_unlock(folio);
out:
	sb_end_pagefault(inode->i_sb);
	return ret;
}

static const struct vm_operations_struct nfs_file_vm_ops = {
	.fault = filemap_fault,
	.map_pages = filemap_map_pages,
	.page_mkwrite = nfs_vm_page_mkwrite,
};

ssize_t nfs_file_write(struct kiocb *iocb, struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file_inode(file);
	unsigned int mntflags = NFS_SERVER(inode)->flags;
	ssize_t result, written;
	errseq_t since;
	int error;

	result = nfs_key_timeout_notify(file, inode);
	if (result)
		return result;

	if (iocb->ki_flags & IOCB_DIRECT)
		return nfs_file_direct_write(iocb, from, false);

	dprintk("NFS: write(%pD2, %zu@%Ld)\n",
		file, iov_iter_count(from), (long long) iocb->ki_pos);

	if (IS_SWAPFILE(inode))
		goto out_swapfile;
	/*
	 * O_APPEND implies that we must revalidate the file length.
	 */
	if (iocb->ki_flags & IOCB_APPEND || iocb->ki_pos > i_size_read(inode)) {
		result = nfs_revalidate_file_size(inode, file);
		if (result)
			return result;
	}

	nfs_clear_invalid_mapping(file->f_mapping);

	since = filemap_sample_wb_err(file->f_mapping);
	nfs_start_io_write(inode);
	result = generic_write_checks(iocb, from);
	if (result > 0)
		result = generic_perform_write(iocb, from);
	nfs_end_io_write(inode);
	if (result <= 0)
		goto out;

	written = result;
	nfs_add_stats(inode, NFSIOS_NORMALWRITTENBYTES, written);

	if (mntflags & NFS_MOUNT_WRITE_EAGER) {
		result = filemap_fdatawrite_range(file->f_mapping,
						  iocb->ki_pos - written,
						  iocb->ki_pos - 1);
		if (result < 0)
			goto out;
	}
	if (mntflags & NFS_MOUNT_WRITE_WAIT) {
		filemap_fdatawait_range(file->f_mapping,
					iocb->ki_pos - written,
					iocb->ki_pos - 1);
	}
	result = generic_write_sync(iocb, written);
	if (result < 0)
		return result;

out:
	/* Return error values */
	error = filemap_check_wb_err(file->f_mapping, since);
	switch (error) {
	default:
		break;
	case -EDQUOT:
	case -EFBIG:
	case -ENOSPC:
		nfs_wb_all(inode);
		error = file_check_and_advance_wb_err(file);
		if (error < 0)
			result = error;
	}
	return result;

out_swapfile:
	printk(KERN_INFO "NFS: attempt to write to active swap file!\n");
	return -ETXTBSY;
}
EXPORT_SYMBOL_GPL(nfs_file_write);

static int
do_getlk(struct file *filp, int cmd, struct file_lock *fl, int is_local)
{
	struct inode *inode = filp->f_mapping->host;
	int status = 0;
	unsigned int saved_type = fl->c.flc_type;

	/* Try local locking first */
	posix_test_lock(filp, fl);
	if (fl->c.flc_type != F_UNLCK) {
		/* found a conflict */
		goto out;
	}
	fl->c.flc_type = saved_type;

	if (NFS_PROTO(inode)->have_delegation(inode, FMODE_READ))
		goto out_noconflict;

	if (is_local)
		goto out_noconflict;

	status = NFS_PROTO(inode)->lock(filp, cmd, fl);
out:
	return status;
out_noconflict:
	fl->c.flc_type = F_UNLCK;
	goto out;
}

static int
do_unlk(struct file *filp, int cmd, struct file_lock *fl, int is_local)
{
	struct inode *inode = filp->f_mapping->host;
	struct nfs_lock_context *l_ctx;
	int status;

	/*
	 * Flush all pending writes before doing anything
	 * with locks..
	 */
	nfs_wb_all(inode);

	l_ctx = nfs_get_lock_context(nfs_file_open_context(filp));
	if (!IS_ERR(l_ctx)) {
		status = nfs_iocounter_wait(l_ctx);
		nfs_put_lock_context(l_ctx);
		/*  NOTE: special case
		 * 	If we're signalled while cleaning up locks on process exit, we
		 * 	still need to complete the unlock.
		 */
		if (status < 0 && !(fl->c.flc_flags & FL_CLOSE))
			return status;
	}

	/*
	 * Use local locking if mounted with "-onolock" or with appropriate
	 * "-olocal_lock="
	 */
	if (!is_local)
		status = NFS_PROTO(inode)->lock(filp, cmd, fl);
	else
		status = locks_lock_file_wait(filp, fl);
	return status;
}

static int
do_setlk(struct file *filp, int cmd, struct file_lock *fl, int is_local)
{
	struct inode *inode = filp->f_mapping->host;
	int status;

	/*
	 * Flush all pending writes before doing anything
	 * with locks..
	 */
	status = nfs_sync_mapping(filp->f_mapping);
	if (status != 0)
		goto out;

	/*
	 * Use local locking if mounted with "-onolock" or with appropriate
	 * "-olocal_lock="
	 */
	if (!is_local)
		status = NFS_PROTO(inode)->lock(filp, cmd, fl);
	else
		status = locks_lock_file_wait(filp, fl);
	if (status < 0)
		goto out;

	/*
	 * Invalidate cache to prevent missing any changes.  If
	 * the file is mapped, clear the page cache as well so
	 * those mappings will be loaded.
	 *
	 * This makes locking act as a cache coherency point.
	 */
	nfs_sync_mapping(filp->f_mapping);
	if (!NFS_PROTO(inode)->have_delegation(inode, FMODE_READ)) {
		nfs_zap_caches(inode);
		if (mapping_mapped(filp->f_mapping))
			nfs_revalidate_mapping(inode, filp->f_mapping);
	}
out:
	return status;
}

/*
 * Lock a (portion of) a file
 */
int nfs_lock(struct file *filp, int cmd, struct file_lock *fl)
{
	struct inode *inode = filp->f_mapping->host;
	int ret = -ENOLCK;
	int is_local = 0;

	dprintk("NFS: lock(%pD2, t=%x, fl=%x, r=%lld:%lld)\n",
			filp, fl->c.flc_type, fl->c.flc_flags,
			(long long)fl->fl_start, (long long)fl->fl_end);

	nfs_inc_stats(inode, NFSIOS_VFSLOCK);

	if (fl->c.flc_flags & FL_RECLAIM)
		return -ENOGRACE;

	if (NFS_SERVER(inode)->flags & NFS_MOUNT_LOCAL_FCNTL)
		is_local = 1;

	if (NFS_PROTO(inode)->lock_check_bounds != NULL) {
		ret = NFS_PROTO(inode)->lock_check_bounds(fl);
		if (ret < 0)
			goto out_err;
	}

	if (IS_GETLK(cmd))
		ret = do_getlk(filp, cmd, fl, is_local);
	else if (lock_is_unlock(fl))
		ret = do_unlk(filp, cmd, fl, is_local);
	else
		ret = do_setlk(filp, cmd, fl, is_local);
out_err:
	return ret;
}
EXPORT_SYMBOL_GPL(nfs_lock);

/*
 * Lock a (portion of) a file
 */
int nfs_flock(struct file *filp, int cmd, struct file_lock *fl)
{
	struct inode *inode = filp->f_mapping->host;
	int is_local = 0;

	dprintk("NFS: flock(%pD2, t=%x, fl=%x)\n",
			filp, fl->c.flc_type, fl->c.flc_flags);

	if (!(fl->c.flc_flags & FL_FLOCK))
		return -ENOLCK;

	if (NFS_SERVER(inode)->flags & NFS_MOUNT_LOCAL_FLOCK)
		is_local = 1;

	/* We're simulating flock() locks using posix locks on the server */
	if (lock_is_unlock(fl))
		return do_unlk(filp, cmd, fl, is_local);
	return do_setlk(filp, cmd, fl, is_local);
}
EXPORT_SYMBOL_GPL(nfs_flock);

const struct file_operations nfs_file_operations = {
	.llseek		= nfs_file_llseek,
	.read_iter	= nfs_file_read,
	.write_iter	= nfs_file_write,
	.mmap		= nfs_file_mmap,
	.open		= nfs_file_open,
	.flush		= nfs_file_flush,
	.release	= nfs_file_release,
	.fsync		= nfs_file_fsync,
	.lock		= nfs_lock,
	.flock		= nfs_flock,
	.splice_read	= nfs_file_splice_read,
	.splice_write	= iter_file_splice_write,
	.check_flags	= nfs_check_flags,
	.setlease	= simple_nosetlease,
};
EXPORT_SYMBOL_GPL(nfs_file_operations);
