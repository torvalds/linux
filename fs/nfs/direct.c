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

#include <linux/uaccess.h>
#include <linux/atomic.h>

#include "internal.h"
#include "iostat.h"
#include "pnfs.h"

#define NFSDBG_FACILITY		NFSDBG_VFS

static struct kmem_cache *nfs_direct_cachep;

/*
 * This represents a set of asynchronous requests that we're waiting on
 */
struct nfs_direct_mirror {
	ssize_t count;
};

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

	struct nfs_direct_mirror mirrors[NFS_PAGEIO_DESCRIPTOR_MIRROR_MAX];
	int			mirror_count;

	ssize_t			count,		/* bytes actually processed */
				max_count,	/* max expected count */
				bytes_left,	/* bytes left to be sent */
				io_start,	/* start of IO */
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
static void nfs_direct_write_complete(struct nfs_direct_req *dreq);
static void nfs_direct_write_schedule_work(struct work_struct *work);

static inline void get_dreq(struct nfs_direct_req *dreq)
{
	atomic_inc(&dreq->io_count);
}

static inline int put_dreq(struct nfs_direct_req *dreq)
{
	return atomic_dec_and_test(&dreq->io_count);
}

static void
nfs_direct_good_bytes(struct nfs_direct_req *dreq, struct nfs_pgio_header *hdr)
{
	int i;
	ssize_t count;

	WARN_ON_ONCE(dreq->count >= dreq->max_count);

	if (dreq->mirror_count == 1) {
		dreq->mirrors[hdr->pgio_mirror_idx].count += hdr->good_bytes;
		dreq->count += hdr->good_bytes;
	} else {
		/* mirrored writes */
		count = dreq->mirrors[hdr->pgio_mirror_idx].count;
		if (count + dreq->io_start < hdr->io_start + hdr->good_bytes) {
			count = hdr->io_start + hdr->good_bytes - dreq->io_start;
			dreq->mirrors[hdr->pgio_mirror_idx].count = count;
		}
		/* update the dreq->count by finding the minimum agreed count from all
		 * mirrors */
		count = dreq->mirrors[0].count;

		for (i = 1; i < dreq->mirror_count; i++)
			count = min(count, dreq->mirrors[i].count);

		dreq->count = count;
	}
}

/*
 * nfs_direct_select_verf - select the right verifier
 * @dreq - direct request possibly spanning multiple servers
 * @ds_clp - nfs_client of data server or NULL if MDS / non-pnfs
 * @commit_idx - commit bucket index for the DS
 *
 * returns the correct verifier to use given the role of the server
 */
static struct nfs_writeverf *
nfs_direct_select_verf(struct nfs_direct_req *dreq,
		       struct nfs_client *ds_clp,
		       int commit_idx)
{
	struct nfs_writeverf *verfp = &dreq->verf;

#ifdef CONFIG_NFS_V4_1
	/*
	 * pNFS is in use, use the DS verf except commit_through_mds is set
	 * for layout segment where nbuckets is zero.
	 */
	if (ds_clp && dreq->ds_cinfo.nbuckets > 0) {
		if (commit_idx >= 0 && commit_idx < dreq->ds_cinfo.nbuckets)
			verfp = &dreq->ds_cinfo.buckets[commit_idx].direct_verf;
		else
			WARN_ON_ONCE(1);
	}
#endif
	return verfp;
}


/*
 * nfs_direct_set_hdr_verf - set the write/commit verifier
 * @dreq - direct request possibly spanning multiple servers
 * @hdr - pageio header to validate against previously seen verfs
 *
 * Set the server's (MDS or DS) "seen" verifier
 */
static void nfs_direct_set_hdr_verf(struct nfs_direct_req *dreq,
				    struct nfs_pgio_header *hdr)
{
	struct nfs_writeverf *verfp;

