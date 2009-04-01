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

#include <linux/nfs_fs.h>
#include <linux/nfs_page.h>
#include <linux/sunrpc/clnt.h>

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/atomic.h>

#include "internal.h"
#include "iostat.h"

#define NFSDBG_FACILITY		NFSDBG_VFS

static struct kmem_cache *nfs_direct_cachep;

/*
 * This represents a set of asynchronous requests that we're waiting on
 */
struct nfs_direct_req {
	struct kref		kref;		/* release manager */

	/* I/O parameters */
	struct nfs_open_context	*ctx;		/* file open context info */
	struct kiocb *		iocb;		/* controlling i/o request */
	struct inode *		inode;		/* target file of i/o */

	/* completion state */
	atomic_t		io_count;	/* i/os we're waiting for */
	spinlock_t		lock;		/* protect completion state */
	ssize_t			count,		/* bytes actually processed */
				error;		/* any reported error */
	struct completion	completion;	/* wait for i/o completion */

	/* commit state */
	struct list_head	rewrite_list;	/* saved nfs_write_data structs */
	struct nfs_write_data *	commit_data;	/* special write_data for commits */
	int			flags;
#define NFS_ODIRECT_DO_COMMIT		(1)	/* an unstable reply was received */
#define NFS_ODIRECT_RESCHED_WRITES	(2)	/* write verification failed */
	struct nfs_writeverf	verf;		/* unstable write verifier */
};

static void nfs_direct_write_complete(struct nfs_direct_req *dreq, struct inode *inode);
static const struct rpc_call_ops nfs_write_direct_ops;

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
 * the NFS client supports direct I/O.  However, we shunt off direct
 * read and write requests before the VFS gets them, so this method
 * should never be called.
 */
ssize_t nfs_direct_IO(int rw, struct kiocb *iocb, const struct iovec *iov, loff_t pos, unsigned long nr_segs)
{
	dprintk("NFS: nfs_direct_IO (%s) off/no(%Ld/%lu) EINVAL\n",
			iocb->ki_filp->f_path.dentry->d_name.name,
			(long long) pos, nr_segs);

	return -EINVAL;
}

static void nfs_direct_dirty_pages(struct page **pages, unsigned int pgbase, size_t count)
{
	unsigned int npages;
	unsigned int i;

	if (count == 0)
		return;
	pages += (pgbase >> PAGE_SHIFT);
	npages = (count + (pgbase & ~PAGE_MASK) + PAGE_SIZE - 1) >> PAGE_SHIFT;
	for (i = 0; i < npages; i++) {
		struct page *page = pages[i];
		if (!PageCompound(page))
			set_page_dirty(page);
	}
}

static void nfs_direct_release_pages(struct page **pages, unsigned int npages)
{
	unsigned int i;
	for (i = 0; i < npages; i++)
		page_cache_release(pages[i]);
}

static inline struct nfs_direct_req *nfs_direct_req_alloc(void)
{
	struct nfs_direct_req *dreq;

	dreq = kmem_cache_alloc(nfs_direct_cachep, GFP_KERNEL);
	if (!dreq)
		return NULL;

	kref_init(&dreq->kref);
	kref_get(&dreq->kref);
	init_completion(&dreq->completion);
	INIT_LIST_HEAD(&dreq->rewrite_list);
	dreq->iocb = NULL;
	dreq->ctx = NULL;
	spin_lock_init(&dreq->lock);
	atomic_set(&dreq->io_count, 0);
	dreq->count = 0;
	dreq->error = 0;
	dreq->flags = 0;

	return dreq;
}

static void nfs_direct_req_free(struct kref *kref)
{
	struct nfs_direct_req *dreq = container_of(kref, struct nfs_direct_req, kref);

	if (dreq->ctx != NULL)
		put_nfs_open_context(dreq->ctx);
	kmem_cache_free(nfs_direct_cachep, dreq);
}

