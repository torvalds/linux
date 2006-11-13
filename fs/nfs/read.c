/*
 * linux/fs/nfs/read.c
 *
 * Block I/O for NFS
 *
 * Partial copy of Linus' read cache modifications to fs/nfs/file.c
 * modified for async RPC by okir@monad.swb.de
 *
 * We do an ugly hack here in order to return proper error codes to the
 * user program when a read request failed: since generic_file_read
 * only checks the return value of inode->i_op->readpage() which is always 0
 * for async RPC, we set the error bit of the page to 1 when an error occurs,
 * and make nfs_readpage transmit requests synchronously when encountering this.
 * This is only a small problem, though, since we now retry all operations
 * within the RPC code when root squashing is suspected.
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
#include <linux/smp_lock.h>

#include <asm/system.h>

#include "iostat.h"

#define NFSDBG_FACILITY		NFSDBG_PAGECACHE

static int nfs_pagein_one(struct list_head *, struct inode *);
static const struct rpc_call_ops nfs_read_partial_ops;
static const struct rpc_call_ops nfs_read_full_ops;

static kmem_cache_t *nfs_rdata_cachep;
static mempool_t *nfs_rdata_mempool;

#define MIN_POOL_READ	(32)

struct nfs_read_data *nfs_readdata_alloc(size_t len)
{
	unsigned int pagecount = (len + PAGE_SIZE - 1) >> PAGE_SHIFT;
	struct nfs_read_data *p = mempool_alloc(nfs_rdata_mempool, SLAB_NOFS);

	if (p) {
		memset(p, 0, sizeof(*p));
		INIT_LIST_HEAD(&p->pages);
		p->npages = pagecount;
		if (pagecount <= ARRAY_SIZE(p->page_array))
			p->pagevec = p->page_array;
		else {
			p->pagevec = kcalloc(pagecount, sizeof(struct page *), GFP_NOFS);
			if (!p->pagevec) {
				mempool_free(p, nfs_rdata_mempool);
				p = NULL;
			}
		}
	}
	return p;
}

static void nfs_readdata_rcu_free(struct rcu_head *head)
{
	struct nfs_read_data *p = container_of(head, struct nfs_read_data, task.u.tk_rcu);
	if (p && (p->pagevec != &p->page_array[0]))
		kfree(p->pagevec);
	mempool_free(p, nfs_rdata_mempool);
}

static void nfs_readdata_free(struct nfs_read_data *rdata)
{
	call_rcu_bh(&rdata->task.u.tk_rcu, nfs_readdata_rcu_free);
}

void nfs_readdata_release(void *data)
{
        nfs_readdata_free(data);
}

static
unsigned int nfs_page_length(struct inode *inode, struct page *page)
{
	loff_t i_size = i_size_read(inode);
	unsigned long idx;

	if (i_size <= 0)
		return 0;
	idx = (i_size - 1) >> PAGE_CACHE_SHIFT;
	if (page->index > idx)
		return 0;
	if (page->index != idx)
		return PAGE_CACHE_SIZE;
	return 1 + ((i_size - 1) & (PAGE_CACHE_SIZE - 1));
}

static
int nfs_return_empty_page(struct page *page)
{
	memclear_highpage_flush(page, 0, PAGE_CACHE_SIZE);
	SetPageUptodate(page);
	unlock_page(page);
	return 0;
}

static void nfs_readpage_truncate_uninitialised_page(struct nfs_read_data *data)
{
	unsigned int remainder = data->args.count - data->res.count;
	unsigned int base = data->args.pgbase + data->res.count;
	unsigned int pglen;
	struct page **pages;

	if (data->res.eof == 0 || remainder == 0)
		return;
	/*
	 * Note: "remainder" can never be negative, since we check for
	 * 	this in the XDR code.
	 */
	pages = &data->args.pages[base >> PAGE_CACHE_SHIFT];
	base &= ~PAGE_CACHE_MASK;
	pglen = PAGE_CACHE_SIZE - base;
	for (;;) {
		if (remainder <= pglen) {
			memclear_highpage_flush(*pages, base, remainder);
			break;
		}
		memclear_highpage_flush(*pages, base, pglen);
		pages++;
		remainder -= pglen;
		pglen = PAGE_CACHE_SIZE;
		base = 0;
	}
}

/*
 * Read a page synchronously.
 */
static int nfs_readpage_sync(struct nfs_open_context *ctx, struct inode *inode,
		struct page *page)
{
	unsigned int	rsize = NFS_SERVER(inode)->rsize;
	unsigned int	count = PAGE_CACHE_SIZE;
	int		result;
	struct nfs_read_data *rdata;

