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

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/file.h>
#include <linux/pagemap.h>
#include <linux/kref.h>
#include <linux/slab.h>
#include <linux/task_io_accounting_ops.h>
#include <linux/module.h>

#include <linux/nfs_fs.h>
#include <linux/nfs_page.h>
#include <linux/sunrpc/clnt.h>

#include <asm/uaccess.h>
#include <linux/atomic.h>

#include "internal.h"
#include "iostat.h"
#include "pnfs.h"

#define NFSDBG_FACILITY		NFSDBG_VFS

static struct kmem_cache *nfs_direct_cachep;

/*
 * This represents a set of asynchronous requests that we're waiting on
 */
struct nfs_direct_req {
	struct kref		kref;		/* release manager */

	/* I/O parameters */
	struct nfs_open_context	*ctx;		/* file open context info */
	struct nfs_lock_context *l_ctx;		/* Lock context info */
	struct kiocb *		iocb;		/* controlling i/o request */
	struct inode *		inode;		/* target file of i/o */

	/* completion state */
	atomic_t		io_count;	/* i/os we're waiting for */
	spinlock_t		lock;		/* protect completion state */
	ssize_t			count,		/* bytes actually processed */
				bytes_left,	/* bytes left to be sent */
				error;		/* any reported error */
	struct completion	completion;	/* wait for i/o completion */

	/* commit state */
	struct nfs_mds_commit_info mds_cinfo;	/* Storage for cinfo */
	struct pnfs_ds_commit_info ds_cinfo;	/* Storage for cinfo */
	struct work_struct	work;
	int			flags;
#define NFS_ODIRECT_DO_COMMIT		(1)	/* an unstable reply was received */
#define NFS_ODIRECT_RESCHED_WRITES	(2)	/* write verification failed */
	struct nfs_writeverf	verf;		/* unstable write verifier */
};

static const struct nfs_pgio_completion_ops nfs_direct_write_completion_ops;
static const struct nfs_commit_completion_ops nfs_direct_commit_completion_ops;
static void nfs_direct_write_complete(struct nfs_direct_req *dreq, struct inode *inode);
static void nfs_direct_write_schedule_work(struct work_struct *work);

static inline void get_dreq(struct nfs_direct_req *dreq)
{
	atomic_inc(&dreq->io_count);
}

static inline int put_dreq(struct nfs_direct_req *dreq)
{
	return atomic_dec_and_test(&dreq->io_count);
}

/**
 * nfs_direct_IO - NFS address space operation for direct I/O
 * @rw: direction (read or write)
 * @iocb: target I/O control block
 * @iov: array of vectors that define I/O buffer
 * @pos: offset in file to begin the operation
 * @nr_segs: size of iovec array
 *
 * The presence of this routine in the address space ops vector means
 * the NFS client supports direct I/O. However, for most direct IO, we
 * shunt off direct read and write requests before the VFS gets them,
 * so this method is only ever called for swap.
 */
ssize_t nfs_direct_IO(int rw, struct kiocb *iocb, const struct iovec *iov, loff_t pos, unsigned long nr_segs)
{
#ifndef CONFIG_NFS_SWAP
	dprintk("NFS: nfs_direct_IO (%s) off/no(%Ld/%lu) EINVAL\n",
			iocb->ki_filp->f_path.dentry->d_name.name,
			(long long) pos, nr_segs);

	return -EINVAL;
#else
	VM_BUG_ON(iocb->ki_nbytes != PAGE_SIZE);

	if (rw == READ || rw == KERNEL_READ)
		return nfs_file_direct_read(iocb, iov, nr_segs, pos,
				rw == READ ? true : false);
	return nfs_file_direct_write(iocb, iov, nr_segs, pos,
				rw == WRITE ? true : false);
#endif /* CONFIG_NFS_SWAP */
}

static void nfs_direct_release_pages(struct page **pages, unsigned int npages)
{
	unsigned int i;
	for (i = 0; i < npages; i++)
		page_cache_release(pages[i]);
}

void nfs_init_cinfo_from_dreq(struct nfs_commit_info *cinfo,
			      struct nfs_direct_req *dreq)
{
	cinfo->lock = &dreq->lock;
	cinfo->mds = &dreq->mds_cinfo;
	cinfo->ds = &dreq->ds_cinfo;
	cinfo->dreq = dreq;
	cinfo->completion_ops = &nfs_direct_commit_completion_ops;
}