static void nfs_direct_req_release(struct nfs_direct_req *dreq)
{
	kref_put(&dreq->kref, nfs_direct_req_free);
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

/*
 * We must hold a reference to all the pages in this direct read request
 * until the RPCs complete.  This could be long *after* we are woken up in
 * nfs_direct_wait (for instance, if someone hits ^C on a slow server).
 */
static void nfs_direct_read_result(struct rpc_task *task, void *calldata)
{
	struct nfs_read_data *data = calldata;

	nfs_readpage_result(task, data);
}

static void nfs_direct_read_release(void *calldata)
{

	struct nfs_read_data *data = calldata;
	struct nfs_direct_req *dreq = (struct nfs_direct_req *) data->req;
	int status = data->task.tk_status;

	spin_lock(&dreq->lock);
	if (unlikely(status < 0)) {
		dreq->error = status;
		spin_unlock(&dreq->lock);
	} else {
		dreq->count += data->res.count;
		spin_unlock(&dreq->lock);
		nfs_direct_dirty_pages(data->pagevec,
				data->args.pgbase,
				data->res.count);
	}
	nfs_direct_release_pages(data->pagevec, data->npages);

	if (put_dreq(dreq))
		nfs_direct_complete(dreq);
	nfs_readdata_release(calldata);
}

static const struct rpc_call_ops nfs_read_direct_ops = {
#if defined(CONFIG_NFS_V4_1)
	.rpc_call_prepare = nfs_read_prepare,
#endif /* CONFIG_NFS_V4_1 */
	.rpc_call_done = nfs_direct_read_result,
	.rpc_release = nfs_direct_read_release,
};

/*
 * For each rsize'd chunk of the user's buffer, dispatch an NFS READ
 * operation.  If nfs_readdata_alloc() or get_user_pages() fails,
 * bail and stop sending more reads.  Read length accounting is
 * handled automatically by nfs_direct_read_result().  Otherwise, if
 * no requests have been sent, just return an error.
 */
static ssize_t nfs_direct_read_schedule_segment(struct nfs_direct_req *dreq,
						const struct iovec *iov,
						loff_t pos)
{
	struct nfs_open_context *ctx = dreq->ctx;
	struct inode *inode = ctx->path.dentry->d_inode;
	unsigned long user_addr = (unsigned long)iov->iov_base;
	size_t count = iov->iov_len;
	size_t rsize = NFS_SERVER(inode)->rsize;
	struct rpc_task *task;
	struct rpc_message msg = {
		.rpc_cred = ctx->cred,
	};
	struct rpc_task_setup task_setup_data = {
		.rpc_client = NFS_CLIENT(inode),
		.rpc_message = &msg,
		.callback_ops = &nfs_read_direct_ops,
		.workqueue = nfsiod_workqueue,
		.flags = RPC_TASK_ASYNC,
	};
	unsigned int pgbase;
	int result;
	ssize_t started = 0;

	do {
		struct nfs_read_data *data;
		size_t bytes;

		pgbase = user_addr & ~PAGE_MASK;
		bytes = min(rsize,count);

		result = -ENOMEM;
		data = nfs_readdata_alloc(nfs_page_array_len(pgbase, bytes));
		if (unlikely(!data))
			break;

		down_read(&current->mm->mmap_sem);
		result = get_user_pages(current, current->mm, user_addr,
					data->npages, 1, 0, data->pagevec, NULL);
		up_read(&current->mm->mmap_sem);
		if (result < 0) {
			nfs_readdata_release(data);
			break;
		}
		if ((unsigned)result < data->npages) {
			bytes = result * PAGE_SIZE;
			if (bytes <= pgbase) {
				nfs_direct_release_pages(data->pagevec, result);
				nfs_readdata_release(data);
				break;
			}
			bytes -= pgbase;
			data->npages = result;
		}

		get_dreq(dreq);

		data->req = (struct nfs_page *) dreq;
		data->inode = inode;
		data->cred = msg.rpc_cred;
		data->args.fh = NFS_FH(inode);
		data->args.context = get_nfs_open_context(ctx);
		data->args.offset = pos;
		data->args.pgbase = pgbase;
		data->args.pages = data->pagevec;
		data->args.count = bytes;
		data->res.fattr = &data->fattr;
		data->res.eof = 0;
		data->res.count = bytes;
		msg.rpc_argp = &data->args;
		msg.rpc_resp = &data->res;

		task_setup_data.task = &data->task;
		task_setup_data.callback_data = data;
		NFS_PROTO(inode)->read_setup(data, &msg);

		task = rpc_run_task(&task_setup_data);
		if (IS_ERR(task))
			break;
		rpc_put_task(task);

		dprintk("NFS: %5u initiated direct read call "
			"(req %s/%Ld, %zu bytes @ offset %Lu)\n",
				data->task.tk_pid,
				inode->i_sb->s_id,
				(long long)NFS_FILEID(inode),
				bytes,
				(unsigned long long)data->args.offset);

		started += bytes;
		user_addr += bytes;
		pos += bytes;
		/* FIXME: Remove this unnecessary math from final patch */
		pgbase += bytes;
		pgbase &= ~PAGE_MASK;
		BUG_ON(pgbase != (user_addr & ~PAGE_MASK));

		count -= bytes;
	} while (count != 0);

	if (started)
		return started;
	return result < 0 ? (ssize_t) result : -EFAULT;
}

static ssize_t nfs_direct_read_schedule_iovec(struct nfs_direct_req *dreq,
					      const struct iovec *iov,
					      unsigned long nr_segs,
					      loff_t pos)
{
	ssize_t result = -EINVAL;
	size_t requested_bytes = 0;
	unsigned long seg;

	get_dreq(dreq);

	for (seg = 0; seg < nr_segs; seg++) {
		const struct iovec *vec = &iov[seg];
		result = nfs_direct_read_schedule_segment(dreq, vec, pos);
		if (result < 0)
			break;
		requested_bytes += result;
		if ((size_t)result < vec->iov_len)
			break;
		pos += vec->iov_len;
	}

	if (put_dreq(dreq))
		nfs_direct_complete(dreq);

	if (requested_bytes != 0)
		return 0;

	if (result < 0)
		return result;
	return -EIO;
}

static ssize_t nfs_direct_read(struct kiocb *iocb, const struct iovec *iov,
			       unsigned long nr_segs, loff_t pos)
{
	ssize_t result = 0;
	struct inode *inode = iocb->ki_filp->f_mapping->host;
	struct nfs_direct_req *dreq;

	dreq = nfs_direct_req_alloc();
	if (!dreq)
		return -ENOMEM;

	dreq->inode = inode;
	dreq->ctx = get_nfs_open_context(nfs_file_open_context(iocb->ki_filp));
	if (!is_sync_kiocb(iocb))
		dreq->iocb = iocb;

	result = nfs_direct_read_schedule_iovec(dreq, iov, nr_segs, pos);
	if (!result)
		result = nfs_direct_wait(dreq);
	nfs_direct_req_release(dreq);

	return result;
}

static void nfs_direct_free_writedata(struct nfs_direct_req *dreq)
{
	while (!list_empty(&dreq->rewrite_list)) {
		struct nfs_write_data *data = list_entry(dreq->rewrite_list.next, struct nfs_write_data, pages);
		list_del(&data->pages);
		nfs_direct_release_pages(data->pagevec, data->npages);
		nfs_writedata_release(data);
	}
}

#if defined(CONFIG_NFS_V3) || defined(CONFIG_NFS_V4)
static void nfs_direct_write_reschedule(struct nfs_direct_req *dreq)
{
	struct inode *inode = dreq->inode;
	struct list_head *p;
	struct nfs_write_data *data;
	struct rpc_task *task;
	struct rpc_message msg = {
		.rpc_cred = dreq->ctx->cred,
	};
	struct rpc_task_setup task_setup_data = {
		.rpc_client = NFS_CLIENT(inode),
		.callback_ops = &nfs_write_direct_ops,
		.workqueue = nfsiod_workqueue,
		.flags = RPC_TASK_ASYNC,
	};

	dreq->count = 0;
	get_dreq(dreq);

	list_for_each(p, &dreq->rewrite_list) {
		data = list_entry(p, struct nfs_write_data, pages);

		get_dreq(dreq);

		/* Use stable writes */
		data->args.stable = NFS_FILE_SYNC;

		/*
		 * Reset data->res.
		 */
		nfs_fattr_init(&data->fattr);
		data->res.count = data->args.count;
		memset(&data->verf, 0, sizeof(data->verf));

		/*
		 * Reuse data->task; data->args should not have changed
		 * since the original request was sent.
		 */
		task_setup_data.task = &data->task;
		task_setup_data.callback_data = data;
		msg.rpc_argp = &data->args;
		msg.rpc_resp = &data->res;
		NFS_PROTO(inode)->write_setup(data, &msg);

		/*
		 * We're called via an RPC callback, so BKL is already held.
		 */
		task = rpc_run_task(&task_setup_data);
		if (!IS_ERR(task))
			rpc_put_task(task);

		dprintk("NFS: %5u rescheduled direct write call (req %s/%Ld, %u bytes @ offset %Lu)\n",
				data->task.tk_pid,
				inode->i_sb->s_id,
				(long long)NFS_FILEID(inode),
				data->args.count,
				(unsigned long long)data->args.offset);
	}

	if (put_dreq(dreq))
		nfs_direct_write_complete(dreq, inode);
}

static void nfs_direct_commit_result(struct rpc_task *task, void *calldata)
{
	struct nfs_write_data *data = calldata;

	/* Call the NFS version-specific code */
	NFS_PROTO(data->inode)->commit_done(task, data);
}

static void nfs_direct_commit_release(void *calldata)
{
	struct nfs_write_data *data = calldata;
	struct nfs_direct_req *dreq = (struct nfs_direct_req *) data->req;
	int status = data->task.tk_status;

	if (status < 0) {
		dprintk("NFS: %5u commit failed with error %d.\n",
				data->task.tk_pid, status);
		dreq->flags = NFS_ODIRECT_RESCHED_WRITES;
	} else if (memcmp(&dreq->verf, &data->verf, sizeof(data->verf))) {
		dprintk("NFS: %5u commit verify failed\n", data->task.tk_pid);
		dreq->flags = NFS_ODIRECT_RESCHED_WRITES;
	}

	dprintk("NFS: %5u commit returned %d\n", data->task.tk_pid, status);
	nfs_direct_write_complete(dreq, data->inode);
	nfs_commitdata_release(calldata);
}

static const struct rpc_call_ops nfs_commit_direct_ops = {
#if defined(CONFIG_NFS_V4_1)
	.rpc_call_prepare = nfs_write_prepare,
#endif /* CONFIG_NFS_V4_1 */
	.rpc_call_done = nfs_direct_commit_result,
	.rpc_release = nfs_direct_commit_release,
};

static void nfs_direct_commit_schedule(struct nfs_direct_req *dreq)
{
	struct nfs_write_data *data = dreq->commit_data;
	struct rpc_task *task;
	struct rpc_message msg = {
		.rpc_argp = &data->args,
		.rpc_resp = &data->res,
		.rpc_cred = dreq->ctx->cred,
	};
	struct rpc_task_setup task_setup_data = {
		.task = &data->task,
		.rpc_client = NFS_CLIENT(dreq->inode),
		.rpc_message = &msg,
		.callback_ops = &nfs_commit_direct_ops,
		.callback_data = data,
		.workqueue = nfsiod_workqueue,
		.flags = RPC_TASK_ASYNC,
	};

	data->inode = dreq->inode;
	data->cred = msg.rpc_cred;

	data->args.fh = NFS_FH(data->inode);
	data->args.offset = 0;
	data->args.count = 0;
	data->args.context = get_nfs_open_context(dreq->ctx);
	data->res.count = 0;
	data->res.fattr = &data->fattr;
	data->res.verf = &data->verf;

	NFS_PROTO(data->inode)->commit_setup(data, &msg);

	/* Note: task.tk_ops->rpc_release will free dreq->commit_data */
	dreq->commit_data = NULL;

	dprintk("NFS: %5u initiated commit call\n", data->task.tk_pid);

	task = rpc_run_task(&task_setup_data);
	if (!IS_ERR(task))
		rpc_put_task(task);
}

static void nfs_direct_write_complete(struct nfs_direct_req *dreq, struct inode *inode)
{
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
			if (dreq->commit_data != NULL)
				nfs_commit_free(dreq->commit_data);
			nfs_direct_free_writedata(dreq);
			nfs_zap_mapping(inode, inode->i_mapping);
			nfs_direct_complete(dreq);
	}
}

