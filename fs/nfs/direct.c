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
 * 04 May 2005	support O_DIRECT with aio  --cel
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

#include "iostat.h"

#define NFSDBG_FACILITY		NFSDBG_VFS

static void nfs_free_user_pages(struct page **pages, int npages, int do_dirty);
static kmem_cache_t *nfs_direct_cachep;

/*
 * This represents a set of asynchronous requests that we're waiting on
 */
struct nfs_direct_req {
	struct kref		kref;		/* release manager */

	/* I/O parameters */
	struct list_head	list;		/* nfs_read/write_data structs */
	struct file *		filp;		/* file descriptor */
	struct kiocb *		iocb;		/* controlling i/o request */
	wait_queue_head_t	wait;		/* wait for i/o completion */
	struct inode *		inode;		/* target file of i/o */
	struct page **		pages;		/* pages in our buffer */
	unsigned int		npages;		/* count of pages */

	/* completion state */
	spinlock_t		lock;		/* protect completion state */
	int			outstanding;	/* i/os we're waiting for */
	ssize_t			count,		/* bytes actually processed */
				error;		/* any reported error */
};

/**
 * nfs_direct_IO - NFS address space operation for direct I/O
 * @rw: direction (read or write)
 * @iocb: target I/O control block
 * @iov: array of vectors that define I/O buffer
 * @pos: offset in file to begin the operation
 * @nr_segs: size of iovec array
 *
 * The presence of this routine in the address space ops vector means
 * the NFS client supports direct I/O.  However, we shunt off direct
 * read and write requests before the VFS gets them, so this method
 * should never be called.
 */
ssize_t nfs_direct_IO(int rw, struct kiocb *iocb, const struct iovec *iov, loff_t pos, unsigned long nr_segs)
{
	struct dentry *dentry = iocb->ki_filp->f_dentry;

	dprintk("NFS: nfs_direct_IO (%s) off/no(%Ld/%lu) EINVAL\n",
			dentry->d_name.name, (long long) pos, nr_segs);

	return -EINVAL;
}

static inline int nfs_get_user_pages(int rw, unsigned long user_addr, size_t size, struct page ***pages)
{
	int result = -ENOMEM;
	unsigned long page_count;
	size_t array_size;

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
		/*
		 * If we got fewer pages than expected from get_user_pages(),
		 * the user buffer runs off the end of a mapping; return EFAULT.
		 */
		if (result >= 0 && result < page_count) {
			nfs_free_user_pages(*pages, result, 0);
			*pages = NULL;
			result = -EFAULT;
		}
	}
	return result;
}

static void nfs_free_user_pages(struct page **pages, int npages, int do_dirty)
{
	int i;
	for (i = 0; i < npages; i++) {
		struct page *page = pages[i];
		if (do_dirty && !PageCompound(page))
			set_page_dirty_lock(page);
		page_cache_release(page);
	}
	kfree(pages);
}

static inline struct nfs_direct_req *nfs_direct_req_alloc(void)
{
	struct nfs_direct_req *dreq;

	dreq = kmem_cache_alloc(nfs_direct_cachep, SLAB_KERNEL);
	if (!dreq)
		return NULL;

	kref_init(&dreq->kref);
	init_waitqueue_head(&dreq->wait);
	INIT_LIST_HEAD(&dreq->list);
	dreq->iocb = NULL;
	spin_lock_init(&dreq->lock);
	dreq->outstanding = 0;
	dreq->count = 0;
	dreq->error = 0;

	return dreq;
}

static void nfs_direct_req_release(struct kref *kref)
{
	struct nfs_direct_req *dreq = container_of(kref, struct nfs_direct_req, kref);
	kmem_cache_free(nfs_direct_cachep, dreq);
}

/*
 * Collects and returns the final error value/byte-count.
 */
static ssize_t nfs_direct_wait(struct nfs_direct_req *dreq)
{
	ssize_t result = -EIOCBQUEUED;

	/* Async requests don't wait here */
	if (dreq->iocb)
		goto out;

	result = wait_event_interruptible(dreq->wait, (dreq->outstanding == 0));

	if (!result)
		result = dreq->error;
	if (!result)
		result = dreq->count;

out:
	kref_put(&dreq->kref, nfs_direct_req_release);
	return (ssize_t) result;
}