static inline struct nfs_direct_req *nfs_direct_req_alloc(void)
{
	struct nfs_direct_req *dreq;

	dreq = kmem_cache_zalloc(nfs_direct_cachep, GFP_KERNEL);
	if (!dreq)
		return NULL;

	kref_init(&dreq->kref);
	kref_get(&dreq->kref);
	init_completion(&dreq->completion);
	INIT_LIST_HEAD(&dreq->mds_cinfo.list);
	INIT_WORK(&dreq->work, nfs_direct_write_schedule_work);
	spin_lock_init(&dreq->lock);

	return dreq;
}

static void nfs_direct_req_free(struct kref *kref)
{
	struct nfs_direct_req *dreq = container_of(kref, struct nfs_direct_req, kref);

	if (dreq->l_ctx != NULL)
		nfs_put_lock_context(dreq->l_ctx);
	if (dreq->ctx != NULL)
		put_nfs_open_context(dreq->ctx);
	kmem_cache_free(nfs_direct_cachep, dreq);
}

static void nfs_direct_req_release(struct nfs_direct_req *dreq)
{
	kref_put(&dreq->kref, nfs_direct_req_free);
}

ssize_t nfs_dreq_bytes_left(struct nfs_direct_req *dreq)
{
	return dreq->bytes_left;
}
EXPORT_SYMBOL_GPL(nfs_dreq_bytes_left);

/*
 * Collects and returns the final error value/byte-count.
 */
static ssize_t nfs_direct_wait(struct nfs_direct_req *dreq)
{
	ssize_t result = -EIOCBQUEUED;

	/* Async requests don't wait here */
	if (dreq->iocb)
		goto out;

	result = wait_for_completion_killable(&dreq->completion);

	if (!result)
		result = dreq->error;
	if (!result)
		result = dreq->count;

out:
	return (ssize_t) result;
}

/*
 * Synchronous I/O uses a stack-allocated iocb.  Thus we can't trust
 * the iocb is still valid here if this is a synchronous request.
 */
static void nfs_direct_complete(struct nfs_direct_req *dreq)
{
	if (dreq->iocb) {
		long res = (long) dreq->error;
		if (!res)
			res = (long) dreq->count;
		aio_complete(dreq->iocb, res, 0);
	}
	complete_all(&dreq->completion);

	nfs_direct_req_release(dreq);
}

static void nfs_direct_readpage_release(struct nfs_page *req)
{
	dprintk("NFS: direct read done (%s/%lld %d@%lld)\n",
		req->wb_context->dentry->d_inode->i_sb->s_id,
		(long long)NFS_FILEID(req->wb_context->dentry->d_inode),
		req->wb_bytes,
		(long long)req_offset(req));
	nfs_release_request(req);
}

static void nfs_direct_read_completion(struct nfs_pgio_header *hdr)
{
	unsigned long bytes = 0;
	struct nfs_direct_req *dreq = hdr->dreq;

	if (test_bit(NFS_IOHDR_REDO, &hdr->flags))
		goto out_put;

	spin_lock(&dreq->lock);
	if (test_bit(NFS_IOHDR_ERROR, &hdr->flags) && (hdr->good_bytes == 0))
		dreq->error = hdr->error;
	else
		dreq->count += hdr->good_bytes;
	spin_unlock(&dreq->lock);

	while (!list_empty(&hdr->pages)) {
		struct nfs_page *req = nfs_list_entry(hdr->pages.next);
		struct page *page = req->wb_page;

		if (!PageCompound(page) && bytes < hdr->good_bytes)
			set_page_dirty(page);
		bytes += req->wb_bytes;
		nfs_list_remove_request(req);
		nfs_direct_readpage_release(req);
	}
out_put:
	if (put_dreq(dreq))
		nfs_direct_complete(dreq);
	hdr->release(hdr);
}

static void nfs_read_sync_pgio_error(struct list_head *head)
{
	struct nfs_page *req;

	while (!list_empty(head)) {
		req = nfs_list_entry(head->next);
		nfs_list_remove_request(req);
		nfs_release_request(req);
	}
}

