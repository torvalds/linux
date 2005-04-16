/*
 * linux/fs/nfs/direct.c
 *
 * Copyright (C) 2003 by Chuck Lever <cel@netapp.com>
 *
 * High-performance uncached I/O for the Linux NFS client
 *
 * There are important applications whose performance or correctness
 * depends on uncached access to file data.  Database clusters
 * (multiple copies of the same instance running on separate hosts) 
 * implement their own cache coherency protocol that subsumes file
 * system cache protocols.  Applications that process datasets 
 * considerably larger than the client's memory do not always benefit 
 * from a local cache.  A streaming video server, for instance, has no 
 * need to cache the contents of a file.
 *
 * When an application requests uncached I/O, all read and write requests
 * are made directly to the server; data stored or fetched via these
 * requests is not cached in the Linux page cache.  The client does not
 * correct unaligned requests from applications.  All requested bytes are
 * held on permanent storage before a direct write system call returns to
 * an application.
 *
 * Solaris implements an uncached I/O facility called directio() that
 * is used for backups and sequential I/O to very large files.  Solaris
 * also supports uncaching whole NFS partitions with "-o forcedirectio,"
 * an undocumented mount option.
 *
 * Designed by Jeff Kimmel, Chuck Lever, and Trond Myklebust, with
 * help from Andrew Morton.
 *
 * 18 Dec 2001	Initial implementation for 2.4  --cel
 * 08 Jul 2002	Version for 2.4.19, with bug fixes --trondmy
 * 08 Jun 2003	Port to 2.5 APIs  --cel
 * 31 Mar 2004	Handle direct I/O without VFS support  --cel
 * 15 Sep 2004	Parallel async reads  --cel
 *
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/smp_lock.h>
#include <linux/file.h>
#include <linux/pagemap.h>
#include <linux/kref.h>

#include <linux/nfs_fs.h>
#include <linux/nfs_page.h>
#include <linux/sunrpc/clnt.h>

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/atomic.h>

#define NFSDBG_FACILITY		NFSDBG_VFS
#define MAX_DIRECTIO_SIZE	(4096UL << PAGE_SHIFT)

static kmem_cache_t *nfs_direct_cachep;

/*
 * This represents a set of asynchronous requests that we're waiting on
 */
struct nfs_direct_req {
	struct kref		kref;		/* release manager */
	struct list_head	list;		/* nfs_read_data structs */
	wait_queue_head_t	wait;		/* wait for i/o completion */
	struct page **		pages;		/* pages in our buffer */
	unsigned int		npages;		/* count of pages */
	atomic_t		complete,	/* i/os we're waiting for */
				count,		/* bytes actually processed */
				error;		/* any reported error */
};


/**
 * nfs_get_user_pages - find and set up pages underlying user's buffer
 * rw: direction (read or write)
 * user_addr: starting address of this segment of user's buffer
 * count: size of this segment
 * @pages: returned array of page struct pointers underlying user's buffer
 */
static inline int
nfs_get_user_pages(int rw, unsigned long user_addr, size_t size,
		struct page ***pages)
{
	int result = -ENOMEM;
	unsigned long page_count;
	size_t array_size;

	/* set an arbitrary limit to prevent type overflow */
	/* XXX: this can probably be as large as INT_MAX */
	if (size > MAX_DIRECTIO_SIZE) {
		*pages = NULL;
		return -EFBIG;
	}

	page_count = (user_addr + size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	page_count -= user_addr >> PAGE_SHIFT;

	array_size = (page_count * sizeof(struct page *));
	*pages = kmalloc(array_size, GFP_KERNEL);
	if (*pages) {
		down_read(&current->mm->mmap_sem);
		result = get_user_pages(current, current->mm, user_addr,
					page_count, (rw == READ), 0,
					*pages, NULL);
		up_read(&current->mm->mmap_sem);
	}
	return result;
}

/**
 * nfs_free_user_pages - tear down page struct array
 * @pages: array of page struct pointers underlying target buffer
 * @npages: number of pages in the array
 * @do_dirty: dirty the pages as we release them
 */
static void
nfs_free_user_pages(struct page **pages, int npages, int do_dirty)
{
	int i;
	for (i = 0; i < npages; i++) {
		if (do_dirty)
			set_page_dirty_lock(pages[i]);
		page_cache_release(pages[i]);
	}
	kfree(pages);
}

/**
 * nfs_direct_req_release - release  nfs_direct_req structure for direct read
 * @kref: kref object embedded in an nfs_direct_req structure
 *
 */
static void nfs_direct_req_release(struct kref *kref)
{
	struct nfs_direct_req *dreq = container_of(kref, struct nfs_direct_req, kref);
	kmem_cache_free(nfs_direct_cachep, dreq);
}

/**
 * nfs_direct_read_alloc - allocate nfs_read_data structures for direct read
 * @count: count of bytes for the read request
 * @rsize: local rsize setting
 *
 * Note we also set the number of requests we have in the dreq when we are
 * done.  This prevents races with I/O completion so we will always wait
 * until all requests have been dispatched and completed.
 */
static struct nfs_direct_req *nfs_direct_read_alloc(size_t nbytes, unsigned int rsize)
{
	struct list_head *list;
	struct nfs_direct_req *dreq;
	unsigned int reads = 0;