	verfp = nfs_direct_select_verf(dreq, hdr->ds_clp, hdr->ds_commit_idx);
	WARN_ON_ONCE(verfp->committed >= 0);
	memcpy(verfp, &hdr->verf, sizeof(struct nfs_writeverf));
	WARN_ON_ONCE(verfp->committed < 0);
}

static int nfs_direct_cmp_verf(const struct nfs_writeverf *v1,
		const struct nfs_writeverf *v2)
{
	return nfs_write_verifier_cmp(&v1->verifier, &v2->verifier);
}

/*
 * nfs_direct_cmp_hdr_verf - compare verifier for pgio header
 * @dreq - direct request possibly spanning multiple servers
 * @hdr - pageio header to validate against previously seen verf
 *
 * set the server's "seen" verf if not initialized.
 * returns result of comparison between @hdr->verf and the "seen"
 * verf of the server used by @hdr (DS or MDS)
 */
static int nfs_direct_set_or_cmp_hdr_verf(struct nfs_direct_req *dreq,
					  struct nfs_pgio_header *hdr)
{
	struct nfs_writeverf *verfp;

	verfp = nfs_direct_select_verf(dreq, hdr->ds_clp, hdr->ds_commit_idx);
	if (verfp->committed < 0) {
		nfs_direct_set_hdr_verf(dreq, hdr);
		return 0;
	}
	return nfs_direct_cmp_verf(verfp, &hdr->verf);
}

/*
 * nfs_direct_cmp_commit_data_verf - compare verifier for commit data
 * @dreq - direct request possibly spanning multiple servers
 * @data - commit data to validate against previously seen verf
 *
 * returns result of comparison between @data->verf and the verf of
 * the server used by @data (DS or MDS)
 */
static int nfs_direct_cmp_commit_data_verf(struct nfs_direct_req *dreq,
					   struct nfs_commit_data *data)
{
	struct nfs_writeverf *verfp;

	verfp = nfs_direct_select_verf(dreq, data->ds_clp,
					 data->ds_commit_index);

	/* verifier not set so always fail */
	if (verfp->committed < 0)
		return 1;

	return nfs_direct_cmp_verf(verfp, &data->verf);
}

/**
 * nfs_direct_IO - NFS address space operation for direct I/O
 * @iocb: target I/O control block
 * @iter: I/O buffer
 *
 * The presence of this routine in the address space ops vector means
 * the NFS client supports direct I/O. However, for most direct IO, we
 * shunt off direct read and write requests before the VFS gets them,
 * so this method is only ever called for swap.
 */
ssize_t nfs_direct_IO(struct kiocb *iocb, struct iov_iter *iter)
{
	struct inode *inode = iocb->ki_filp->f_mapping->host;

	/* we only support swap file calling nfs_direct_IO */
	if (!IS_SWAPFILE(inode))
		return 0;

	VM_BUG_ON(iov_iter_count(iter) != PAGE_SIZE);

	if (iov_iter_rw(iter) == READ)
		return nfs_file_direct_read(iocb, iter);
	return nfs_file_direct_write(iocb, iter);
}

static void nfs_direct_release_pages(struct page **pages, unsigned int npages)
{
	unsigned int i;
	for (i = 0; i < npages; i++)
		put_page(pages[i]);
}

void nfs_init_cinfo_from_dreq(struct nfs_commit_info *cinfo,
			      struct nfs_direct_req *dreq)
{
	cinfo->inode = dreq->inode;
	cinfo->mds = &dreq->mds_cinfo;
	cinfo->ds = &dreq->ds_cinfo;
	cinfo->dreq = dreq;
	cinfo->completion_ops = &nfs_direct_commit_completion_ops;
}

static inline void nfs_direct_setup_mirroring(struct nfs_direct_req *dreq,
					     struct nfs_pageio_descriptor *pgio,
					     struct nfs_page *req)
{
	int mirror_count = 1;