static void nfs_direct_pgio_init(struct nfs_pgio_header *hdr)
{
	get_dreq(hdr->dreq);
}

static const struct nfs_pgio_completion_ops nfs_direct_read_completion_ops = {
	.error_cleanup = nfs_read_sync_pgio_error,
	.init_hdr = nfs_direct_pgio_init,
	.completion = nfs_direct_read_completion,
};

/*
 * For each rsize'd chunk of the user's buffer, dispatch an NFS READ
 * operation.  If nfs_readdata_alloc() or get_user_pages() fails,
 * bail and stop sending more reads.  Read length accounting is
 * handled automatically by nfs_direct_read_result().  Otherwise, if
 * no requests have been sent, just return an error.
 */
static ssize_t nfs_direct_read_schedule_segment(struct nfs_pageio_descriptor *desc,
						const struct iovec *iov,
						loff_t pos, bool uio)
{
	struct nfs_direct_req *dreq = desc->pg_dreq;
	struct nfs_open_context *ctx = dreq->ctx;
	struct inode *inode = ctx->dentry->d_inode;
	unsigned long user_addr = (unsigned long)iov->iov_base;
	size_t count = iov->iov_len;
	size_t rsize = NFS_SERVER(inode)->rsize;
	unsigned int pgbase;
	int result;
	ssize_t started = 0;
	struct page **pagevec = NULL;
	unsigned int npages;

	do {
		size_t bytes;
		int i;

		pgbase = user_addr & ~PAGE_MASK;
		bytes = min(max_t(size_t, rsize, PAGE_SIZE), count);

		result = -ENOMEM;
		npages = nfs_page_array_len(pgbase, bytes);
		if (!pagevec)
			pagevec = kmalloc(npages * sizeof(struct page *),
					  GFP_KERNEL);
		if (!pagevec)
			break;
		if (uio) {
			down_read(&current->mm->mmap_sem);
			result = get_user_pages(current, current->mm, user_addr,
					npages, 1, 0, pagevec, NULL);
			up_read(&current->mm->mmap_sem);
			if (result < 0)
				break;
		} else {
			WARN_ON(npages != 1);
			result = get_kernel_page(user_addr, 1, pagevec);
			if (WARN_ON(result != 1))
				break;
		}

		if ((unsigned)result < npages) {
			bytes = result * PAGE_SIZE;
			if (bytes <= pgbase) {
				nfs_direct_release_pages(pagevec, result);
				break;
			}
			bytes -= pgbase;
			npages = result;
		}

		for (i = 0; i < npages; i++) {
			struct nfs_page *req;
			unsigned int req_len = min_t(size_t, bytes, PAGE_SIZE - pgbase);
			/* XXX do we need to do the eof zeroing found in async_filler? */
			req = nfs_create_request(dreq->ctx, dreq->inode,
						 pagevec[i],
						 pgbase, req_len);
			if (IS_ERR(req)) {
				result = PTR_ERR(req);
				break;
			}
			req->wb_index = pos >> PAGE_SHIFT;
			req->wb_offset = pos & ~PAGE_MASK;
			if (!nfs_pageio_add_request(desc, req)) {
				result = desc->pg_error;
				nfs_release_request(req);
				break;
			}
			pgbase = 0;
			bytes -= req_len;
			started += req_len;
			user_addr += req_len;
			pos += req_len;
			count -= req_len;
			dreq->bytes_left -= req_len;
		}
		/* The nfs_page now hold references to these pages */
		nfs_direct_release_pages(pagevec, npages);
	} while (count != 0 && result >= 0);

	kfree(pagevec);

	if (started)
		return started;
	return result < 0 ? (ssize_t) result : -EFAULT;
}

