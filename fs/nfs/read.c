/*
 * linux/fs/nfs/read.c
 *
 * Block I/O for NFS
 *
 * Partial copy of Linus' read cache modifications to fs/nfs/file.c
 * modified for async RPC by okir@monad.swb.de
 */

#include <linux/time.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/sunrpc/clnt.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_page.h>
#include <linux/module.h>

#include "nfs4_fs.h"
#include "internal.h"
#include "iostat.h"
#include "fscache.h"

#define NFSDBG_FACILITY		NFSDBG_PAGECACHE

static const struct nfs_pageio_ops nfs_pageio_read_ops;
static const struct rpc_call_ops nfs_read_common_ops;
static const struct nfs_pgio_completion_ops nfs_async_read_completion_ops;

static struct kmem_cache *nfs_rdata_cachep;

struct nfs_read_header *nfs_readhdr_alloc(void)
{
	struct nfs_read_header *rhdr;

	rhdr = kmem_cache_zalloc(nfs_rdata_cachep, GFP_KERNEL);
	if (rhdr) {
		struct nfs_pgio_header *hdr = &rhdr->header;

		INIT_LIST_HEAD(&hdr->pages);
		INIT_LIST_HEAD(&hdr->rpc_list);
		spin_lock_init(&hdr->lock);
		atomic_set(&hdr->refcnt, 0);
	}
	return rhdr;
}
EXPORT_SYMBOL_GPL(nfs_readhdr_alloc);

static struct nfs_read_data *nfs_readdata_alloc(struct nfs_pgio_header *hdr,
						unsigned int pagecount)
{
	struct nfs_read_data *data, *prealloc;

	prealloc = &container_of(hdr, struct nfs_read_header, header)->rpc_data;
	if (prealloc->header == NULL)
		data = prealloc;
	else
		data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		goto out;

	if (nfs_pgarray_set(&data->pages, pagecount)) {
		data->header = hdr;
		atomic_inc(&hdr->refcnt);
	} else {
		if (data != prealloc)
			kfree(data);
		data = NULL;
	}
out:
	return data;
}

void nfs_readhdr_free(struct nfs_pgio_header *hdr)
{
	struct nfs_read_header *rhdr = container_of(hdr, struct nfs_read_header, header);

	kmem_cache_free(nfs_rdata_cachep, rhdr);
}
EXPORT_SYMBOL_GPL(nfs_readhdr_free);

void nfs_readdata_release(struct nfs_read_data *rdata)
{
	struct nfs_pgio_header *hdr = rdata->header;
	struct nfs_read_header *read_header = container_of(hdr, struct nfs_read_header, header);

	put_nfs_open_context(rdata->args.context);
	if (rdata->pages.pagevec != rdata->pages.page_array)
		kfree(rdata->pages.pagevec);
	if (rdata == &read_header->rpc_data) {
		rdata->header = NULL;
		rdata = NULL;
	}
	if (atomic_dec_and_test(&hdr->refcnt))
		hdr->completion_ops->completion(hdr);
	/* Note: we only free the rpc_task after callbacks are done.
	 * See the comment in rpc_free_task() for why
	 */
	kfree(rdata);
}
EXPORT_SYMBOL_GPL(nfs_readdata_release);

static
int nfs_return_empty_page(struct page *page)
{
	zero_user(page, 0, PAGE_CACHE_SIZE);
	SetPageUptodate(page);
	unlock_page(page);
	return 0;
}

void nfs_pageio_init_read(struct nfs_pageio_descriptor *pgio,
			      struct inode *inode,
			      const struct nfs_pgio_completion_ops *compl_ops)
{
	nfs_pageio_init(pgio, inode, &nfs_pageio_read_ops, compl_ops,
			NFS_SERVER(inode)->rsize, 0);
}
EXPORT_SYMBOL_GPL(nfs_pageio_init_read);

void nfs_pageio_reset_read_mds(struct nfs_pageio_descriptor *pgio)
{
	pgio->pg_ops = &nfs_pageio_read_ops;
	pgio->pg_bsize = NFS_SERVER(pgio->pg_inode)->rsize;
}
EXPORT_SYMBOL_GPL(nfs_pageio_reset_read_mds);

int nfs_readpage_async(struct nfs_open_context *ctx, struct inode *inode,
		       struct page *page)
{
	struct nfs_page	*new;
	unsigned int len;
	struct nfs_pageio_descriptor pgio;