	if (pgio->pg_ops->pg_get_mirror_count)
		mirror_count = pgio->pg_ops->pg_get_mirror_count(pgio, req);

	dreq->mirror_count = mirror_count;
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
	dreq->verf.committed = NFS_INVALID_STABLE_HOW;	/* not set yet */
	INIT_WORK(&dreq->work, nfs_direct_write_schedule_work);
	dreq->mirror_count = 1;
	spin_lock_init(&dreq->lock);

	return dreq;
}

static void nfs_direct_req_free(struct kref *kref)
{
	struct nfs_direct_req *dreq = container_of(kref, struct nfs_direct_req, kref);

	nfs_free_pnfs_ds_cinfo(&dreq->ds_cinfo);
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

	if (!result) {
		result = dreq->count;
		WARN_ON_ONCE(dreq->count < 0);
	}
	if (!result)
		result = dreq->error;

out:
	return (ssize_t) result;
}

/*
 * Synchronous I/O uses a stack-allocated iocb.  Thus we can't trust
 * the iocb is still valid here if this is a synchronous request.
 */
static void nfs_direct_complete(struct nfs_direct_req *dreq)
{
	struct inode *inode = dreq->inode;

	inode_dio_end(inode);

	if (dreq->iocb) {
		long res = (long) dreq->error;
		if (dreq->count != 0) {
			res = (long) dreq->count;
			WARN_ON_ONCE(dreq->count < 0);
		}
		dreq->iocb->ki_complete(dreq->iocb, res, 0);
	}

	complete(&dreq->completion);

	nfs_direct_req_release(dreq);
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
		nfs_direct_good_bytes(dreq, hdr);

	spin_unlock(&dreq->lock);

	while (!list_empty(&hdr->pages)) {
		struct nfs_page *req = nfs_list_entry(hdr->pages.next);
		struct page *page = req->wb_page;

		if (!PageCompound(page) && bytes < hdr->good_bytes)
			set_page_dirty(page);
		bytes += req->wb_bytes;
		nfs_list_remove_request(req);
		nfs_release_request(req);
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

static ssize_t nfs_direct_read_schedule_iovec(struct nfs_direct_req *dreq,
					      struct iov_iter *iter,
					      loff_t pos)
{
	struct nfs_pageio_descriptor desc;
	struct inode *inode = dreq->inode;
	ssize_t result = -EINVAL;
	size_t requested_bytes = 0;
	size_t rsize = max_t(size_t, NFS_SERVER(inode)->rsize, PAGE_SIZE);

	nfs_pageio_init_read(&desc, dreq->inode, false,
			     &nfs_direct_read_completion_ops);
	get_dreq(dreq);
	desc.pg_dreq = dreq;
	inode_dio_begin(inode);

	while (iov_iter_count(iter)) {
		struct page **pagevec;
		size_t bytes;
		size_t pgbase;
		unsigned npages, i;

		result = iov_iter_get_pages_alloc(iter, &pagevec, 
						  rsize, &pgbase);
		if (result < 0)
			break;
	
		bytes = result;
		iov_iter_advance(iter, bytes);
		npages = (result + pgbase + PAGE_SIZE - 1) / PAGE_SIZE;
		for (i = 0; i < npages; i++) {
			struct nfs_page *req;
			unsigned int req_len = min_t(size_t, bytes, PAGE_SIZE - pgbase);
			/* XXX do we need to do the eof zeroing found in async_filler? */
			req = nfs_create_request(dreq->ctx, pagevec[i], NULL,
						 pgbase, req_len);
			if (IS_ERR(req)) {
				result = PTR_ERR(req);
				break;
			}
			req->wb_index = pos >> PAGE_SHIFT;
			req->wb_offset = pos & ~PAGE_MASK;
			if (!nfs_pageio_add_request(&desc, req)) {
				result = desc.pg_error;
				nfs_release_request(req);
				break;
			}
			pgbase = 0;
			bytes -= req_len;
			requested_bytes += req_len;
			pos += req_len;
			dreq->bytes_left -= req_len;
		}
		nfs_direct_release_pages(pagevec, npages);
		kvfree(pagevec);
		if (result < 0)
			break;
	}

	nfs_pageio_complete(&desc);

	/*
	 * If no bytes were started, return the error, and let the
	 * generic layer handle the completion.
	 */
	if (requested_bytes == 0) {
		inode_dio_end(inode);
		nfs_direct_req_release(dreq);
		return result < 0 ? result : -EIO;
	}

	if (put_dreq(dreq))
		nfs_direct_complete(dreq);
	return requested_bytes;
}

/**
 * nfs_file_direct_read - file direct read operation for NFS files
 * @iocb: target I/O control block
 * @iter: vector of user buffers into which to read data
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
ssize_t nfs_file_direct_read(struct kiocb *iocb, struct iov_iter *iter)
{
	struct file *file = iocb->ki_filp;
	struct address_space *mapping = file->f_mapping;
	struct inode *inode = mapping->host;
	struct nfs_direct_req *dreq;
	struct nfs_lock_context *l_ctx;
	ssize_t result = -EINVAL, requested;
	size_t count = iov_iter_count(iter);
	nfs_add_stats(mapping->host, NFSIOS_DIRECTREADBYTES, count);

	dfprintk(FILE, "NFS: direct read(%pD2, %zd@%Ld)\n",
		file, count, (long long) iocb->ki_pos);

	result = 0;
	if (!count)
		goto out;

	task_io_account_read(count);

	result = -ENOMEM;
	dreq = nfs_direct_req_alloc();
	if (dreq == NULL)
		goto out;

	dreq->inode = inode;
	dreq->bytes_left = dreq->max_count = count;
	dreq->io_start = iocb->ki_pos;
	dreq->ctx = get_nfs_open_context(nfs_file_open_context(iocb->ki_filp));
	l_ctx = nfs_get_lock_context(dreq->ctx);
	if (IS_ERR(l_ctx)) {
		result = PTR_ERR(l_ctx);
		goto out_release;
	}
	dreq->l_ctx = l_ctx;
	if (!is_sync_kiocb(iocb))
		dreq->iocb = iocb;

	nfs_start_io_direct(inode);

	NFS_I(inode)->read_io += count;
	requested = nfs_direct_read_schedule_iovec(dreq, iter, iocb->ki_pos);

	nfs_end_io_direct(inode);

	if (requested > 0) {
		result = nfs_direct_wait(dreq);
		if (result > 0) {
			requested -= result;
			iocb->ki_pos += result;
		}
		iov_iter_revert(iter, requested);
	} else {
		result = requested;
	}

out_release:
	nfs_direct_req_release(dreq);
out:
	return result;
}

static void
nfs_direct_write_scan_commit_list(struct inode *inode,
				  struct list_head *list,
				  struct nfs_commit_info *cinfo)
{
	mutex_lock(&NFS_I(cinfo->inode)->commit_mutex);
#ifdef CONFIG_NFS_V4_1
	if (cinfo->ds != NULL && cinfo->ds->nwritten != 0)
		NFS_SERVER(inode)->pnfs_curr_ld->recover_commit_reqs(list, cinfo);
#endif
	nfs_scan_commit_list(&cinfo->mds->list, list, cinfo, 0);
	mutex_unlock(&NFS_I(cinfo->inode)->commit_mutex);
}

static void nfs_direct_write_reschedule(struct nfs_direct_req *dreq)
{
	struct nfs_pageio_descriptor desc;
	struct nfs_page *req, *tmp;
	LIST_HEAD(reqs);
	struct nfs_commit_info cinfo;
	LIST_HEAD(failed);
	int i;

	nfs_init_cinfo_from_dreq(&cinfo, dreq);
	nfs_direct_write_scan_commit_list(dreq->inode, &reqs, &cinfo);

	dreq->count = 0;
	dreq->verf.committed = NFS_INVALID_STABLE_HOW;
	nfs_clear_pnfs_ds_commit_verifiers(&dreq->ds_cinfo);
	for (i = 0; i < dreq->mirror_count; i++)
		dreq->mirrors[i].count = 0;
	get_dreq(dreq);

	nfs_pageio_init_write(&desc, dreq->inode, FLUSH_STABLE, false,
			      &nfs_direct_write_completion_ops);
	desc.pg_dreq = dreq;

	req = nfs_list_entry(reqs.next);
	nfs_direct_setup_mirroring(dreq, &desc, req);
	if (desc.pg_error < 0) {
		list_splice_init(&reqs, &failed);
		goto out_failed;
	}

	list_for_each_entry_safe(req, tmp, &reqs, wb_list) {
		if (!nfs_pageio_add_request(&desc, req)) {
			nfs_list_remove_request(req);
			nfs_list_add_request(req, &failed);
			spin_lock(&cinfo.inode->i_lock);
			dreq->flags = 0;
			if (desc.pg_error < 0)
				dreq->error = desc.pg_error;
			else
				dreq->error = -EIO;
			spin_unlock(&cinfo.inode->i_lock);
		}
		nfs_release_request(req);
	}
	nfs_pageio_complete(&desc);

out_failed:
	while (!list_empty(&failed)) {
		req = nfs_list_entry(failed.next);
		nfs_list_remove_request(req);
		nfs_unlock_and_release_request(req);
	}

	if (put_dreq(dreq))
		nfs_direct_write_complete(dreq);
}

static void nfs_direct_commit_complete(struct nfs_commit_data *data)
{
	struct nfs_direct_req *dreq = data->dreq;
	struct nfs_commit_info cinfo;
	struct nfs_page *req;
	int status = data->task.tk_status;

	nfs_init_cinfo_from_dreq(&cinfo, dreq);
	if (status < 0 || nfs_direct_cmp_commit_data_verf(dreq, data))
		dreq->flags = NFS_ODIRECT_RESCHED_WRITES;

	while (!list_empty(&data->pages)) {
		req = nfs_list_entry(data->pages.next);
		nfs_list_remove_request(req);
		if (dreq->flags == NFS_ODIRECT_RESCHED_WRITES) {
			/* Note the rewrite will go through mds */
			nfs_mark_request_commit(req, NULL, &cinfo, 0);
		} else
			nfs_release_request(req);
		nfs_unlock_and_release_request(req);
	}

	if (atomic_dec_and_test(&cinfo.mds->rpcs_out))
		nfs_direct_write_complete(dreq);
}