static ssize_t nfs_direct_read_schedule_iovec(struct nfs_direct_req *dreq,
					      const struct iovec *iov,
					      unsigned long nr_segs,
					      loff_t pos, bool uio)
{
	struct nfs_pageio_descriptor desc;
	ssize_t result = -EINVAL;
	size_t requested_bytes = 0;
	unsigned long seg;

	NFS_PROTO(dreq->inode)->read_pageio_init(&desc, dreq->inode,
			     &nfs_direct_read_completion_ops);
	get_dreq(dreq);
	desc.pg_dreq = dreq;

	for (seg = 0; seg < nr_segs; seg++) {
		const struct iovec *vec = &iov[seg];
		result = nfs_direct_read_schedule_segment(&desc, vec, pos, uio);
		if (result < 0)
			break;
		requested_bytes += result;
		if ((size_t)result < vec->iov_len)
			break;
		pos += vec->iov_len;
	}

	nfs_pageio_complete(&desc);

	/*
	 * If no bytes were started, return the error, and let the
	 * generic layer handle the completion.
	 */
	if (requested_bytes == 0) {
		nfs_direct_req_release(dreq);
		return result < 0 ? result : -EIO;
	}

	if (put_dreq(dreq))
		nfs_direct_complete(dreq);
	return 0;
}

static ssize_t nfs_direct_read(struct kiocb *iocb, const struct iovec *iov,
			       unsigned long nr_segs, loff_t pos, bool uio)
{
	ssize_t result = -ENOMEM;
	struct inode *inode = iocb->ki_filp->f_mapping->host;
	struct nfs_direct_req *dreq;
	struct nfs_lock_context *l_ctx;

	dreq = nfs_direct_req_alloc();
	if (dreq == NULL)
		goto out;

	dreq->inode = inode;
	dreq->bytes_left = iov_length(iov, nr_segs);
	dreq->ctx = get_nfs_open_context(nfs_file_open_context(iocb->ki_filp));
	l_ctx = nfs_get_lock_context(dreq->ctx);
	if (IS_ERR(l_ctx)) {
		result = PTR_ERR(l_ctx);
		goto out_release;
	}
	dreq->l_ctx = l_ctx;
	if (!is_sync_kiocb(iocb))
		dreq->iocb = iocb;

	NFS_I(inode)->read_io += iov_length(iov, nr_segs);
	result = nfs_direct_read_schedule_iovec(dreq, iov, nr_segs, pos, uio);
	if (!result)
		result = nfs_direct_wait(dreq);
out_release:
	nfs_direct_req_release(dreq);
out:
	return result;
}

static void nfs_inode_dio_write_done(struct inode *inode)
{
	nfs_zap_mapping(inode, inode->i_mapping);
	inode_dio_done(inode);
}

#if IS_ENABLED(CONFIG_NFS_V3) || IS_ENABLED(CONFIG_NFS_V4)
static void nfs_direct_write_reschedule(struct nfs_direct_req *dreq)
{
	struct nfs_pageio_descriptor desc;
	struct nfs_page *req, *tmp;
	LIST_HEAD(reqs);
	struct nfs_commit_info cinfo;
	LIST_HEAD(failed);

	nfs_init_cinfo_from_dreq(&cinfo, dreq);
	pnfs_recover_commit_reqs(dreq->inode, &reqs, &cinfo);
	spin_lock(cinfo.lock);
	nfs_scan_commit_list(&cinfo.mds->list, &reqs, &cinfo, 0);
	spin_unlock(cinfo.lock);

	dreq->count = 0;
	get_dreq(dreq);

	NFS_PROTO(dreq->inode)->write_pageio_init(&desc, dreq->inode, FLUSH_STABLE,
			      &nfs_direct_write_completion_ops);
	desc.pg_dreq = dreq;

	list_for_each_entry_safe(req, tmp, &reqs, wb_list) {
		if (!nfs_pageio_add_request(&desc, req)) {
			nfs_list_remove_request(req);
			nfs_list_add_request(req, &failed);
			spin_lock(cinfo.lock);
			dreq->flags = 0;
			dreq->error = -EIO;
			spin_unlock(cinfo.lock);
		}
		nfs_release_request(req);
	}
	nfs_pageio_complete(&desc);

	while (!list_empty(&failed)) {
		req = nfs_list_entry(failed.next);
		nfs_list_remove_request(req);
		nfs_unlock_and_release_request(req);
	}

	if (put_dreq(dreq))
		nfs_direct_write_complete(dreq, dreq->inode);
}