/*
 * We must hold a reference to all the pages in this direct read request
 * until the RPCs complete.  This could be long *after* we are woken up in
 * nfs_direct_wait (for instance, if someone hits ^C on a slow server).
 *
 * In addition, synchronous I/O uses a stack-allocated iocb.  Thus we
 * can't trust the iocb is still valid here if this is a synchronous
 * request.  If the waiter is woken prematurely, the iocb is long gone.
 */
static void nfs_direct_complete(struct nfs_direct_req *dreq)
{
	nfs_free_user_pages(dreq->pages, dreq->npages, 1);

	if (dreq->iocb) {
		long res = (long) dreq->error;
		if (!res)
			res = (long) dreq->count;
		aio_complete(dreq->iocb, res, 0);
	} else
		wake_up(&dreq->wait);

	iput(dreq->inode);
	kref_put(&dreq->kref, nfs_direct_req_release);
}

/*
 * Note we also set the number of requests we have in the dreq when we are
 * done.  This prevents races with I/O completion so we will always wait
 * until all requests have been dispatched and completed.
 */
static struct nfs_direct_req *nfs_direct_read_alloc(size_t nbytes, size_t rsize)
{
	struct list_head *list;
	struct nfs_direct_req *dreq;
	unsigned int rpages = (rsize + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;

	dreq = nfs_direct_req_alloc();
	if (!dreq)
		return NULL;

	list = &dreq->list;
	for(;;) {
		struct nfs_read_data *data = nfs_readdata_alloc(rpages);

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
		dreq->outstanding++;
		if (nbytes <= rsize)
			break;
		nbytes -= rsize;
	}
	kref_get(&dreq->kref);
	return dreq;
}

static void nfs_direct_read_result(struct rpc_task *task, void *calldata)
{
	struct nfs_read_data *data = calldata;
	struct nfs_direct_req *dreq = (struct nfs_direct_req *) data->req;

	if (nfs_readpage_result(task, data) != 0)
		return;

	spin_lock(&dreq->lock);

	if (likely(task->tk_status >= 0))
		dreq->count += data->res.count;
	else
		dreq->error = task->tk_status;

	if (--dreq->outstanding) {
		spin_unlock(&dreq->lock);
		return;
	}

	spin_unlock(&dreq->lock);
	nfs_direct_complete(dreq);
}

static const struct rpc_call_ops nfs_read_direct_ops = {
	.rpc_call_done = nfs_direct_read_result,
	.rpc_release = nfs_readdata_release,
};

/*
 * For each nfs_read_data struct that was allocated on the list, dispatch
 * an NFS READ operation
 */
static void nfs_direct_read_schedule(struct nfs_direct_req *dreq, unsigned long user_addr, size_t count, loff_t pos)
{
	struct file *file = dreq->filp;
	struct inode *inode = file->f_mapping->host;
	struct nfs_open_context *ctx = (struct nfs_open_context *)
							file->private_data;
	struct list_head *list = &dreq->list;
	struct page **pages = dreq->pages;
	size_t rsize = NFS_SERVER(inode)->rsize;
	unsigned int curpage, pgbase;

	curpage = 0;
	pgbase = user_addr & ~PAGE_MASK;
	do {
		struct nfs_read_data *data;
		size_t bytes;

		bytes = rsize;
		if (count < rsize)
			bytes = count;

		data = list_entry(list->next, struct nfs_read_data, pages);
		list_del_init(&data->pages);

		data->inode = inode;
		data->cred = ctx->cred;
		data->args.fh = NFS_FH(inode);
		data->args.context = ctx;
		data->args.offset = pos;
		data->args.pgbase = pgbase;
		data->args.pages = &pages[curpage];
		data->args.count = bytes;
		data->res.fattr = &data->fattr;
		data->res.eof = 0;
		data->res.count = bytes;

		rpc_init_task(&data->task, NFS_CLIENT(inode), RPC_TASK_ASYNC,
				&nfs_read_direct_ops, data);
		NFS_PROTO(inode)->read_setup(data);

		data->task.tk_cookie = (unsigned long) inode;

		lock_kernel();
		rpc_execute(&data->task);
		unlock_kernel();

		dfprintk(VFS, "NFS: %4d initiated direct read call (req %s/%Ld, %u bytes @ offset %Lu)\n",
				data->task.tk_pid,
				inode->i_sb->s_id,
				(long long)NFS_FILEID(inode),
				bytes,
				(unsigned long long)data->args.offset);

		pos += bytes;
		pgbase += bytes;
		curpage += pgbase >> PAGE_SHIFT;
		pgbase &= ~PAGE_MASK;

		count -= bytes;
	} while (count != 0);
}

static ssize_t nfs_direct_read(struct kiocb *iocb, unsigned long user_addr, size_t count, loff_t pos, struct page **pages, unsigned int nr_pages)
{
	ssize_t result;
	sigset_t oldset;
	struct inode *inode = iocb->ki_filp->f_mapping->host;
	struct rpc_clnt *clnt = NFS_CLIENT(inode);
	struct nfs_direct_req *dreq;

	dreq = nfs_direct_read_alloc(count, NFS_SERVER(inode)->rsize);
	if (!dreq)
		return -ENOMEM;

	dreq->pages = pages;
	dreq->npages = nr_pages;
	igrab(inode);
	dreq->inode = inode;
	dreq->filp = iocb->ki_filp;
	if (!is_sync_kiocb(iocb))
		dreq->iocb = iocb;

	nfs_add_stats(inode, NFSIOS_DIRECTREADBYTES, count);
	rpc_clnt_sigmask(clnt, &oldset);
	nfs_direct_read_schedule(dreq, user_addr, count, pos);
	result = nfs_direct_wait(dreq);
	rpc_clnt_sigunmask(clnt, &oldset);

	return result;
}

static struct nfs_direct_req *nfs_direct_write_alloc(size_t nbytes, size_t wsize)
{
	struct list_head *list;
	struct nfs_direct_req *dreq;
	unsigned int wpages = (wsize + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;

	dreq = nfs_direct_req_alloc();
	if (!dreq)
		return NULL;

	list = &dreq->list;
	for(;;) {
		struct nfs_write_data *data = nfs_writedata_alloc(wpages);

		if (unlikely(!data)) {
			while (!list_empty(list)) {
				data = list_entry(list->next,
						  struct nfs_write_data, pages);
				list_del(&data->pages);
				nfs_writedata_free(data);
			}
			kref_put(&dreq->kref, nfs_direct_req_release);
			return NULL;
		}

		INIT_LIST_HEAD(&data->pages);
		list_add(&data->pages, list);

		data->req = (struct nfs_page *) dreq;
		dreq->outstanding++;
		if (nbytes <= wsize)
			break;
		nbytes -= wsize;
	}
	kref_get(&dreq->kref);
	return dreq;
}

/*
 * NB: Return the value of the first error return code.  Subsequent
 *     errors after the first one are ignored.
 */
static void nfs_direct_write_result(struct rpc_task *task, void *calldata)
{
	struct nfs_write_data *data = calldata;
	struct nfs_direct_req *dreq = (struct nfs_direct_req *) data->req;
	int status = task->tk_status;

	if (nfs_writeback_done(task, data) != 0)
		return;
	/* If the server fell back to an UNSTABLE write, it's an error. */
	if (unlikely(data->res.verf->committed != NFS_FILE_SYNC))
		status = -EIO;

	spin_lock(&dreq->lock);

	if (likely(status >= 0))
		dreq->count += data->res.count;
	else
		dreq->error = status;

	if (--dreq->outstanding) {
		spin_unlock(&dreq->lock);
		return;
	}

	spin_unlock(&dreq->lock);

	nfs_end_data_update(data->inode);
	nfs_direct_complete(dreq);
}

static const struct rpc_call_ops nfs_write_direct_ops = {
	.rpc_call_done = nfs_direct_write_result,
	.rpc_release = nfs_writedata_release,
};

/*
 * For each nfs_write_data struct that was allocated on the list, dispatch
 * an NFS WRITE operation
 *
 * XXX: For now, support only FILE_SYNC writes.  Later we may add
 *      support for UNSTABLE + COMMIT.
 */
static void nfs_direct_write_schedule(struct nfs_direct_req *dreq, unsigned long user_addr, size_t count, loff_t pos)
{
	struct file *file = dreq->filp;
	struct inode *inode = file->f_mapping->host;
	struct nfs_open_context *ctx = (struct nfs_open_context *)
							file->private_data;
	struct list_head *list = &dreq->list;
	struct page **pages = dreq->pages;
	size_t wsize = NFS_SERVER(inode)->wsize;
	unsigned int curpage, pgbase;

	curpage = 0;
	pgbase = user_addr & ~PAGE_MASK;
	do {
		struct nfs_write_data *data;
		size_t bytes;

		bytes = wsize;
		if (count < wsize)
			bytes = count;

		data = list_entry(list->next, struct nfs_write_data, pages);
		list_del_init(&data->pages);

		data->inode = inode;
		data->cred = ctx->cred;
		data->args.fh = NFS_FH(inode);
		data->args.context = ctx;
		data->args.offset = pos;
		data->args.pgbase = pgbase;
		data->args.pages = &pages[curpage];
		data->args.count = bytes;
		data->res.fattr = &data->fattr;
		data->res.count = bytes;
		data->res.verf = &data->verf;

		rpc_init_task(&data->task, NFS_CLIENT(inode), RPC_TASK_ASYNC,
				&nfs_write_direct_ops, data);
		NFS_PROTO(inode)->write_setup(data, FLUSH_STABLE);

		data->task.tk_priority = RPC_PRIORITY_NORMAL;
		data->task.tk_cookie = (unsigned long) inode;

		lock_kernel();
		rpc_execute(&data->task);
		unlock_kernel();

		dfprintk(VFS, "NFS: %4d initiated direct write call (req %s/%Ld, %u bytes @ offset %Lu)\n",
				data->task.tk_pid,
				inode->i_sb->s_id,
				(long long)NFS_FILEID(inode),
				bytes,
				(unsigned long long)data->args.offset);

		pos += bytes;
		pgbase += bytes;
		curpage += pgbase >> PAGE_SHIFT;
		pgbase &= ~PAGE_MASK;

		count -= bytes;
	} while (count != 0);
}

static ssize_t nfs_direct_write(struct kiocb *iocb, unsigned long user_addr, size_t count, loff_t pos, struct page **pages, int nr_pages)
{
	ssize_t result;
	sigset_t oldset;
	struct inode *inode = iocb->ki_filp->f_mapping->host;
	struct rpc_clnt *clnt = NFS_CLIENT(inode);
	struct nfs_direct_req *dreq;

	dreq = nfs_direct_write_alloc(count, NFS_SERVER(inode)->wsize);
	if (!dreq)
		return -ENOMEM;

	dreq->pages = pages;
	dreq->npages = nr_pages;
	igrab(inode);
	dreq->inode = inode;
	dreq->filp = iocb->ki_filp;
	if (!is_sync_kiocb(iocb))
		dreq->iocb = iocb;

	nfs_add_stats(inode, NFSIOS_DIRECTWRITTENBYTES, count);

	nfs_begin_data_update(inode);

	rpc_clnt_sigmask(clnt, &oldset);
	nfs_direct_write_schedule(dreq, user_addr, count, pos);
	result = nfs_direct_wait(dreq);
	rpc_clnt_sigunmask(clnt, &oldset);

	return result;
}

/**
 * nfs_file_direct_read - file direct read operation for NFS files
 * @iocb: target I/O control block
 * @buf: user's buffer into which to read data
 * @count: number of bytes to read
 * @pos: byte offset in file where reading starts
 *
 * We use this function for direct reads instead of calling
 * generic_file_aio_read() in order to avoid gfar's check to see if
 * the request starts before the end of the file.  For that check
 * to work, we must generate a GETATTR before each direct read, and
 * even then there is a window between the GETATTR and the subsequent
 * READ where the file size could change.  Our preference is simply
 * to do all reads the application wants, and the server will take
 * care of managing the end of file boundary.
 *
 * This function also eliminates unnecessarily updating the file's
 * atime locally, as the NFS server sets the file's atime, and this
 * client must read the updated atime from the server back into its
 * cache.
 */
ssize_t nfs_file_direct_read(struct kiocb *iocb, char __user *buf, size_t count, loff_t pos)
{
	ssize_t retval = -EINVAL;
	int page_count;
	struct page **pages;
	struct file *file = iocb->ki_filp;
	struct address_space *mapping = file->f_mapping;

	dprintk("nfs: direct read(%s/%s, %lu@%Ld)\n",
		file->f_dentry->d_parent->d_name.name,
		file->f_dentry->d_name.name,
		(unsigned long) count, (long long) pos);

	if (count < 0)
		goto out;
	retval = -EFAULT;
	if (!access_ok(VERIFY_WRITE, buf, count))
		goto out;
	retval = 0;
	if (!count)
		goto out;

	retval = nfs_sync_mapping(mapping);
	if (retval)
		goto out;

	page_count = nfs_get_user_pages(READ, (unsigned long) buf,
						count, &pages);
	if (page_count < 0) {
		nfs_free_user_pages(pages, 0, 0);
		retval = page_count;
		goto out;
	}

	retval = nfs_direct_read(iocb, (unsigned long) buf, count, pos,
						pages, page_count);
	if (retval > 0)
		iocb->ki_pos = pos + retval;

out:
	return retval;
}

/**
 * nfs_file_direct_write - file direct write operation for NFS files
 * @iocb: target I/O control block
 * @buf: user's buffer from which to write data
 * @count: number of bytes to write
 * @pos: byte offset in file where writing starts
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
ssize_t nfs_file_direct_write(struct kiocb *iocb, const char __user *buf, size_t count, loff_t pos)
{
	ssize_t retval;
	int page_count;
	struct page **pages;
	struct file *file = iocb->ki_filp;
	struct address_space *mapping = file->f_mapping;

	dfprintk(VFS, "nfs: direct write(%s/%s, %lu@%Ld)\n",
		file->f_dentry->d_parent->d_name.name,
		file->f_dentry->d_name.name,
		(unsigned long) count, (long long) pos);

	retval = generic_write_checks(file, &pos, &count, 0);
	if (retval)
		goto out;

	retval = -EINVAL;
	if ((ssize_t) count < 0)
		goto out;
	retval = 0;
	if (!count)
		goto out;

	retval = -EFAULT;
	if (!access_ok(VERIFY_READ, buf, count))
		goto out;

	retval = nfs_sync_mapping(mapping);
	if (retval)
		goto out;

	page_count = nfs_get_user_pages(WRITE, (unsigned long) buf,
						count, &pages);
	if (page_count < 0) {
		nfs_free_user_pages(pages, 0, 0);
		retval = page_count;
		goto out;
	}

	retval = nfs_direct_write(iocb, (unsigned long) buf, count,
					pos, pages, page_count);

	/*
	 * XXX: nfs_end_data_update() already ensures this file's
	 *      cached data is subsequently invalidated.  Do we really
	 *      need to call invalidate_inode_pages2() again here?
	 *
	 *      For aio writes, this invalidation will almost certainly
	 *      occur before the writes complete.  Kind of racey.
	 */
	if (mapping->nrpages)
		invalidate_inode_pages2(mapping);

	if (retval > 0)
		iocb->ki_pos = pos + retval;

out:
	return retval;
}

/**
 * nfs_init_directcache - create a slab cache for nfs_direct_req structures
 *
 */
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

/**
 * nfs_init_directcache - destroy the slab cache for nfs_direct_req structures
 *
 */
void nfs_destroy_directcache(void)
{
	if (kmem_cache_destroy(nfs_direct_cachep))
		printk(KERN_INFO "nfs_direct_cache: not all structures were freed\n");
}