	len = nfs_page_length(page);
	if (len == 0)
		return nfs_return_empty_page(page);
	new = nfs_create_request(ctx, inode, page, 0, len);
	if (IS_ERR(new)) {
		unlock_page(page);
		return PTR_ERR(new);
	}
	if (len < PAGE_CACHE_SIZE)
		zero_user_segment(page, len, PAGE_CACHE_SIZE);

	NFS_PROTO(inode)->read_pageio_init(&pgio, inode, &nfs_async_read_completion_ops);
	nfs_pageio_add_request(&pgio, new);
	nfs_pageio_complete(&pgio);
	NFS_I(inode)->read_io += pgio.pg_bytes_written;
	return 0;
}

static void nfs_readpage_release(struct nfs_page *req)
{
	struct inode *d_inode = req->wb_context->dentry->d_inode;

	if (PageUptodate(req->wb_page))
		nfs_readpage_to_fscache(d_inode, req->wb_page, 0);

	unlock_page(req->wb_page);

	dprintk("NFS: read done (%s/%Lu %d@%Ld)\n",
			req->wb_context->dentry->d_inode->i_sb->s_id,
			(unsigned long long)NFS_FILEID(req->wb_context->dentry->d_inode),
			req->wb_bytes,
			(long long)req_offset(req));
	nfs_release_request(req);
}

/* Note io was page aligned */
static void nfs_read_completion(struct nfs_pgio_header *hdr)
{
	unsigned long bytes = 0;

	if (test_bit(NFS_IOHDR_REDO, &hdr->flags))
		goto out;
	while (!list_empty(&hdr->pages)) {
		struct nfs_page *req = nfs_list_entry(hdr->pages.next);
		struct page *page = req->wb_page;

		if (test_bit(NFS_IOHDR_EOF, &hdr->flags)) {
			if (bytes > hdr->good_bytes)
				zero_user(page, 0, PAGE_SIZE);
			else if (hdr->good_bytes - bytes < PAGE_SIZE)
				zero_user_segment(page,
					hdr->good_bytes & ~PAGE_MASK,
					PAGE_SIZE);
		}
		bytes += req->wb_bytes;
		if (test_bit(NFS_IOHDR_ERROR, &hdr->flags)) {
			if (bytes <= hdr->good_bytes)
				SetPageUptodate(page);
		} else
			SetPageUptodate(page);
		nfs_list_remove_request(req);
		nfs_readpage_release(req);
	}
out:
	hdr->release(hdr);
}

int nfs_initiate_read(struct rpc_clnt *clnt,
		      struct nfs_read_data *data,
		      const struct rpc_call_ops *call_ops, int flags)
{
	struct inode *inode = data->header->inode;
	int swap_flags = IS_SWAPFILE(inode) ? NFS_RPC_SWAPFLAGS : 0;
	struct rpc_task *task;
	struct rpc_message msg = {
		.rpc_argp = &data->args,
		.rpc_resp = &data->res,
		.rpc_cred = data->header->cred,
	};
	struct rpc_task_setup task_setup_data = {
		.task = &data->task,
		.rpc_client = clnt,
		.rpc_message = &msg,
		.callback_ops = call_ops,
		.callback_data = data,
		.workqueue = nfsiod_workqueue,
		.flags = RPC_TASK_ASYNC | swap_flags | flags,
	};

	/* Set up the initial task struct. */
	NFS_PROTO(inode)->read_setup(data, &msg);

	dprintk("NFS: %5u initiated read call (req %s/%llu, %u bytes @ "
			"offset %llu)\n",
			data->task.tk_pid,
			inode->i_sb->s_id,
			(unsigned long long)NFS_FILEID(inode),
			data->args.count,
			(unsigned long long)data->args.offset);

	task = rpc_run_task(&task_setup_data);
	if (IS_ERR(task))
		return PTR_ERR(task);
	rpc_put_task(task);
	return 0;
}
EXPORT_SYMBOL_GPL(nfs_initiate_read);

/*
 * Set up the NFS read request struct
 */
static void nfs_read_rpcsetup(struct nfs_read_data *data,
		unsigned int count, unsigned int offset)
{
	struct nfs_page *req = data->header->req;

	data->args.fh     = NFS_FH(data->header->inode);
	data->args.offset = req_offset(req) + offset;
	data->args.pgbase = req->wb_pgbase + offset;
	data->args.pages  = data->pages.pagevec;
	data->args.count  = count;
	data->args.context = get_nfs_open_context(req->wb_context);
	data->args.lock_context = req->wb_lock_context;

	data->res.fattr   = &data->fattr;
	data->res.count   = count;
	data->res.eof     = 0;
	nfs_fattr_init(&data->fattr);
}