static void nfs_direct_commit_complete(struct nfs_commit_data *data)
{
	struct nfs_direct_req *dreq = data->dreq;
	struct nfs_commit_info cinfo;
	struct nfs_page *req;
	int status = data->task.tk_status;

	nfs_init_cinfo_from_dreq(&cinfo, dreq);
	if (status < 0) {
		dprintk("NFS: %5u commit failed with error %d.\n",
			data->task.tk_pid, status);
		dreq->flags = NFS_ODIRECT_RESCHED_WRITES;
	} else if (memcmp(&dreq->verf, &data->verf, sizeof(data->verf))) {
		dprintk("NFS: %5u commit verify failed\n", data->task.tk_pid);
		dreq->flags = NFS_ODIRECT_RESCHED_WRITES;
	}

	dprintk("NFS: %5u commit returned %d\n", data->task.tk_pid, status);
	while (!list_empty(&data->pages)) {
		req = nfs_list_entry(data->pages.next);
		nfs_list_remove_request(req);
		if (dreq->flags == NFS_ODIRECT_RESCHED_WRITES) {
			/* Note the rewrite will go through mds */
			nfs_mark_request_commit(req, NULL, &cinfo);
		} else
			nfs_release_request(req);
		nfs_unlock_and_release_request(req);
	}

	if (atomic_dec_and_test(&cinfo.mds->rpcs_out))
		nfs_direct_write_complete(dreq, data->inode);
}

static void nfs_direct_error_cleanup(struct nfs_inode *nfsi)
{
	/* There is no lock to clear */
}

static const struct nfs_commit_completion_ops nfs_direct_commit_completion_ops = {
	.completion = nfs_direct_commit_complete,
	.error_cleanup = nfs_direct_error_cleanup,
};

static void nfs_direct_commit_schedule(struct nfs_direct_req *dreq)
{
	int res;
	struct nfs_commit_info cinfo;
	LIST_HEAD(mds_list);

	nfs_init_cinfo_from_dreq(&cinfo, dreq);
	nfs_scan_commit(dreq->inode, &mds_list, &cinfo);
	res = nfs_generic_commit_list(dreq->inode, &mds_list, 0, &cinfo);
	if (res < 0) /* res == -ENOMEM */
		nfs_direct_write_reschedule(dreq);
}

static void nfs_direct_write_schedule_work(struct work_struct *work)
{
	struct nfs_direct_req *dreq = container_of(work, struct nfs_direct_req, work);
	int flags = dreq->flags;

	dreq->flags = 0;
	switch (flags) {
		case NFS_ODIRECT_DO_COMMIT:
			nfs_direct_commit_schedule(dreq);
			break;
		case NFS_ODIRECT_RESCHED_WRITES:
			nfs_direct_write_reschedule(dreq);
			break;
		default:
			nfs_inode_dio_write_done(dreq->inode);
			nfs_direct_complete(dreq);
	}
}

static void nfs_direct_write_complete(struct nfs_direct_req *dreq, struct inode *inode)
{
	schedule_work(&dreq->work); /* Calls nfs_direct_write_schedule_work */
}

#else
static void nfs_direct_write_schedule_work(struct work_struct *work)
{
}

static void nfs_direct_write_complete(struct nfs_direct_req *dreq, struct inode *inode)
{
	nfs_inode_dio_write_done(inode);
	nfs_direct_complete(dreq);
}
#endif

/*
 * NB: Return the value of the first error return code.  Subsequent
 *     errors after the first one are ignored.
 */
/*
 * For each wsize'd chunk of the user's buffer, dispatch an NFS WRITE
 * operation.  If nfs_writedata_alloc() or get_user_pages() fails,
 * bail and stop sending more writes.  Write length accounting is
 * handled automatically by nfs_direct_write_result().  Otherwise, if
 * no requests have been sent, just return an error.
 */
