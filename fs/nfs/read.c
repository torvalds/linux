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

#include <linux/config.h>
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

#define NFSDBG_FACILITY		NFSDBG_PAGECACHE

static int nfs_pagein_one(struct list_head *, struct inode *);
static void nfs_readpage_result_partial(struct nfs_read_data *, int);
static void nfs_readpage_result_full(struct nfs_read_data *, int);

static kmem_cache_t *nfs_rdata_cachep;
mempool_t *nfs_rdata_mempool;

#define MIN_POOL_READ	(32)

void nfs_readdata_release(struct rpc_task *task)
{
        struct nfs_read_data   *data = (struct nfs_read_data *)task->tk_calldata;
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

	rdata = nfs_readdata_alloc();
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
			NFS_SERVER(inode)->hostname,
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
		/* Note: result == 0 should only happen if we're caching
		 * a write that extends the file and punches a hole.
		 */
		if (rdata->res.eof != 0 || result == 0)
			break;
	} while (count);
	spin_lock(&inode->i_lock);
	NFS_I(inode)->cache_validity |= NFS_INO_INVALID_ATIME;
	spin_unlock(&inode->i_lock);

	if (count)
		memclear_highpage_flush(page, rdata->args.pgbase, count);
	SetPageUptodate(page);
	if (PageError(page))
		ClearPageError(page);
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
		unsigned int count, unsigned int offset)
{
	struct inode		*inode;

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

	NFS_PROTO(inode)->read_setup(data);

	data->task.tk_cookie = (unsigned long)inode;
	data->task.tk_calldata = data;
	/* Release requests */
	data->task.tk_release = nfs_readdata_release;

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
	unsigned int rsize = NFS_SERVER(inode)->rsize;
	unsigned int nbytes, offset;
	int requests = 0;
	LIST_HEAD(list);

	nfs_list_remove_request(req);

	nbytes = req->wb_bytes;
	for(;;) {
		data = nfs_readdata_alloc();
		if (!data)
			goto out_bad;
		INIT_LIST_HEAD(&data->pages);
		list_add(&data->pages, &list);
		requests++;
		if (nbytes <= rsize)
			break;
		nbytes -= rsize;
	}
	atomic_set(&req->wb_complete, requests);

	ClearPageError(page);
	offset = 0;
	nbytes = req->wb_bytes;
	do {
		data = list_entry(list.next, struct nfs_read_data, pages);
		list_del_init(&data->pages);

		data->pagevec[0] = page;
		data->complete = nfs_readpage_result_partial;

		if (nbytes > rsize) {
			nfs_read_rpcsetup(req, data, rsize, offset);
			offset += rsize;
			nbytes -= rsize;
		} else {
			nfs_read_rpcsetup(req, data, nbytes, offset);
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

	data = nfs_readdata_alloc();
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

	data->complete = nfs_readpage_result_full;
	nfs_read_rpcsetup(req, data, count, 0);

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
static void nfs_readpage_result_partial(struct nfs_read_data *data, int status)
{
	struct nfs_page *req = data->req;
	struct page *page = req->wb_page;
 
	if (status >= 0) {
		unsigned int request = data->args.count;
		unsigned int result = data->res.count;

		if (result < request) {
			memclear_highpage_flush(page,
						data->args.pgbase + result,
						request - result);
		}
	} else
		SetPageError(page);

	if (atomic_dec_and_test(&req->wb_complete)) {
		if (!PageError(page))
			SetPageUptodate(page);
		nfs_readpage_release(req);
	}
}

/*
 * This is the callback from RPC telling us whether a reply was
 * received or some error occurred (timeout or socket shutdown).
 */
static void nfs_readpage_result_full(struct nfs_read_data *data, int status)
{
	unsigned int count = data->res.count;

	while (!list_empty(&data->pages)) {
		struct nfs_page *req = nfs_list_entry(data->pages.next);
		struct page *page = req->wb_page;
		nfs_list_remove_request(req);

		if (status >= 0) {
			if (count < PAGE_CACHE_SIZE) {
				if (count < req->wb_bytes)
					memclear_highpage_flush(page,
							req->wb_pgbase + count,
							req->wb_bytes - count);
				count = 0;
			} else
				count -= PAGE_CACHE_SIZE;
			SetPageUptodate(page);
		} else
			SetPageError(page);
		nfs_readpage_release(req);
	}
}

/*
 * This is the callback from RPC telling us whether a reply was
 * received or some error occurred (timeout or socket shutdown).
 */
void nfs_readpage_result(struct rpc_task *task)
{
	struct nfs_read_data *data = (struct nfs_read_data *)task->tk_calldata;
	struct nfs_readargs *argp = &data->args;
	struct nfs_readres *resp = &data->res;
	int status = task->tk_status;

	dprintk("NFS: %4d nfs_readpage_result, (status %d)\n",
		task->tk_pid, status);

	/* Is this a short read? */
	if (task->tk_status >= 0 && resp->count < argp->count && !resp->eof) {
		/* Has the server at least made some progress? */
		if (resp->count != 0) {
			/* Yes, so retry the read at the end of the data */
			argp->offset += resp->count;
			argp->pgbase += resp->count;
			argp->count -= resp->count;
			rpc_restart_call(task);
			return;
		}
		task->tk_status = -EIO;
	}
	spin_lock(&data->inode->i_lock);
	NFS_I(data->inode)->cache_validity |= NFS_INO_INVALID_ATIME;
	spin_unlock(&data->inode->i_lock);
	data->complete(data, status);
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
	int ret;

	dprintk("NFS: nfs_readpages (%s/%Ld %d)\n",
			inode->i_sb->s_id,
			(long long)NFS_FILEID(inode),
			nr_pages);

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
			ret = err;
	}
	put_nfs_open_context(desc.ctx);
	return ret;
}

int nfs_init_readpagecache(void)
{
	nfs_rdata_cachep = kmem_cache_create("nfs_read_data",
					     sizeof(struct nfs_read_data),
					     0, SLAB_HWCACHE_ALIGN,
					     NULL, NULL);
	if (nfs_rdata_cachep == NULL)
		return -ENOMEM;

	nfs_rdata_mempool = mempool_create(MIN_POOL_READ,
					   mempool_alloc_slab,
					   mempool_free_slab,
					   nfs_rdata_cachep);
	if (nfs_rdata_mempool == NULL)
		return -ENOMEM;

	return 0;
}

void nfs_destroy_readpagecache(void)
{
	mempool_destroy(nfs_rdata_mempool);
	if (kmem_cache_destroy(nfs_rdata_cachep))
		printk(KERN_INFO "nfs_read_data: not all structures were freed\n");
}