static void nfs_alloc_commit_data(struct nfs_direct_req *dreq)
{
	dreq->commit_data = nfs_commitdata_alloc();
	if (dreq->commit_data != NULL)
		dreq->commit_data->req = (struct nfs_page *) dreq;
}
#else
static inline void nfs_alloc_commit_data(struct nfs_direct_req *dreq)
{
	dreq->commit_data = NULL;
}

static void nfs_direct_write_complete(struct nfs_direct_req *dreq, struct inode *inode)
{
	nfs_direct_free_writedata(dreq);
	nfs_zap_mapping(inode, inode->i_mapping);
	nfs_direct_complete(dreq);
}
#endif

static void nfs_direct_write_result(struct rpc_task *task, void *calldata)
{
	struct nfs_write_data *data = calldata;

	if (nfs_writeback_done(task, data) != 0)
		return;
}

/*
 * NB: Return the value of the first error return code.  Subsequent
 *     errors after the first one are ignored.
 */
static void nfs_direct_write_release(void *calldata)
{
	struct nfs_write_data *data = calldata;
	struct nfs_direct_req *dreq = (struct nfs_direct_req *) data->req;
	int status = data->task.tk_status;

	spin_lock(&dreq->lock);

	if (unlikely(status < 0)) {
		/* An error has occurred, so we should not commit */
		dreq->flags = 0;
		dreq->error = status;
	}
	if (unlikely(dreq->error != 0))
		goto out_unlock;

	dreq->count += data->res.count;

	if (data->res.verf->committed != NFS_FILE_SYNC) {
		switch (dreq->flags) {
			case 0:
				memcpy(&dreq->verf, &data->verf, sizeof(dreq->verf));
				dreq->flags = NFS_ODIRECT_DO_COMMIT;
				break;
			case NFS_ODIRECT_DO_COMMIT:
				if (memcmp(&dreq->verf, &data->verf, sizeof(dreq->verf))) {
					dprintk("NFS: %5u write verify failed\n", data->task.tk_pid);
					dreq->flags = NFS_ODIRECT_RESCHED_WRITES;
				}
		}
	}
out_unlock:
	spin_unlock(&dreq->lock);

	if (put_dreq(dreq))
		nfs_direct_write_complete(dreq, data->inode);
}