static ssize_t nfs_direct_write_schedule_segment(struct nfs_pageio_descriptor *desc,
						 const struct iovec *iov,
						 loff_t pos, bool uio)
{
	struct nfs_direct_req *dreq = desc->pg_dreq;
	struct nfs_open_context *ctx = dreq->ctx;
	struct inode *inode = ctx->dentry->d_inode;
	unsigned long user_addr = (unsigned long)iov->iov_base;
	size_t count = iov->iov_len;
	size_t wsize = NFS_SERVER(inode)->wsize;
	unsigned int pgbase;
	int result;
	ssize_t started = 0;
	struct page **pagevec = NULL;
	unsigned int npages;

	do {
		size_t bytes;
		int i;

		pgbase = user_addr & ~PAGE_MASK;
		bytes = min(max_t(size_t, wsize, PAGE_SIZE), count);

		result = -ENOMEM;
		npages = nfs_page_array_len(pgbase, bytes);
		if (!pagevec)
			pagevec = kmalloc(npages * sizeof(struct page *), GFP_KERNEL);
		if (!pagevec)
			break;

		if (uio) {
			down_read(&current->mm->mmap_sem);
			result = get_user_pages(current, current->mm, user_addr,
						npages, 0, 0, pagevec, NULL);
			up_read(&current->mm->mmap_sem);
			if (result < 0)
				break;
		} else {
			WARN_ON(npages != 1);
			result = get_kernel_page(user_addr, 0, pagevec);
			if (WARN_ON(result != 1))
				break;
		}

		if ((unsigned)result < npages) {
			bytes = result * PAGE_SIZE;
			if (bytes <= pgbase) {
				nfs_direct_release_pages(pagevec, result);
				break;
			}
			bytes -= pgbase;
			npages = result;
		}

		for (i = 0; i < npages; i++) {
			struct nfs_page *req;
			unsigned int req_len = min_t(size_t, bytes, PAGE_SIZE - pgbase);

			req = nfs_create_request(dreq->ctx, dreq->inode,
						 pagevec[i],
						 pgbase, req_len);
			if (IS_ERR(req)) {
				result = PTR_ERR(req);
				break;
			}
			nfs_lock_request(req);
			req->wb_index = pos >> PAGE_SHIFT;
			req->wb_offset = pos & ~PAGE_MASK;
			if (!nfs_pageio_add_request(desc, req)) {
				result = desc->pg_error;
				nfs_unlock_and_release_request(req);
				break;
			}
			pgbase = 0;
			bytes -= req_len;
			started += req_len;
			user_addr += req_len;
			pos += req_len;
			count -= req_len;
			dreq->bytes_left -= req_len;
		}
		/* The nfs_page now hold references to these pages */
		nfs_direct_release_pages(pagevec, npages);
	} while (count != 0 && result >= 0);

	kfree(pagevec);

	if (started)
		return started;
	return result < 0 ? (ssize_t) result : -EFAULT;
}

static void nfs_direct_write_completion(struct nfs_pgio_header *hdr)
{
	struct nfs_direct_req *dreq = hdr->dreq;
	struct nfs_commit_info cinfo;
	int bit = -1;
	struct nfs_page *req = nfs_list_entry(hdr->pages.next);

	if (test_bit(NFS_IOHDR_REDO, &hdr->flags))
		goto out_put;

	nfs_init_cinfo_from_dreq(&cinfo, dreq);

	spin_lock(&dreq->lock);

	if (test_bit(NFS_IOHDR_ERROR, &hdr->flags)) {
		dreq->flags = 0;
		dreq->error = hdr->error;
	}
	if (dreq->error != 0)
		bit = NFS_IOHDR_ERROR;
	else {
		dreq->count += hdr->good_bytes;
		if (test_bit(NFS_IOHDR_NEED_RESCHED, &hdr->flags)) {
			dreq->flags = NFS_ODIRECT_RESCHED_WRITES;
			bit = NFS_IOHDR_NEED_RESCHED;
		} else if (test_bit(NFS_IOHDR_NEED_COMMIT, &hdr->flags)) {
			if (dreq->flags == NFS_ODIRECT_RESCHED_WRITES)
				bit = NFS_IOHDR_NEED_RESCHED;
			else if (dreq->flags == 0) {
				memcpy(&dreq->verf, hdr->verf,
				       sizeof(dreq->verf));
				bit = NFS_IOHDR_NEED_COMMIT;
				dreq->flags = NFS_ODIRECT_DO_COMMIT;
			} else if (dreq->flags == NFS_ODIRECT_DO_COMMIT) {
				if (memcmp(&dreq->verf, hdr->verf, sizeof(dreq->verf))) {
					dreq->flags = NFS_ODIRECT_RESCHED_WRITES;
					bit = NFS_IOHDR_NEED_RESCHED;
				} else
					bit = NFS_IOHDR_NEED_COMMIT;
			}
		}
	}
	spin_unlock(&dreq->lock);

	while (!list_empty(&hdr->pages)) {
		req = nfs_list_entry(hdr->pages.next);
		nfs_list_remove_request(req);
		switch (bit) {
		case NFS_IOHDR_NEED_RESCHED:
		case NFS_IOHDR_NEED_COMMIT:
			kref_get(&req->wb_kref);
			nfs_mark_request_commit(req, hdr->lseg, &cinfo);
		}
		nfs_unlock_and_release_request(req);
	}

out_put:
	if (put_dreq(dreq))
		nfs_direct_write_complete(dreq, hdr->inode);
	hdr->release(hdr);
}