static int nfs_do_read(struct nfs_read_data *data,
		const struct rpc_call_ops *call_ops)
{
	struct inode *inode = data->header->inode;

	return nfs_initiate_read(NFS_CLIENT(inode), data, call_ops, 0);
}

static int
nfs_do_multiple_reads(struct list_head *head,
		const struct rpc_call_ops *call_ops)
{
	struct nfs_read_data *data;
	int ret = 0;

	while (!list_empty(head)) {
		int ret2;

		data = list_first_entry(head, struct nfs_read_data, list);
		list_del_init(&data->list);

		ret2 = nfs_do_read(data, call_ops);
		if (ret == 0)
			ret = ret2;
	}
	return ret;
}

static void
nfs_async_read_error(struct list_head *head)
{
	struct nfs_page	*req;

	while (!list_empty(head)) {
		req = nfs_list_entry(head->next);
		nfs_list_remove_request(req);
		nfs_readpage_release(req);
	}
}

static const struct nfs_pgio_completion_ops nfs_async_read_completion_ops = {
	.error_cleanup = nfs_async_read_error,
	.completion = nfs_read_completion,
};

static void nfs_pagein_error(struct nfs_pageio_descriptor *desc,
		struct nfs_pgio_header *hdr)
{
	set_bit(NFS_IOHDR_REDO, &hdr->flags);
	while (!list_empty(&hdr->rpc_list)) {
		struct nfs_read_data *data = list_first_entry(&hdr->rpc_list,
				struct nfs_read_data, list);
		list_del(&data->list);
		nfs_readdata_release(data);
	}
	desc->pg_completion_ops->error_cleanup(&desc->pg_list);
}

/*
 * Generate multiple requests to fill a single page.
 *
 * We optimize to reduce the number of read operations on the wire.  If we
 * detect that we're reading a page, or an area of a page, that is past the
 * end of file, we do not generate NFS read operations but just clear the
 * parts of the page that would have come back zero from the server anyway.
 *
 * We rely on the cached value of i_size to make this determination; another
 * client can fill pages on the server past our cached end-of-file, but we
 * won't see the new data until our attribute cache is updated.  This is more
 * or less conventional NFS client behavior.
 */
static int nfs_pagein_multi(struct nfs_pageio_descriptor *desc,
			    struct nfs_pgio_header *hdr)
{
	struct nfs_page *req = hdr->req;
	struct page *page = req->wb_page;
	struct nfs_read_data *data;
	size_t rsize = desc->pg_bsize, nbytes;
	unsigned int offset;

	offset = 0;
	nbytes = desc->pg_count;
	do {
		size_t len = min(nbytes,rsize);

		data = nfs_readdata_alloc(hdr, 1);
		if (!data) {
			nfs_pagein_error(desc, hdr);
			return -ENOMEM;
		}
		data->pages.pagevec[0] = page;
		nfs_read_rpcsetup(data, len, offset);
		list_add(&data->list, &hdr->rpc_list);
		nbytes -= len;
		offset += len;
	} while (nbytes != 0);

	nfs_list_remove_request(req);
	nfs_list_add_request(req, &hdr->pages);
	desc->pg_rpc_callops = &nfs_read_common_ops;
	return 0;
}

static int nfs_pagein_one(struct nfs_pageio_descriptor *desc,
			  struct nfs_pgio_header *hdr)
{
	struct nfs_page		*req;
	struct page		**pages;
	struct nfs_read_data    *data;
	struct list_head *head = &desc->pg_list;

	data = nfs_readdata_alloc(hdr, nfs_page_array_len(desc->pg_base,
							  desc->pg_count));
	if (!data) {
		nfs_pagein_error(desc, hdr);
		return -ENOMEM;
	}

	pages = data->pages.pagevec;
	while (!list_empty(head)) {
		req = nfs_list_entry(head->next);
		nfs_list_remove_request(req);
		nfs_list_add_request(req, &hdr->pages);
		*pages++ = req->wb_page;
	}

	nfs_read_rpcsetup(data, desc->pg_count, 0);
	list_add(&data->list, &hdr->rpc_list);
	desc->pg_rpc_callops = &nfs_read_common_ops;
	return 0;
}

int nfs_generic_pagein(struct nfs_pageio_descriptor *desc,
		       struct nfs_pgio_header *hdr)
{
	if (desc->pg_bsize < PAGE_CACHE_SIZE)
		return nfs_pagein_multi(desc, hdr);
	return nfs_pagein_one(desc, hdr);
}
EXPORT_SYMBOL_GPL(nfs_generic_pagein);