static void nfs_direct_resched_write(struct nfs_commit_info *cinfo,
		struct nfs_page *req)
{
	struct nfs_direct_req *dreq = cinfo->dreq;

	spin_lock(&dreq->lock);
	dreq->flags = NFS_ODIRECT_RESCHED_WRITES;
	spin_unlock(&dreq->lock);
	nfs_mark_request_commit(req, NULL, cinfo, 0);
}

static const struct nfs_commit_completion_ops nfs_direct_commit_completion_ops = {
	.completion = nfs_direct_commit_complete,
	.resched_write = nfs_direct_resched_write,
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
			nfs_zap_mapping(dreq->inode, dreq->inode->i_mapping);
			nfs_direct_complete(dreq);
	}
}

static void nfs_direct_write_complete(struct nfs_direct_req *dreq)
{
	schedule_work(&dreq->work); /* Calls nfs_direct_write_schedule_work */
}

static void nfs_direct_write_completion(struct nfs_pgio_header *hdr)
{
	struct nfs_direct_req *dreq = hdr->dreq;
	struct nfs_commit_info cinfo;
	bool request_commit = false;
	struct nfs_page *req = nfs_list_entry(hdr->pages.next);

	if (test_bit(NFS_IOHDR_REDO, &hdr->flags))
		goto out_put;

	nfs_init_cinfo_from_dreq(&cinfo, dreq);

	spin_lock(&dreq->lock);

	if (test_bit(NFS_IOHDR_ERROR, &hdr->flags)) {
		dreq->flags = 0;
		dreq->error = hdr->error;
	}
	if (dreq->error == 0) {
		nfs_direct_good_bytes(dreq, hdr);
		if (nfs_write_need_commit(hdr)) {
			if (dreq->flags == NFS_ODIRECT_RESCHED_WRITES)
				request_commit = true;
			else if (dreq->flags == 0) {
				nfs_direct_set_hdr_verf(dreq, hdr);
				request_commit = true;
				dreq->flags = NFS_ODIRECT_DO_COMMIT;
			} else if (dreq->flags == NFS_ODIRECT_DO_COMMIT) {
				request_commit = true;
				if (nfs_direct_set_or_cmp_hdr_verf(dreq, hdr))
					dreq->flags =
						NFS_ODIRECT_RESCHED_WRITES;
			}
		}
	}
	spin_unlock(&dreq->lock);

	while (!list_empty(&hdr->pages)) {

		req = nfs_list_entry(hdr->pages.next);
		nfs_list_remove_request(req);
		if (request_commit) {
			kref_get(&req->wb_kref);
			nfs_mark_request_commit(req, hdr->lseg, &cinfo,
				hdr->ds_commit_idx);
		}
		nfs_unlock_and_release_request(req);
	}

out_put:
	if (put_dreq(dreq))
		nfs_direct_write_complete(dreq);
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

static void nfs_direct_write_reschedule_io(struct nfs_pgio_header *hdr)
{
	struct nfs_direct_req *dreq = hdr->dreq;

	spin_lock(&dreq->lock);
	if (dreq->error == 0) {
		dreq->flags = NFS_ODIRECT_RESCHED_WRITES;
		/* fake unstable write to let common nfs resend pages */
		hdr->verf.committed = NFS_UNSTABLE;
		hdr->good_bytes = hdr->args.count;
	}
	spin_unlock(&dreq->lock);
}

static const struct nfs_pgio_completion_ops nfs_direct_write_completion_ops = {
	.error_cleanup = nfs_write_sync_pgio_error,
	.init_hdr = nfs_direct_pgio_init,
	.completion = nfs_direct_write_completion,
	.reschedule_io = nfs_direct_write_reschedule_io,
};


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
static ssize_t nfs_direct_write_schedule_iovec(struct nfs_direct_req *dreq,
					       struct iov_iter *iter,
					       loff_t pos)
{
	struct nfs_pageio_descriptor desc;
	struct inode *inode = dreq->inode;
	ssize_t result = 0;
	size_t requested_bytes = 0;
	size_t wsize = max_t(size_t, NFS_SERVER(inode)->wsize, PAGE_SIZE);

	nfs_pageio_init_write(&desc, inode, FLUSH_COND_STABLE, false,
			      &nfs_direct_write_completion_ops);
	desc.pg_dreq = dreq;
	get_dreq(dreq);
	inode_dio_begin(inode);

	NFS_I(inode)->write_io += iov_iter_count(iter);
	while (iov_iter_count(iter)) {
		struct page **pagevec;
		size_t bytes;
		size_t pgbase;
		unsigned npages, i;

		result = iov_iter_get_pages_alloc(iter, &pagevec, 
						  wsize, &pgbase);
		if (result < 0)
			break;

		bytes = result;
		iov_iter_advance(iter, bytes);
		npages = (result + pgbase + PAGE_SIZE - 1) / PAGE_SIZE;
		for (i = 0; i < npages; i++) {
			struct nfs_page *req;
			unsigned int req_len = min_t(size_t, bytes, PAGE_SIZE - pgbase);

			req = nfs_create_request(dreq->ctx, pagevec[i], NULL,
						 pgbase, req_len);
			if (IS_ERR(req)) {
				result = PTR_ERR(req);
				break;
			}

			nfs_direct_setup_mirroring(dreq, &desc, req);
			if (desc.pg_error < 0) {
				nfs_free_request(req);
				result = desc.pg_error;
				break;
			}

			nfs_lock_request(req);
			req->wb_index = pos >> PAGE_SHIFT;
			req->wb_offset = pos & ~PAGE_MASK;
			if (!nfs_pageio_add_request(&desc, req)) {
				result = desc.pg_error;
				nfs_unlock_and_release_request(req);
				break;
			}
			pgbase = 0;
			bytes -= req_len;
			requested_bytes += req_len;
			pos += req_len;
			dreq->bytes_left -= req_len;
		}
		nfs_direct_release_pages(pagevec, npages);
		kvfree(pagevec);
		if (result < 0)
			break;
	}
	nfs_pageio_complete(&desc);

	/*
	 * If no bytes were started, return the error, and let the
	 * generic layer handle the completion.
	 */
	if (requested_bytes == 0) {
		inode_dio_end(inode);
		nfs_direct_req_release(dreq);
		return result < 0 ? result : -EIO;
	}

	if (put_dreq(dreq))
		nfs_direct_write_complete(dreq);
	return requested_bytes;
}

/**
 * nfs_file_direct_write - file direct write operation for NFS files
 * @iocb: target I/O control block
 * @iter: vector of user buffers from which to write data
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
ssize_t nfs_file_direct_write(struct kiocb *iocb, struct iov_iter *iter)
{
	ssize_t result = -EINVAL, requested;
	size_t count;
	struct file *file = iocb->ki_filp;
	struct address_space *mapping = file->f_mapping;
	struct inode *inode = mapping->host;
	struct nfs_direct_req *dreq;
	struct nfs_lock_context *l_ctx;
	loff_t pos, end;

	dfprintk(FILE, "NFS: direct write(%pD2, %zd@%Ld)\n",
		file, iov_iter_count(iter), (long long) iocb->ki_pos);

	result = generic_write_checks(iocb, iter);
	if (result <= 0)
		return result;
	count = result;
	nfs_add_stats(mapping->host, NFSIOS_DIRECTWRITTENBYTES, count);

	pos = iocb->ki_pos;
	end = (pos + iov_iter_count(iter) - 1) >> PAGE_SHIFT;

	task_io_account_write(count);

	result = -ENOMEM;
	dreq = nfs_direct_req_alloc();
	if (!dreq)
		goto out;

	dreq->inode = inode;
	dreq->bytes_left = dreq->max_count = count;
	dreq->io_start = pos;
	dreq->ctx = get_nfs_open_context(nfs_file_open_context(iocb->ki_filp));
	l_ctx = nfs_get_lock_context(dreq->ctx);
	if (IS_ERR(l_ctx)) {
		result = PTR_ERR(l_ctx);
		goto out_release;
	}
	dreq->l_ctx = l_ctx;
	if (!is_sync_kiocb(iocb))
		dreq->iocb = iocb;

	nfs_start_io_direct(inode);

	requested = nfs_direct_write_schedule_iovec(dreq, iter, pos);

	if (mapping->nrpages) {
		invalidate_inode_pages2_range(mapping,
					      pos >> PAGE_SHIFT, end);
	}

	nfs_end_io_direct(inode);

	if (requested > 0) {
		result = nfs_direct_wait(dreq);
		if (result > 0) {
			requested -= result;
			iocb->ki_pos = pos + result;
			/* XXX: should check the generic_write_sync retval */
			generic_write_sync(iocb, result);
		}
		iov_iter_revert(iter, requested);
	} else {
		result = requested;
	}
out_release:
	nfs_direct_req_release(dreq);
out:
	return result;
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