static void nfs_write_sync_pgio_error(struct list_head *head)
{
	struct nfs_page *req;

	while (!list_empty(head)) {
		req = nfs_list_entry(head->next);
		nfs_list_remove_request(req);
		nfs_unlock_and_release_request(req);
	}
}

static const struct nfs_pgio_completion_ops nfs_direct_write_completion_ops = {
	.error_cleanup = nfs_write_sync_pgio_error,
	.init_hdr = nfs_direct_pgio_init,
	.completion = nfs_direct_write_completion,
};

static ssize_t nfs_direct_write_schedule_iovec(struct nfs_direct_req *dreq,
					       const struct iovec *iov,
					       unsigned long nr_segs,
					       loff_t pos, bool uio)
{
	struct nfs_pageio_descriptor desc;
	struct inode *inode = dreq->inode;
	ssize_t result = 0;
	size_t requested_bytes = 0;
	unsigned long seg;

	NFS_PROTO(inode)->write_pageio_init(&desc, inode, FLUSH_COND_STABLE,
			      &nfs_direct_write_completion_ops);
	desc.pg_dreq = dreq;
	get_dreq(dreq);
	atomic_inc(&inode->i_dio_count);

	NFS_I(dreq->inode)->write_io += iov_length(iov, nr_segs);
	for (seg = 0; seg < nr_segs; seg++) {
		const struct iovec *vec = &iov[seg];
		result = nfs_direct_write_schedule_segment(&desc, vec, pos, uio);
		if (result < 0)
			break;
		requested_bytes += result;
		if ((size_t)result < vec->iov_len)
			break;
		pos += vec->iov_len;
	}
	nfs_pageio_complete(&desc);

	/*
	 * If no bytes were started, return the error, and let the
	 * generic layer handle the completion.
	 */
	if (requested_bytes == 0) {
		inode_dio_done(inode);
		nfs_direct_req_release(dreq);
		return result < 0 ? result : -EIO;
	}

	if (put_dreq(dreq))
		nfs_direct_write_complete(dreq, dreq->inode);
	return 0;
}

static ssize_t nfs_direct_write(struct kiocb *iocb, const struct iovec *iov,
				unsigned long nr_segs, loff_t pos,
				size_t count, bool uio)
{
	ssize_t result = -ENOMEM;
	struct inode *inode = iocb->ki_filp->f_mapping->host;
	struct nfs_direct_req *dreq;
	struct nfs_lock_context *l_ctx;

	dreq = nfs_direct_req_alloc();
	if (!dreq)
		goto out;

	dreq->inode = inode;
	dreq->bytes_left = count;
	dreq->ctx = get_nfs_open_context(nfs_file_open_context(iocb->ki_filp));
	l_ctx = nfs_get_lock_context(dreq->ctx);
	if (IS_ERR(l_ctx)) {
		result = PTR_ERR(l_ctx);
		goto out_release;
	}
	dreq->l_ctx = l_ctx;
	if (!is_sync_kiocb(iocb))
		dreq->iocb = iocb;

	result = nfs_direct_write_schedule_iovec(dreq, iov, nr_segs, pos, uio);
	if (!result)
		result = nfs_direct_wait(dreq);
out_release:
	nfs_direct_req_release(dreq);
out:
	return result;
}