static int nfs_generic_pg_readpages(struct nfs_pageio_descriptor *desc)
{
	struct nfs_read_header *rhdr;
	struct nfs_pgio_header *hdr;
	int ret;

	rhdr = nfs_readhdr_alloc();
	if (!rhdr) {
		desc->pg_completion_ops->error_cleanup(&desc->pg_list);
		return -ENOMEM;
	}
	hdr = &rhdr->header;
	nfs_pgheader_init(desc, hdr, nfs_readhdr_free);
	atomic_inc(&hdr->refcnt);
	ret = nfs_generic_pagein(desc, hdr);
	if (ret == 0)
		ret = nfs_do_multiple_reads(&hdr->rpc_list,
					    desc->pg_rpc_callops);
	if (atomic_dec_and_test(&hdr->refcnt))
		hdr->completion_ops->completion(hdr);
	return ret;
}

static const struct nfs_pageio_ops nfs_pageio_read_ops = {
	.pg_test = nfs_generic_pg_test,
	.pg_doio = nfs_generic_pg_readpages,
};

/*
 * This is the callback from RPC telling us whether a reply was
 * received or some error occurred (timeout or socket shutdown).
 */
int nfs_readpage_result(struct rpc_task *task, struct nfs_read_data *data)
{
	struct inode *inode = data->header->inode;
	int status;

	dprintk("NFS: %s: %5u, (status %d)\n", __func__, task->tk_pid,
			task->tk_status);

	status = NFS_PROTO(inode)->read_done(task, data);
	if (status != 0)
		return status;

	nfs_add_stats(inode, NFSIOS_SERVERREADBYTES, data->res.count);

	if (task->tk_status == -ESTALE) {
		set_bit(NFS_INO_STALE, &NFS_I(inode)->flags);
		nfs_mark_for_revalidate(inode);
	}
	return 0;
}

static void nfs_readpage_retry(struct rpc_task *task, struct nfs_read_data *data)
{
	struct nfs_readargs *argp = &data->args;
	struct nfs_readres *resp = &data->res;

	/* This is a short read! */
	nfs_inc_stats(data->header->inode, NFSIOS_SHORTREAD);
	/* Has the server at least made some progress? */
	if (resp->count == 0) {
		nfs_set_pgio_error(data->header, -EIO, argp->offset);
		return;
	}
	/* Yes, so retry the read at the end of the data */
	data->mds_offset += resp->count;
	argp->offset += resp->count;
	argp->pgbase += resp->count;
	argp->count -= resp->count;
	rpc_restart_call_prepare(task);
}

static void nfs_readpage_result_common(struct rpc_task *task, void *calldata)
{
	struct nfs_read_data *data = calldata;
	struct nfs_pgio_header *hdr = data->header;

	/* Note the only returns of nfs_readpage_result are 0 and -EAGAIN */
	if (nfs_readpage_result(task, data) != 0)
		return;
	if (task->tk_status < 0)
		nfs_set_pgio_error(hdr, task->tk_status, data->args.offset);
	else if (data->res.eof) {
		loff_t bound;

		bound = data->args.offset + data->res.count;
		spin_lock(&hdr->lock);
		if (bound < hdr->io_start + hdr->good_bytes) {
			set_bit(NFS_IOHDR_EOF, &hdr->flags);
			clear_bit(NFS_IOHDR_ERROR, &hdr->flags);
			hdr->good_bytes = bound - hdr->io_start;
		}
		spin_unlock(&hdr->lock);
	} else if (data->res.count != data->args.count)
		nfs_readpage_retry(task, data);
}

static void nfs_readpage_release_common(void *calldata)
{
	nfs_readdata_release(calldata);
}

void nfs_read_prepare(struct rpc_task *task, void *calldata)
{
	struct nfs_read_data *data = calldata;
	int err;
	err = NFS_PROTO(data->header->inode)->read_rpc_prepare(task, data);
	if (err)
		rpc_exit(task, err);
}

static const struct rpc_call_ops nfs_read_common_ops = {
	.rpc_call_prepare = nfs_read_prepare,
	.rpc_call_done = nfs_readpage_result_common,
	.rpc_release = nfs_readpage_release_common,
};

/*
 * Read a page over NFS.
 * We read the page synchronously in the following case:
 *  -	The error flag is set for this page. This happens only when a
 *	previous async read operation failed.
 */