static const struct rpc_call_ops nfs_write_direct_ops = {
#if defined(CONFIG_NFS_V4_1)
	.rpc_call_prepare = nfs_write_prepare,
#endif /* CONFIG_NFS_V4_1 */
	.rpc_call_done = nfs_direct_write_result,
	.rpc_release = nfs_direct_write_release,
};

/*
 * For each wsize'd chunk of the user's buffer, dispatch an NFS WRITE
 * operation.  If nfs_writedata_alloc() or get_user_pages() fails,
 * bail and stop sending more writes.  Write length accounting is
 * handled automatically by nfs_direct_write_result().  Otherwise, if
 * no requests have been sent, just return an error.
 */
static ssize_t nfs_direct_write_schedule_segment(struct nfs_direct_req *dreq,
						 const struct iovec *iov,
						 loff_t pos, int sync)
{
	struct nfs_open_context *ctx = dreq->ctx;
	struct inode *inode = ctx->path.dentry->d_inode;
	unsigned long user_addr = (unsigned long)iov->iov_base;
	size_t count = iov->iov_len;
	struct rpc_task *task;
	struct rpc_message msg = {
		.rpc_cred = ctx->cred,
	};
	struct rpc_task_setup task_setup_data = {
		.rpc_client = NFS_CLIENT(inode),
		.rpc_message = &msg,
		.callback_ops = &nfs_write_direct_ops,
		.workqueue = nfsiod_workqueue,
		.flags = RPC_TASK_ASYNC,
	};
	size_t wsize = NFS_SERVER(inode)->wsize;
	unsigned int pgbase;
	int result;
	ssize_t started = 0;

	do {
		struct nfs_write_data *data;
		size_t bytes;

		pgbase = user_addr & ~PAGE_MASK;
		bytes = min(wsize,count);

		result = -ENOMEM;
		data = nfs_writedata_alloc(nfs_page_array_len(pgbase, bytes));
		if (unlikely(!data))
			break;

		down_read(&current->mm->mmap_sem);
		result = get_user_pages(current, current->mm, user_addr,
					data->npages, 0, 0, data->pagevec, NULL);
		up_read(&current->mm->mmap_sem);
		if (result < 0) {
			nfs_writedata_release(data);
			break;
		}
		if ((unsigned)result < data->npages) {
			bytes = result * PAGE_SIZE;
			if (bytes <= pgbase) {
				nfs_direct_release_pages(data->pagevec, result);
				nfs_writedata_release(data);
				break;
			}
			bytes -= pgbase;
			data->npages = result;
		}

		get_dreq(dreq);

		list_move_tail(&data->pages, &dreq->rewrite_list);

		data->req = (struct nfs_page *) dreq;
		data->inode = inode;
		data->cred = msg.rpc_cred;
		data->args.fh = NFS_FH(inode);
		data->args.context = get_nfs_open_context(ctx);
		data->args.offset = pos;
		data->args.pgbase = pgbase;
		data->args.pages = data->pagevec;
		data->args.count = bytes;
		data->args.stable = sync;
		data->res.fattr = &data->fattr;
		data->res.count = bytes;
		data->res.verf = &data->verf;

		task_setup_data.task = &data->task;
		task_setup_data.callback_data = data;
		msg.rpc_argp = &data->args;
		msg.rpc_resp = &data->res;
		NFS_PROTO(inode)->write_setup(data, &msg);

		task = rpc_run_task(&task_setup_data);
		if (IS_ERR(task))
			break;
		rpc_put_task(task);

		dprintk("NFS: %5u initiated direct write call "
			"(req %s/%Ld, %zu bytes @ offset %Lu)\n",
				data->task.tk_pid,
				inode->i_sb->s_id,
				(long long)NFS_FILEID(inode),
				bytes,
				(unsigned long long)data->args.offset);

		started += bytes;
		user_addr += bytes;
		pos += bytes;

		/* FIXME: Remove this useless math from the final patch */
		pgbase += bytes;
		pgbase &= ~PAGE_MASK;
		BUG_ON(pgbase != (user_addr & ~PAGE_MASK));

		count -= bytes;
	} while (count != 0);

	if (started)
		return started;
	return result < 0 ? (ssize_t) result : -EFAULT;
}