/**
 * nfs_file_direct_read - file direct read operation for NFS files
 * @iocb: target I/O control block
 * @iov: vector of user buffers into which to read data
 * @nr_segs: size of iov vector
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
ssize_t nfs_file_direct_read(struct kiocb *iocb, const struct iovec *iov,
				unsigned long nr_segs, loff_t pos, bool uio)
{
	ssize_t retval = -EINVAL;
	struct file *file = iocb->ki_filp;
	struct address_space *mapping = file->f_mapping;
	size_t count;

	count = iov_length(iov, nr_segs);
	nfs_add_stats(mapping->host, NFSIOS_DIRECTREADBYTES, count);

	dfprintk(FILE, "NFS: direct read(%s/%s, %zd@%Ld)\n",
		file->f_path.dentry->d_parent->d_name.name,
		file->f_path.dentry->d_name.name,
		count, (long long) pos);

	retval = 0;
	if (!count)
		goto out;

	retval = nfs_sync_mapping(mapping);
	if (retval)
		goto out;

	task_io_account_read(count);

	retval = nfs_direct_read(iocb, iov, nr_segs, pos, uio);
	if (retval > 0)
		iocb->ki_pos = pos + retval;

out:
	return retval;
}

/**
 * nfs_file_direct_write - file direct write operation for NFS files
 * @iocb: target I/O control block
 * @iov: vector of user buffers from which to write data
 * @nr_segs: size of iov vector
 * @pos: byte offset in file where writing starts
 *
 * We use this function for direct writes instead of calling
 * generic_file_aio_write() in order to avoid taking the inode
 * semaphore and updating the i_size.  The NFS server will set
 * the new i_size and this client must read the updated size
 * back into its cache.  We let the server do generic write
 * parameter checking and report problems.
 *
 * We eliminate local atime updates, see direct read above.
 *
 * We avoid unnecessary page cache invalidations for normal cached
 * readers of this file.
 *
 * Note that O_APPEND is not supported for NFS direct writes, as there
 * is no atomic O_APPEND write facility in the NFS protocol.
 */
ssize_t nfs_file_direct_write(struct kiocb *iocb, const struct iovec *iov,
				unsigned long nr_segs, loff_t pos, bool uio)
{
	ssize_t retval = -EINVAL;
	struct file *file = iocb->ki_filp;
	struct address_space *mapping = file->f_mapping;
	size_t count;

	count = iov_length(iov, nr_segs);
	nfs_add_stats(mapping->host, NFSIOS_DIRECTWRITTENBYTES, count);

	dfprintk(FILE, "NFS: direct write(%s/%s, %zd@%Ld)\n",
		file->f_path.dentry->d_parent->d_name.name,
		file->f_path.dentry->d_name.name,
		count, (long long) pos);

	retval = generic_write_checks(file, &pos, &count, 0);
	if (retval)
		goto out;

	retval = -EINVAL;
	if ((ssize_t) count < 0)
		goto out;
	retval = 0;
	if (!count)
		goto out;

	retval = nfs_sync_mapping(mapping);
	if (retval)
		goto out;

	task_io_account_write(count);

	retval = nfs_direct_write(iocb, iov, nr_segs, pos, count, uio);
	if (retval > 0) {
		struct inode *inode = mapping->host;

		iocb->ki_pos = pos + retval;
		spin_lock(&inode->i_lock);
		if (i_size_read(inode) < iocb->ki_pos)
			i_size_write(inode, iocb->ki_pos);
		spin_unlock(&inode->i_lock);
	}
out:
	return retval;
}

/**
 * nfs_init_directcache - create a slab cache for nfs_direct_req structures
 *
 */
int __init nfs_init_directcache(void)
{
	nfs_direct_cachep = kmem_cache_create("nfs_direct_cache",
						sizeof(struct nfs_direct_req),
						0, (SLAB_RECLAIM_ACCOUNT|
							SLAB_MEM_SPREAD),
						NULL);
	if (nfs_direct_cachep == NULL)
		return -ENOMEM;

	return 0;
}

/**
 * nfs_destroy_directcache - destroy the slab cache for nfs_direct_req structures
 *
 */
void nfs_destroy_directcache(void)
{
	kmem_cache_destroy(nfs_direct_cachep);
}