	rdata = nfs_readdata_alloc(count);
	if (!rdata)
		return -ENOMEM;

	memset(rdata, 0, sizeof(*rdata));
	rdata->flags = (IS_SWAPFILE(inode)? NFS_RPC_SWAPFLAGS : 0);
	rdata->cred = ctx->cred;
	rdata->inode = inode;
	INIT_LIST_HEAD(&rdata->pages);
	rdata->args.fh = NFS_FH(inode);
	rdata->args.context = ctx;
	rdata->args.pages = &page;
	rdata->args.pgbase = 0UL;
	rdata->args.count = rsize;
	rdata->res.fattr = &rdata->fattr;

	dprintk("NFS: nfs_readpage_sync(%p)\n", page);

	/*
	 * This works now because the socket layer never tries to DMA
	 * into this buffer directly.
	 */
	do {
		if (count < rsize)
			rdata->args.count = count;
		rdata->res.count = rdata->args.count;
		rdata->args.offset = page_offset(page) + rdata->args.pgbase;

		dprintk("NFS: nfs_proc_read(%s, (%s/%Ld), %Lu, %u)\n",
			NFS_SERVER(inode)->nfs_client->cl_hostname,
			inode->i_sb->s_id,
			(long long)NFS_FILEID(inode),
			(unsigned long long)rdata->args.pgbase,
			rdata->args.count);

		lock_kernel();
		result = NFS_PROTO(inode)->read(rdata);
		unlock_kernel();

		/*
		 * Even if we had a partial success we can't mark the page
		 * cache valid.
		 */
		if (result < 0) {
			if (result == -EISDIR)
				result = -EINVAL;
			goto io_error;
		}
		count -= result;
		rdata->args.pgbase += result;
		nfs_add_stats(inode, NFSIOS_SERVERREADBYTES, result);

		/* Note: result == 0 should only happen if we're caching
		 * a write that extends the file and punches a hole.
		 */
		if (rdata->res.eof != 0 || result == 0)
			break;
	} while (count);
	spin_lock(&inode->i_lock);
	NFS_I(inode)->cache_validity |= NFS_INO_INVALID_ATIME;
	spin_unlock(&inode->i_lock);

	if (rdata->res.eof || rdata->res.count == rdata->args.count) {
		SetPageUptodate(page);
		if (rdata->res.eof && count != 0)
			memclear_highpage_flush(page, rdata->args.pgbase, count);
	}
	result = 0;

io_error:
	unlock_page(page);
	nfs_readdata_free(rdata);
	return result;
}

static int nfs_readpage_async(struct nfs_open_context *ctx, struct inode *inode,
		struct page *page)
{
	LIST_HEAD(one_request);
	struct nfs_page	*new;
	unsigned int len;

	len = nfs_page_length(inode, page);
	if (len == 0)
		return nfs_return_empty_page(page);
	new = nfs_create_request(ctx, inode, page, 0, len);
	if (IS_ERR(new)) {
		unlock_page(page);
		return PTR_ERR(new);
	}
	if (len < PAGE_CACHE_SIZE)
		memclear_highpage_flush(page, len, PAGE_CACHE_SIZE - len);

	nfs_list_add_request(new, &one_request);
	nfs_pagein_one(&one_request, inode);
	return 0;
}

static void nfs_readpage_release(struct nfs_page *req)
{
	unlock_page(req->wb_page);

	dprintk("NFS: read done (%s/%Ld %d@%Ld)\n",
			req->wb_context->dentry->d_inode->i_sb->s_id,
			(long long)NFS_FILEID(req->wb_context->dentry->d_inode),
			req->wb_bytes,
			(long long)req_offset(req));
	nfs_clear_request(req);
	nfs_release_request(req);
}

/*
 * Set up the NFS read request struct
 */
static void nfs_read_rpcsetup(struct nfs_page *req, struct nfs_read_data *data,
		const struct rpc_call_ops *call_ops,
		unsigned int count, unsigned int offset)
{
	struct inode		*inode;
	int flags;

	data->req	  = req;
	data->inode	  = inode = req->wb_context->dentry->d_inode;
	data->cred	  = req->wb_context->cred;

	data->args.fh     = NFS_FH(inode);
	data->args.offset = req_offset(req) + offset;
	data->args.pgbase = req->wb_pgbase + offset;
	data->args.pages  = data->pagevec;
	data->args.count  = count;
	data->args.context = req->wb_context;

	data->res.fattr   = &data->fattr;
	data->res.count   = count;
	data->res.eof     = 0;
	nfs_fattr_init(&data->fattr);

	/* Set up the initial task struct. */
	flags = RPC_TASK_ASYNC | (IS_SWAPFILE(inode)? NFS_RPC_SWAPFLAGS : 0);
	rpc_init_task(&data->task, NFS_CLIENT(inode), flags, call_ops, data);
	NFS_PROTO(inode)->read_setup(data);

	data->task.tk_cookie = (unsigned long)inode;

	dprintk("NFS: %4d initiated read call (req %s/%Ld, %u bytes @ offset %Lu)\n",
			data->task.tk_pid,
			inode->i_sb->s_id,
			(long long)NFS_FILEID(inode),
			count,
			(unsigned long long)data->args.offset);
}