static ssize_t nfs_direct_write_schedule_iovec(struct nfs_direct_req *dreq,
					       const struct iovec *iov,
					       unsigned long nr_segs,
					       loff_t pos, int sync)
{
	ssize_t result = 0;
	size_t requested_bytes = 0;
	unsigned long seg;

	get_dreq(dreq);

	for (seg = 0; seg < nr_segs; seg++) {
		const struct iovec *vec = &iov[seg];
		result = nfs_direct_write_schedule_segment(dreq, vec,
							   pos, sync);
		if (result < 0)
			break;
		requested_bytes += result;
		if ((size_t)result < vec->iov_len)
			break;
		pos += vec->iov_len;
	}

	if (put_dreq(dreq))
		nfs_direct_write_complete(dreq, dreq->inode);

	if (requested_bytes != 0)
		return 0;

	if (result < 0)
		return result;
	return -EIO;
}

static ssize_t nfs_direct_write(struct kiocb *iocb, const struct iovec *iov,
				unsigned long nr_segs, loff_t pos,
				size_t count)
{
	ssize_t result = 0;
	struct inode *inode = iocb->ki_filp->f_mapping->host;
	struct nfs_direct_req *dreq;
	size_t wsize = NFS_SERVER(inode)->wsize;
	int sync = NFS_UNSTABLE;

	dreq = nfs_direct_req_alloc();
	if (!dreq)
		return -ENOMEM;
	nfs_alloc_commit_data(dreq);

	if (dreq->commit_data == NULL || count < wsize)
		sync = NFS_FILE_SYNC;

	dreq->inode = inode;
	dreq->ctx = get_nfs_open_context(nfs_file_open_context(iocb->ki_filp));
	if (!is_sync_kiocb(iocb))
		dreq->iocb = iocb;

	result = nfs_direct_write_schedule_iovec(dreq, iov, nr_segs, pos, sync);
	if (!result)
		result = nfs_direct_wait(dreq);
	nfs_direct_req_release(dreq);

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
				unsigned long nr_segs, loff_t pos)
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

	retval = nfs_direct_read(iocb, iov, nr_segs, pos);
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
ssize_t nfs_file_direct_write(struct kiocb *iocb, const struct iovec *iov,
				unsigned long nr_segs, loff_t pos)
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

	retval = nfs_direct_write(iocb, iov, nr_segs, pos, count);

	if (retval > 0)
		iocb->ki_pos = pos + retval;

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