	dreq = kmem_cache_alloc(nfs_direct_cachep, SLAB_KERNEL);
	if (!dreq)
		return NULL;

	kref_init(&dreq->kref);
	init_waitqueue_head(&dreq->wait);
	INIT_LIST_HEAD(&dreq->list);
	atomic_set(&dreq->count, 0);
	atomic_set(&dreq->error, 0);

	list = &dreq->list;
	for(;;) {
		struct nfs_read_data *data = nfs_readdata_alloc();

		if (unlikely(!data)) {
			while (!list_empty(list)) {
				data = list_entry(list->next,
						  struct nfs_read_data, pages);
				list_del(&data->pages);
				nfs_readdata_free(data);
			}
			kref_put(&dreq->kref, nfs_direct_req_release);
			return NULL;
		}

		INIT_LIST_HEAD(&data->pages);
		list_add(&data->pages, list);

		data->req = (struct nfs_page *) dreq;
		reads++;
		if (nbytes <= rsize)
			break;
		nbytes -= rsize;
	}
	kref_get(&dreq->kref);
	atomic_set(&dreq->complete, reads);
	return dreq;
}

/**
 * nfs_direct_read_result - handle a read reply for a direct read request
 * @data: address of NFS READ operation control block
 * @status: status of this NFS READ operation
 *
 * We must hold a reference to all the pages in this direct read request
 * until the RPCs complete.  This could be long *after* we are woken up in
 * nfs_direct_read_wait (for instance, if someone hits ^C on a slow server).
 */
static void nfs_direct_read_result(struct nfs_read_data *data, int status)
{
	struct nfs_direct_req *dreq = (struct nfs_direct_req *) data->req;

	if (likely(status >= 0))
		atomic_add(data->res.count, &dreq->count);
	else
		atomic_set(&dreq->error, status);

	if (unlikely(atomic_dec_and_test(&dreq->complete))) {
		nfs_free_user_pages(dreq->pages, dreq->npages, 1);
		wake_up(&dreq->wait);
		kref_put(&dreq->kref, nfs_direct_req_release);
	}
}

/**
 * nfs_direct_read_schedule - dispatch NFS READ operations for a direct read
 * @dreq: address of nfs_direct_req struct for this request
 * @inode: target inode
 * @ctx: target file open context
 * @user_addr: starting address of this segment of user's buffer
 * @count: size of this segment
 * @file_offset: offset in file to begin the operation
 *
 * For each nfs_read_data struct that was allocated on the list, dispatch
 * an NFS READ operation
 */
static void nfs_direct_read_schedule(struct nfs_direct_req *dreq,
		struct inode *inode, struct nfs_open_context *ctx,
		unsigned long user_addr, size_t count, loff_t file_offset)
{
	struct list_head *list = &dreq->list;
	struct page **pages = dreq->pages;
	unsigned int curpage, pgbase;
	unsigned int rsize = NFS_SERVER(inode)->rsize;

	curpage = 0;
	pgbase = user_addr & ~PAGE_MASK;
	do {
		struct nfs_read_data *data;
		unsigned int bytes;

		bytes = rsize;
		if (count < rsize)
			bytes = count;

		data = list_entry(list->next, struct nfs_read_data, pages);
		list_del_init(&data->pages);

		data->inode = inode;
		data->cred = ctx->cred;
		data->args.fh = NFS_FH(inode);
		data->args.context = ctx;
		data->args.offset = file_offset;
		data->args.pgbase = pgbase;
		data->args.pages = &pages[curpage];
		data->args.count = bytes;
		data->res.fattr = &data->fattr;
		data->res.eof = 0;
		data->res.count = bytes;

		NFS_PROTO(inode)->read_setup(data);

		data->task.tk_cookie = (unsigned long) inode;
		data->task.tk_calldata = data;
		data->task.tk_release = nfs_readdata_release;
		data->complete = nfs_direct_read_result;

		lock_kernel();
		rpc_execute(&data->task);
		unlock_kernel();

		dfprintk(VFS, "NFS: %4d initiated direct read call (req %s/%Ld, %u bytes @ offset %Lu)\n",
				data->task.tk_pid,
				inode->i_sb->s_id,
				(long long)NFS_FILEID(inode),
				bytes,
				(unsigned long long)data->args.offset);

		file_offset += bytes;
		pgbase += bytes;
		curpage += pgbase >> PAGE_SHIFT;
		pgbase &= ~PAGE_MASK;

		count -= bytes;
	} while (count != 0);
}

/**
 * nfs_direct_read_wait - wait for I/O completion for direct reads
 * @dreq: request on which we are to wait
 * @intr: whether or not this wait can be interrupted
 *
 * Collects and returns the final error value/byte-count.
 */
static ssize_t nfs_direct_read_wait(struct nfs_direct_req *dreq, int intr)
{
	int result = 0;

	if (intr) {
		result = wait_event_interruptible(dreq->wait,
					(atomic_read(&dreq->complete) == 0));
	} else {
		wait_event(dreq->wait, (atomic_read(&dreq->complete) == 0));
	}

	if (!result)
		result = atomic_read(&dreq->error);
	if (!result)
		result = atomic_read(&dreq->count);

	kref_put(&dreq->kref, nfs_direct_req_release);
	return (ssize_t) result;
}

/**
 * nfs_direct_read_seg - Read in one iov segment.  Generate separate
 *                        read RPCs for each "rsize" bytes.
 * @inode: target inode
 * @ctx: target file open context
 * @user_addr: starting address of this segment of user's buffer
 * @count: size of this segment
 * @file_offset: offset in file to begin the operation
 * @pages: array of addresses of page structs defining user's buffer
 * @nr_pages: number of pages in the array
 *
 */
static ssize_t nfs_direct_read_seg(struct inode *inode,
		struct nfs_open_context *ctx, unsigned long user_addr,
		size_t count, loff_t file_offset, struct page **pages,
		unsigned int nr_pages)
{
	ssize_t result;
	sigset_t oldset;
	struct rpc_clnt *clnt = NFS_CLIENT(inode);
	struct nfs_direct_req *dreq;

	dreq = nfs_direct_read_alloc(count, NFS_SERVER(inode)->rsize);
	if (!dreq)
		return -ENOMEM;

	dreq->pages = pages;
	dreq->npages = nr_pages;

	rpc_clnt_sigmask(clnt, &oldset);
	nfs_direct_read_schedule(dreq, inode, ctx, user_addr, count,
				 file_offset);
	result = nfs_direct_read_wait(dreq, clnt->cl_intr);
	rpc_clnt_sigunmask(clnt, &oldset);

	return result;
}

/**
 * nfs_direct_read - For each iov segment, map the user's buffer
 *                   then generate read RPCs.
 * @inode: target inode
 * @ctx: target file open context
 * @iov: array of vectors that define I/O buffer
 * file_offset: offset in file to begin the operation
 * nr_segs: size of iovec array
 *
 * We've already pushed out any non-direct writes so that this read
 * will see them when we read from the server.
 */
static ssize_t
nfs_direct_read(struct inode *inode, struct nfs_open_context *ctx,
		const struct iovec *iov, loff_t file_offset,
		unsigned long nr_segs)
{
	ssize_t tot_bytes = 0;
	unsigned long seg = 0;

	while ((seg < nr_segs) && (tot_bytes >= 0)) {
		ssize_t result;
		int page_count;
		struct page **pages;
		const struct iovec *vec = &iov[seg++];
		unsigned long user_addr = (unsigned long) vec->iov_base;
		size_t size = vec->iov_len;

                page_count = nfs_get_user_pages(READ, user_addr, size, &pages);
                if (page_count < 0) {
                        nfs_free_user_pages(pages, 0, 0);
			if (tot_bytes > 0)
				break;
                        return page_count;
                }

		result = nfs_direct_read_seg(inode, ctx, user_addr, size,
				file_offset, pages, page_count);

		if (result <= 0) {
			if (tot_bytes > 0)
				break;
			return result;
		}
		tot_bytes += result;
		file_offset += result;
		if (result < size)
			break;
	}

	return tot_bytes;
}

/**
 * nfs_direct_write_seg - Write out one iov segment.  Generate separate
 *                        write RPCs for each "wsize" bytes, then commit.
 * @inode: target inode
 * @ctx: target file open context
 * user_addr: starting address of this segment of user's buffer
 * count: size of this segment
 * file_offset: offset in file to begin the operation
 * @pages: array of addresses of page structs defining user's buffer
 * nr_pages: size of pages array
 */
static ssize_t nfs_direct_write_seg(struct inode *inode,
		struct nfs_open_context *ctx, unsigned long user_addr,
		size_t count, loff_t file_offset, struct page **pages,
		int nr_pages)
{
	const unsigned int wsize = NFS_SERVER(inode)->wsize;
	size_t request;
	int curpage, need_commit;
	ssize_t result, tot_bytes;
	struct nfs_writeverf first_verf;
	struct nfs_write_data *wdata;

	wdata = nfs_writedata_alloc();
	if (!wdata)
		return -ENOMEM;

	wdata->inode = inode;
	wdata->cred = ctx->cred;
	wdata->args.fh = NFS_FH(inode);
	wdata->args.context = ctx;
	wdata->args.stable = NFS_UNSTABLE;
	if (IS_SYNC(inode) || NFS_PROTO(inode)->version == 2 || count <= wsize)
		wdata->args.stable = NFS_FILE_SYNC;
	wdata->res.fattr = &wdata->fattr;
	wdata->res.verf = &wdata->verf;

	nfs_begin_data_update(inode);
retry:
	need_commit = 0;
	tot_bytes = 0;
	curpage = 0;
	request = count;
	wdata->args.pgbase = user_addr & ~PAGE_MASK;
	wdata->args.offset = file_offset;
	do {
		wdata->args.count = request;
		if (wdata->args.count > wsize)
			wdata->args.count = wsize;
		wdata->args.pages = &pages[curpage];

		dprintk("NFS: direct write: c=%u o=%Ld ua=%lu, pb=%u, cp=%u\n",
			wdata->args.count, (long long) wdata->args.offset,
			user_addr + tot_bytes, wdata->args.pgbase, curpage);

		lock_kernel();
		result = NFS_PROTO(inode)->write(wdata);
		unlock_kernel();

		if (result <= 0) {
			if (tot_bytes > 0)
				break;
			goto out;
		}

		if (tot_bytes == 0)
			memcpy(&first_verf.verifier, &wdata->verf.verifier,
						sizeof(first_verf.verifier));
		if (wdata->verf.committed != NFS_FILE_SYNC) {
			need_commit = 1;
			if (memcmp(&first_verf.verifier, &wdata->verf.verifier,
					sizeof(first_verf.verifier)));
				goto sync_retry;
		}

		tot_bytes += result;

		/* in case of a short write: stop now, let the app recover */
		if (result < wdata->args.count)
			break;

		wdata->args.offset += result;
		wdata->args.pgbase += result;
		curpage += wdata->args.pgbase >> PAGE_SHIFT;
		wdata->args.pgbase &= ~PAGE_MASK;
		request -= result;
	} while (request != 0);

	/*
	 * Commit data written so far, even in the event of an error
	 */
	if (need_commit) {
		wdata->args.count = tot_bytes;
		wdata->args.offset = file_offset;

		lock_kernel();
		result = NFS_PROTO(inode)->commit(wdata);
		unlock_kernel();

		if (result < 0 || memcmp(&first_verf.verifier,
					 &wdata->verf.verifier,
					 sizeof(first_verf.verifier)) != 0)
			goto sync_retry;
	}
	result = tot_bytes;

out:
	nfs_end_data_update_defer(inode);
	nfs_writedata_free(wdata);
	return result;

sync_retry:
	wdata->args.stable = NFS_FILE_SYNC;
	goto retry;
}

/**
 * nfs_direct_write - For each iov segment, map the user's buffer
 *                    then generate write and commit RPCs.
 * @inode: target inode
 * @ctx: target file open context
 * @iov: array of vectors that define I/O buffer
 * file_offset: offset in file to begin the operation
 * nr_segs: size of iovec array
 *
 * Upon return, generic_file_direct_IO invalidates any cached pages
 * that non-direct readers might access, so they will pick up these
 * writes immediately.
 */
static ssize_t nfs_direct_write(struct inode *inode,
		struct nfs_open_context *ctx, const struct iovec *iov,
		loff_t file_offset, unsigned long nr_segs)
{
	ssize_t tot_bytes = 0;
	unsigned long seg = 0;

	while ((seg < nr_segs) && (tot_bytes >= 0)) {
		ssize_t result;
		int page_count;
		struct page **pages;
		const struct iovec *vec = &iov[seg++];
		unsigned long user_addr = (unsigned long) vec->iov_base;
		size_t size = vec->iov_len;

                page_count = nfs_get_user_pages(WRITE, user_addr, size, &pages);
                if (page_count < 0) {
                        nfs_free_user_pages(pages, 0, 0);
			if (tot_bytes > 0)
				break;
                        return page_count;
                }

		result = nfs_direct_write_seg(inode, ctx, user_addr, size,
				file_offset, pages, page_count);
		nfs_free_user_pages(pages, page_count, 0);

		if (result <= 0) {
			if (tot_bytes > 0)
				break;
			return result;
		}
		tot_bytes += result;
		file_offset += result;
		if (result < size)
			break;
	}
	return tot_bytes;
}

/**
 * nfs_direct_IO - NFS address space operation for direct I/O
 * rw: direction (read or write)
 * @iocb: target I/O control block
 * @iov: array of vectors that define I/O buffer
 * file_offset: offset in file to begin the operation
 * nr_segs: size of iovec array
 *
 */
ssize_t
nfs_direct_IO(int rw, struct kiocb *iocb, const struct iovec *iov,
		loff_t file_offset, unsigned long nr_segs)
{
	ssize_t result = -EINVAL;
	struct file *file = iocb->ki_filp;
	struct nfs_open_context *ctx;
	struct dentry *dentry = file->f_dentry;
	struct inode *inode = dentry->d_inode;

	/*
	 * No support for async yet
	 */
	if (!is_sync_kiocb(iocb))
		return result;

	ctx = (struct nfs_open_context *)file->private_data;
	switch (rw) {
	case READ:
		dprintk("NFS: direct_IO(read) (%s) off/no(%Lu/%lu)\n",
				dentry->d_name.name, file_offset, nr_segs);

		result = nfs_direct_read(inode, ctx, iov,
						file_offset, nr_segs);
		break;
	case WRITE:
		dprintk("NFS: direct_IO(write) (%s) off/no(%Lu/%lu)\n",
				dentry->d_name.name, file_offset, nr_segs);

		result = nfs_direct_write(inode, ctx, iov,
						file_offset, nr_segs);
		break;
	default:
		break;
	}
	return result;
}

/**
 * nfs_file_direct_read - file direct read operation for NFS files
 * @iocb: target I/O control block
 * @buf: user's buffer into which to read data
 * count: number of bytes to read
 * pos: byte offset in file where reading starts
 *
 * We use this function for direct reads instead of calling
 * generic_file_aio_read() in order to avoid gfar's check to see if
 * the request starts before the end of the file.  For that check
 * to work, we must generate a GETATTR before each direct read, and
 * even then there is a window between the GETATTR and the subsequent
 * READ where the file size could change.  So our preference is simply
 * to do all reads the application wants, and the server will take
 * care of managing the end of file boundary.
 * 
 * This function also eliminates unnecessarily updating the file's
 * atime locally, as the NFS server sets the file's atime, and this
 * client must read the updated atime from the server back into its
 * cache.
 */
ssize_t
nfs_file_direct_read(struct kiocb *iocb, char __user *buf, size_t count, loff_t pos)
{
	ssize_t retval = -EINVAL;
	loff_t *ppos = &iocb->ki_pos;
	struct file *file = iocb->ki_filp;
	struct nfs_open_context *ctx =
			(struct nfs_open_context *) file->private_data;
	struct dentry *dentry = file->f_dentry;
	struct address_space *mapping = file->f_mapping;
	struct inode *inode = mapping->host;
	struct iovec iov = {
		.iov_base = buf,
		.iov_len = count,
	};

	dprintk("nfs: direct read(%s/%s, %lu@%lu)\n",
		dentry->d_parent->d_name.name, dentry->d_name.name,
		(unsigned long) count, (unsigned long) pos);

	if (!is_sync_kiocb(iocb))
		goto out;
	if (count < 0)
		goto out;
	retval = -EFAULT;
	if (!access_ok(VERIFY_WRITE, iov.iov_base, iov.iov_len))
		goto out;
	retval = 0;
	if (!count)
		goto out;

	if (mapping->nrpages) {
		retval = filemap_fdatawrite(mapping);
		if (retval == 0)
			retval = nfs_wb_all(inode);
		if (retval == 0)
			retval = filemap_fdatawait(mapping);
		if (retval)
			goto out;
	}

	retval = nfs_direct_read(inode, ctx, &iov, pos, 1);
	if (retval > 0)
		*ppos = pos + retval;

out:
	return retval;
}

/**
 * nfs_file_direct_write - file direct write operation for NFS files
 * @iocb: target I/O control block
 * @buf: user's buffer from which to write data
 * count: number of bytes to write
 * pos: byte offset in file where writing starts
 *
 * We use this function for direct writes instead of calling
 * generic_file_aio_write() in order to avoid taking the inode
 * semaphore and updating the i_size.  The NFS server will set
 * the new i_size and this client must read the updated size
 * back into its cache.  We let the server do generic write
 * parameter checking and report problems.
 *
 * We also avoid an unnecessary invocation of generic_osync_inode(),
 * as it is fairly meaningless to sync the metadata of an NFS file.
 *
 * We eliminate local atime updates, see direct read above.
 *
 * We avoid unnecessary page cache invalidations for normal cached
 * readers of this file.
 *
 * Note that O_APPEND is not supported for NFS direct writes, as there
 * is no atomic O_APPEND write facility in the NFS protocol.
 */
ssize_t
nfs_file_direct_write(struct kiocb *iocb, const char __user *buf, size_t count, loff_t pos)
{
	ssize_t retval = -EINVAL;
	loff_t *ppos = &iocb->ki_pos;
	unsigned long limit = current->signal->rlim[RLIMIT_FSIZE].rlim_cur;
	struct file *file = iocb->ki_filp;
	struct nfs_open_context *ctx =
			(struct nfs_open_context *) file->private_data;
	struct dentry *dentry = file->f_dentry;
	struct address_space *mapping = file->f_mapping;
	struct inode *inode = mapping->host;
	struct iovec iov = {
		.iov_base = (char __user *)buf,
		.iov_len = count,
	};

	dfprintk(VFS, "nfs: direct write(%s/%s(%ld), %lu@%lu)\n",
		dentry->d_parent->d_name.name, dentry->d_name.name,
		inode->i_ino, (unsigned long) count, (unsigned long) pos);

	if (!is_sync_kiocb(iocb))
		goto out;
	if (count < 0)
		goto out;
        if (pos < 0)
		goto out;
	retval = -EFAULT;
	if (!access_ok(VERIFY_READ, iov.iov_base, iov.iov_len))
		goto out;
        if (file->f_error) {
                retval = file->f_error;
                file->f_error = 0;
                goto out;
        }
	retval = -EFBIG;
	if (limit != RLIM_INFINITY) {
		if (pos >= limit) {
			send_sig(SIGXFSZ, current, 0);
			goto out;
		}
		if (count > limit - (unsigned long) pos)
			count = limit - (unsigned long) pos;
	}
	retval = 0;
	if (!count)
		goto out;

	if (mapping->nrpages) {
		retval = filemap_fdatawrite(mapping);
		if (retval == 0)
			retval = nfs_wb_all(inode);
		if (retval == 0)
			retval = filemap_fdatawait(mapping);
		if (retval)
			goto out;
	}

	retval = nfs_direct_write(inode, ctx, &iov, pos, 1);
	if (mapping->nrpages)
		invalidate_inode_pages2(mapping);
	if (retval > 0)
		*ppos = pos + retval;

out:
	return retval;
}

int nfs_init_directcache(void)
{
	nfs_direct_cachep = kmem_cache_create("nfs_direct_cache",
						sizeof(struct nfs_direct_req),
						0, SLAB_RECLAIM_ACCOUNT,
						NULL, NULL);
	if (nfs_direct_cachep == NULL)
		return -ENOMEM;

	return 0;
}

void nfs_destroy_directcache(void)
{
	if (kmem_cache_destroy(nfs_direct_cachep))
		printk(KERN_INFO "nfs_direct_cache: not all structures were freed\n");
}