static void
nfs_async_read_error(struct list_head *head)
{
	struct nfs_page	*req;

	while (!list_empty(head)) {
		req = nfs_list_entry(head->next);
		nfs_list_remove_request(req);
		SetPageError(req->wb_page);
		nfs_readpage_release(req);
	}
}

/*
 * Start an async read operation
 */
static void nfs_execute_read(struct nfs_read_data *data)
{
	struct rpc_clnt *clnt = NFS_CLIENT(data->inode);
	sigset_t oldset;

	rpc_clnt_sigmask(clnt, &oldset);
	lock_kernel();
	rpc_execute(&data->task);
	unlock_kernel();
	rpc_clnt_sigunmask(clnt, &oldset);
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
static int nfs_pagein_multi(struct list_head *head, struct inode *inode)
{
	struct nfs_page *req = nfs_list_entry(head->next);
	struct page *page = req->wb_page;
	struct nfs_read_data *data;
	size_t rsize = NFS_SERVER(inode)->rsize, nbytes;
	unsigned int offset;
	int requests = 0;
	LIST_HEAD(list);

	nfs_list_remove_request(req);

	nbytes = req->wb_bytes;
	do {
		size_t len = min(nbytes,rsize);

		data = nfs_readdata_alloc(len);
		if (!data)
			goto out_bad;
		INIT_LIST_HEAD(&data->pages);
		list_add(&data->pages, &list);
		requests++;
		nbytes -= len;
	} while(nbytes != 0);
	atomic_set(&req->wb_complete, requests);

	ClearPageError(page);
	offset = 0;
	nbytes = req->wb_bytes;
	do {
		data = list_entry(list.next, struct nfs_read_data, pages);
		list_del_init(&data->pages);

		data->pagevec[0] = page;

		if (nbytes > rsize) {
			nfs_read_rpcsetup(req, data, &nfs_read_partial_ops,
					rsize, offset);
			offset += rsize;
			nbytes -= rsize;
		} else {
			nfs_read_rpcsetup(req, data, &nfs_read_partial_ops,
					nbytes, offset);
			nbytes = 0;
		}
		nfs_execute_read(data);
	} while (nbytes != 0);

	return 0;

out_bad:
	while (!list_empty(&list)) {
		data = list_entry(list.next, struct nfs_read_data, pages);
		list_del(&data->pages);
		nfs_readdata_free(data);
	}
	SetPageError(page);
	nfs_readpage_release(req);
	return -ENOMEM;
}

static int nfs_pagein_one(struct list_head *head, struct inode *inode)
{
	struct nfs_page		*req;
	struct page		**pages;
	struct nfs_read_data	*data;
	unsigned int		count;

	if (NFS_SERVER(inode)->rsize < PAGE_CACHE_SIZE)
		return nfs_pagein_multi(head, inode);

	data = nfs_readdata_alloc(NFS_SERVER(inode)->rsize);
	if (!data)
		goto out_bad;

	INIT_LIST_HEAD(&data->pages);
	pages = data->pagevec;
	count = 0;
	while (!list_empty(head)) {
		req = nfs_list_entry(head->next);
		nfs_list_remove_request(req);
		nfs_list_add_request(req, &data->pages);
		ClearPageError(req->wb_page);
		*pages++ = req->wb_page;
		count += req->wb_bytes;
	}
	req = nfs_list_entry(data->pages.next);

	nfs_read_rpcsetup(req, data, &nfs_read_full_ops, count, 0);

	nfs_execute_read(data);
	return 0;
out_bad:
	nfs_async_read_error(head);
	return -ENOMEM;
}

static int
nfs_pagein_list(struct list_head *head, int rpages)
{
	LIST_HEAD(one_request);
	struct nfs_page		*req;
	int			error = 0;
	unsigned int		pages = 0;

	while (!list_empty(head)) {
		pages += nfs_coalesce_requests(head, &one_request, rpages);
		req = nfs_list_entry(one_request.next);
		error = nfs_pagein_one(&one_request, req->wb_context->dentry->d_inode);
		if (error < 0)
			break;
	}
	if (error >= 0)
		return pages;

	nfs_async_read_error(head);
	return error;
}

/*
 * Handle a read reply that fills part of a page.
 */
static void nfs_readpage_result_partial(struct rpc_task *task, void *calldata)
{
	struct nfs_read_data *data = calldata;
	struct nfs_page *req = data->req;
	struct page *page = req->wb_page;
 
	if (likely(task->tk_status >= 0))
		nfs_readpage_truncate_uninitialised_page(data);
	else
		SetPageError(page);
	if (nfs_readpage_result(task, data) != 0)
		return;
	if (atomic_dec_and_test(&req->wb_complete)) {
		if (!PageError(page))
			SetPageUptodate(page);
		nfs_readpage_release(req);
	}
}

static const struct rpc_call_ops nfs_read_partial_ops = {
	.rpc_call_done = nfs_readpage_result_partial,
	.rpc_release = nfs_readdata_release,
};

static void nfs_readpage_set_pages_uptodate(struct nfs_read_data *data)
{
	unsigned int count = data->res.count;
	unsigned int base = data->args.pgbase;
	struct page **pages;

	if (data->res.eof)
		count = data->args.count;
	if (unlikely(count == 0))
		return;
	pages = &data->args.pages[base >> PAGE_CACHE_SHIFT];
	base &= ~PAGE_CACHE_MASK;
	count += base;
	for (;count >= PAGE_CACHE_SIZE; count -= PAGE_CACHE_SIZE, pages++)
		SetPageUptodate(*pages);
	if (count != 0)
		SetPageUptodate(*pages);
}

static void nfs_readpage_set_pages_error(struct nfs_read_data *data)
{
	unsigned int count = data->args.count;
	unsigned int base = data->args.pgbase;
	struct page **pages;

	pages = &data->args.pages[base >> PAGE_CACHE_SHIFT];
	base &= ~PAGE_CACHE_MASK;
	count += base;
	for (;count >= PAGE_CACHE_SIZE; count -= PAGE_CACHE_SIZE, pages++)
		SetPageError(*pages);
	if (count != 0)
		SetPageError(*pages);
}

/*
 * This is the callback from RPC telling us whether a reply was
 * received or some error occurred (timeout or socket shutdown).
 */
static void nfs_readpage_result_full(struct rpc_task *task, void *calldata)
{
	struct nfs_read_data *data = calldata;

	/*
	 * Note: nfs_readpage_result may change the values of
	 * data->args. In the multi-page case, we therefore need
	 * to ensure that we call the next nfs_readpage_set_page_uptodate()
	 * first in the multi-page case.
	 */
	if (likely(task->tk_status >= 0)) {
		nfs_readpage_truncate_uninitialised_page(data);
		nfs_readpage_set_pages_uptodate(data);
	} else
		nfs_readpage_set_pages_error(data);
	if (nfs_readpage_result(task, data) != 0)
		return;
	while (!list_empty(&data->pages)) {
		struct nfs_page *req = nfs_list_entry(data->pages.next);

		nfs_list_remove_request(req);
		nfs_readpage_release(req);
	}
}

static const struct rpc_call_ops nfs_read_full_ops = {
	.rpc_call_done = nfs_readpage_result_full,
	.rpc_release = nfs_readdata_release,
};

/*
 * This is the callback from RPC telling us whether a reply was
 * received or some error occurred (timeout or socket shutdown).
 */
int nfs_readpage_result(struct rpc_task *task, struct nfs_read_data *data)
{
	struct nfs_readargs *argp = &data->args;
	struct nfs_readres *resp = &data->res;
	int status;

	dprintk("NFS: %4d nfs_readpage_result, (status %d)\n",
		task->tk_pid, task->tk_status);

	status = NFS_PROTO(data->inode)->read_done(task, data);
	if (status != 0)
		return status;

	nfs_add_stats(data->inode, NFSIOS_SERVERREADBYTES, resp->count);

	if (task->tk_status < 0) {
		if (task->tk_status == -ESTALE) {
			set_bit(NFS_INO_STALE, &NFS_FLAGS(data->inode));
			nfs_mark_for_revalidate(data->inode);
		}
	} else if (resp->count < argp->count && !resp->eof) {
		/* This is a short read! */
		nfs_inc_stats(data->inode, NFSIOS_SHORTREAD);
		/* Has the server at least made some progress? */
		if (resp->count != 0) {
			/* Yes, so retry the read at the end of the data */
			argp->offset += resp->count;
			argp->pgbase += resp->count;
			argp->count -= resp->count;
			rpc_restart_call(task);
			return -EAGAIN;
		}
		task->tk_status = -EIO;
	}
	spin_lock(&data->inode->i_lock);
	NFS_I(data->inode)->cache_validity |= NFS_INO_INVALID_ATIME;
	spin_unlock(&data->inode->i_lock);
	return 0;
}

/*
 * Read a page over NFS.
 * We read the page synchronously in the following case:
 *  -	The error flag is set for this page. This happens only when a
 *	previous async read operation failed.
 */
int nfs_readpage(struct file *file, struct page *page)
{
	struct nfs_open_context *ctx;
	struct inode *inode = page->mapping->host;
	int		error;

	dprintk("NFS: nfs_readpage (%p %ld@%lu)\n",
		page, PAGE_CACHE_SIZE, page->index);
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
		goto out_error;

	error = -ESTALE;
	if (NFS_STALE(inode))
		goto out_error;

	if (file == NULL) {
		ctx = nfs_find_open_context(inode, NULL, FMODE_READ);
		if (ctx == NULL)
			return -EBADF;
	} else
		ctx = get_nfs_open_context((struct nfs_open_context *)
				file->private_data);
	if (!IS_SYNC(inode)) {
		error = nfs_readpage_async(ctx, inode, page);
		goto out;
	}

	error = nfs_readpage_sync(ctx, inode, page);
	if (error < 0 && IS_SWAPFILE(inode))
		printk("Aiee.. nfs swap-in of page failed!\n");
out:
	put_nfs_open_context(ctx);
	return error;

out_error:
	unlock_page(page);
	return error;
}

struct nfs_readdesc {
	struct list_head *head;
	struct nfs_open_context *ctx;
};

static int
readpage_async_filler(void *data, struct page *page)
{
	struct nfs_readdesc *desc = (struct nfs_readdesc *)data;
	struct inode *inode = page->mapping->host;
	struct nfs_page *new;
	unsigned int len;

	nfs_wb_page(inode, page);
	len = nfs_page_length(inode, page);
	if (len == 0)
		return nfs_return_empty_page(page);
	new = nfs_create_request(desc->ctx, inode, page, 0, len);
	if (IS_ERR(new)) {
			SetPageError(page);
			unlock_page(page);
			return PTR_ERR(new);
	}
	if (len < PAGE_CACHE_SIZE)
		memclear_highpage_flush(page, len, PAGE_CACHE_SIZE - len);
	nfs_list_add_request(new, desc->head);
	return 0;
}

int nfs_readpages(struct file *filp, struct address_space *mapping,
		struct list_head *pages, unsigned nr_pages)
{
	LIST_HEAD(head);
	struct nfs_readdesc desc = {
		.head		= &head,
	};
	struct inode *inode = mapping->host;
	struct nfs_server *server = NFS_SERVER(inode);
	int ret = -ESTALE;

	dprintk("NFS: nfs_readpages (%s/%Ld %d)\n",
			inode->i_sb->s_id,
			(long long)NFS_FILEID(inode),
			nr_pages);
	nfs_inc_stats(inode, NFSIOS_VFSREADPAGES);

	if (NFS_STALE(inode))
		goto out;

	if (filp == NULL) {
		desc.ctx = nfs_find_open_context(inode, NULL, FMODE_READ);
		if (desc.ctx == NULL)
			return -EBADF;
	} else
		desc.ctx = get_nfs_open_context((struct nfs_open_context *)
				filp->private_data);
	ret = read_cache_pages(mapping, pages, readpage_async_filler, &desc);
	if (!list_empty(&head)) {
		int err = nfs_pagein_list(&head, server->rpages);
		if (!ret)
			nfs_add_stats(inode, NFSIOS_READPAGES, err);
			ret = err;
	}
	put_nfs_open_context(desc.ctx);
out:
	return ret;
}

int __init nfs_init_readpagecache(void)
{
	nfs_rdata_cachep = kmem_cache_create("nfs_read_data",
					     sizeof(struct nfs_read_data),
					     0, SLAB_HWCACHE_ALIGN,
					     NULL, NULL);
	if (nfs_rdata_cachep == NULL)
		return -ENOMEM;

	nfs_rdata_mempool = mempool_create_slab_pool(MIN_POOL_READ,
						     nfs_rdata_cachep);
	if (nfs_rdata_mempool == NULL)
		return -ENOMEM;

	return 0;
}

void nfs_destroy_readpagecache(void)
{
	mempool_destroy(nfs_rdata_mempool);
	kmem_cache_destroy(nfs_rdata_cachep);
}