int nfs_readpage(struct file *file, struct page *page)
{
	struct nfs_open_context *ctx;
	struct inode *inode = page_file_mapping(page)->host;
	int		error;

	dprintk("NFS: nfs_readpage (%p %ld@%lu)\n",
		page, PAGE_CACHE_SIZE, page_file_index(page));
	nfs_inc_stats(inode, NFSIOS_VFSREADPAGE);
	nfs_add_stats(inode, NFSIOS_READPAGES, 1);

	/*
	 * Try to flush any pending writes to the file..
	 *
	 * NOTE! Because we own the page lock, there cannot
	 * be any new pending writes generated at this point
	 * for this page (other pages can be written to).
	 */
	error = nfs_wb_page(inode, page);
	if (error)
		goto out_unlock;
	if (PageUptodate(page))
		goto out_unlock;

	error = -ESTALE;
	if (NFS_STALE(inode))
		goto out_unlock;

	if (file == NULL) {
		error = -EBADF;
		ctx = nfs_find_open_context(inode, NULL, FMODE_READ);
		if (ctx == NULL)
			goto out_unlock;
	} else
		ctx = get_nfs_open_context(nfs_file_open_context(file));

	if (!IS_SYNC(inode)) {
		error = nfs_readpage_from_fscache(ctx, inode, page);
		if (error == 0)
			goto out;
	}

	error = nfs_readpage_async(ctx, inode, page);

out:
	put_nfs_open_context(ctx);
	return error;
out_unlock:
	unlock_page(page);
	return error;
}

struct nfs_readdesc {
	struct nfs_pageio_descriptor *pgio;
	struct nfs_open_context *ctx;
};

static int
readpage_async_filler(void *data, struct page *page)
{
	struct nfs_readdesc *desc = (struct nfs_readdesc *)data;
	struct inode *inode = page_file_mapping(page)->host;
	struct nfs_page *new;
	unsigned int len;
	int error;

	len = nfs_page_length(page);
	if (len == 0)
		return nfs_return_empty_page(page);

	new = nfs_create_request(desc->ctx, inode, page, 0, len);
	if (IS_ERR(new))
		goto out_error;

	if (len < PAGE_CACHE_SIZE)
		zero_user_segment(page, len, PAGE_CACHE_SIZE);
	if (!nfs_pageio_add_request(desc->pgio, new)) {
		error = desc->pgio->pg_error;
		goto out_unlock;
	}
	return 0;
out_error:
	error = PTR_ERR(new);
out_unlock:
	unlock_page(page);
	return error;
}

int nfs_readpages(struct file *filp, struct address_space *mapping,
		struct list_head *pages, unsigned nr_pages)
{
	struct nfs_pageio_descriptor pgio;
	struct nfs_readdesc desc = {
		.pgio = &pgio,
	};
	struct inode *inode = mapping->host;
	unsigned long npages;
	int ret = -ESTALE;

	dprintk("NFS: nfs_readpages (%s/%Lu %d)\n",
			inode->i_sb->s_id,
			(unsigned long long)NFS_FILEID(inode),
			nr_pages);
	nfs_inc_stats(inode, NFSIOS_VFSREADPAGES);

	if (NFS_STALE(inode))
		goto out;

	if (filp == NULL) {
		desc.ctx = nfs_find_open_context(inode, NULL, FMODE_READ);
		if (desc.ctx == NULL)
			return -EBADF;
	} else
		desc.ctx = get_nfs_open_context(nfs_file_open_context(filp));

	/* attempt to read as many of the pages as possible from the cache
	 * - this returns -ENOBUFS immediately if the cookie is negative
	 */
	ret = nfs_readpages_from_fscache(desc.ctx, inode, mapping,
					 pages, &nr_pages);
	if (ret == 0)
		goto read_complete; /* all pages were read */

	NFS_PROTO(inode)->read_pageio_init(&pgio, inode, &nfs_async_read_completion_ops);

	ret = read_cache_pages(mapping, pages, readpage_async_filler, &desc);

	nfs_pageio_complete(&pgio);
	NFS_I(inode)->read_io += pgio.pg_bytes_written;
	npages = (pgio.pg_bytes_written + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;
	nfs_add_stats(inode, NFSIOS_READPAGES, npages);
read_complete:
	put_nfs_open_context(desc.ctx);
out:
	return ret;
}

int __init nfs_init_readpagecache(void)
{
	nfs_rdata_cachep = kmem_cache_create("nfs_read_data",
					     sizeof(struct nfs_read_header),
					     0, SLAB_HWCACHE_ALIGN,
					     NULL);
	if (nfs_rdata_cachep == NULL)
		return -ENOMEM;

	return 0;
}

void nfs_destroy_readpagecache(void)
{
	kmem_cache_destroy(nfs_rdata_cachep);
}
