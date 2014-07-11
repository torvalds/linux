/*
 * linux/fs/nfs/pagelist.c
 *
 * A set of helper functions for managing NFS read and write requests.
 * The main purpose of these routines is to provide support for the
 * coalescing of several requests into a single RPC call.
 *
 * Copyright 2000, 2001 (c) Trond Myklebust <trond.myklebust@fys.uio.no>
 *
 */

#include <linux/slab.h>
#include <linux/file.h>
#include <linux/sched.h>
#include <linux/sunrpc/clnt.h>
#include <linux/nfs.h>
#include <linux/nfs3.h>
#include <linux/nfs4.h>
#include <linux/nfs_page.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_mount.h>
#include <linux/export.h>

#include "internal.h"
#include "pnfs.h"

#define NFSDBG_FACILITY		NFSDBG_PAGECACHE

static struct kmem_cache *nfs_page_cachep;
static const struct rpc_call_ops nfs_pgio_common_ops;

static void nfs_free_request(struct nfs_page *);

static bool nfs_pgarray_set(struct nfs_page_array *p, unsigned int pagecount)
{
	p->npages = pagecount;
	if (pagecount <= ARRAY_SIZE(p->page_array))
		p->pagevec = p->page_array;
	else {
		p->pagevec = kcalloc(pagecount, sizeof(struct page *), GFP_KERNEL);
		if (!p->pagevec)
			p->npages = 0;
	}
	return p->pagevec != NULL;
}

void nfs_pgheader_init(struct nfs_pageio_descriptor *desc,
		       struct nfs_pgio_header *hdr,
		       void (*release)(struct nfs_pgio_header *hdr))
{
	hdr->req = nfs_list_entry(desc->pg_list.next);
	hdr->inode = desc->pg_inode;
	hdr->cred = hdr->req->wb_context->cred;
	hdr->io_start = req_offset(hdr->req);
	hdr->good_bytes = desc->pg_count;
	hdr->dreq = desc->pg_dreq;
	hdr->layout_private = desc->pg_layout_private;
	hdr->release = release;
	hdr->completion_ops = desc->pg_completion_ops;
	if (hdr->completion_ops->init_hdr)
		hdr->completion_ops->init_hdr(hdr);
}
EXPORT_SYMBOL_GPL(nfs_pgheader_init);

void nfs_set_pgio_error(struct nfs_pgio_header *hdr, int error, loff_t pos)
{
	spin_lock(&hdr->lock);
	if (pos < hdr->io_start + hdr->good_bytes) {
		set_bit(NFS_IOHDR_ERROR, &hdr->flags);
		clear_bit(NFS_IOHDR_EOF, &hdr->flags);
		hdr->good_bytes = pos - hdr->io_start;
		hdr->error = error;
	}
	spin_unlock(&hdr->lock);
}

static inline struct nfs_page *
nfs_page_alloc(void)
{
	struct nfs_page	*p = kmem_cache_zalloc(nfs_page_cachep, GFP_NOIO);
	if (p)
		INIT_LIST_HEAD(&p->wb_list);
	return p;
}

static inline void
nfs_page_free(struct nfs_page *p)
{
	kmem_cache_free(nfs_page_cachep, p);
}

static void
nfs_iocounter_inc(struct nfs_io_counter *c)
{
	atomic_inc(&c->io_count);
}

static void
nfs_iocounter_dec(struct nfs_io_counter *c)
{
	if (atomic_dec_and_test(&c->io_count)) {
		clear_bit(NFS_IO_INPROGRESS, &c->flags);
		smp_mb__after_atomic();
		wake_up_bit(&c->flags, NFS_IO_INPROGRESS);
	}
}

static int
__nfs_iocounter_wait(struct nfs_io_counter *c)
{
	wait_queue_head_t *wq = bit_waitqueue(&c->flags, NFS_IO_INPROGRESS);
	DEFINE_WAIT_BIT(q, &c->flags, NFS_IO_INPROGRESS);
	int ret = 0;

	do {
		prepare_to_wait(wq, &q.wait, TASK_KILLABLE);
		set_bit(NFS_IO_INPROGRESS, &c->flags);
		if (atomic_read(&c->io_count) == 0)
			break;
		ret = nfs_wait_bit_killable(&c->flags);
	} while (atomic_read(&c->io_count) != 0);
	finish_wait(wq, &q.wait);
	return ret;
}

/**
 * nfs_iocounter_wait - wait for i/o to complete
 * @c: nfs_io_counter to use
 *
 * returns -ERESTARTSYS if interrupted by a fatal signal.
 * Otherwise returns 0 once the io_count hits 0.
 */
int
nfs_iocounter_wait(struct nfs_io_counter *c)
{
	if (atomic_read(&c->io_count) == 0)
		return 0;
	return __nfs_iocounter_wait(c);
}

static int nfs_wait_bit_uninterruptible(void *word)
{
	io_schedule();
	return 0;
}

/*
 * nfs_page_group_lock - lock the head of the page group
 * @req - request in group that is to be locked
 *
 * this lock must be held if modifying the page group list
 */
void
nfs_page_group_lock(struct nfs_page *req)
{
	struct nfs_page *head = req->wb_head;

	WARN_ON_ONCE(head != head->wb_head);

	wait_on_bit_lock(&head->wb_flags, PG_HEADLOCK,
			nfs_wait_bit_uninterruptible,
			TASK_UNINTERRUPTIBLE);
}

/*
 * nfs_page_group_unlock - unlock the head of the page group
 * @req - request in group that is to be unlocked
 */
void
nfs_page_group_unlock(struct nfs_page *req)
{
	struct nfs_page *head = req->wb_head;

	WARN_ON_ONCE(head != head->wb_head);

	smp_mb__before_atomic();
	clear_bit(PG_HEADLOCK, &head->wb_flags);
	smp_mb__after_atomic();
	wake_up_bit(&head->wb_flags, PG_HEADLOCK);
}

/*
 * nfs_page_group_sync_on_bit_locked
 *
 * must be called with page group lock held
 */
static bool
nfs_page_group_sync_on_bit_locked(struct nfs_page *req, unsigned int bit)
{
	struct nfs_page *head = req->wb_head;
	struct nfs_page *tmp;

	WARN_ON_ONCE(!test_bit(PG_HEADLOCK, &head->wb_flags));
	WARN_ON_ONCE(test_and_set_bit(bit, &req->wb_flags));

	tmp = req->wb_this_page;
	while (tmp != req) {
		if (!test_bit(bit, &tmp->wb_flags))
			return false;
		tmp = tmp->wb_this_page;
	}

	/* true! reset all bits */
	tmp = req;
	do {
		clear_bit(bit, &tmp->wb_flags);
		tmp = tmp->wb_this_page;
	} while (tmp != req);

	return true;
}

/*
 * nfs_page_group_sync_on_bit - set bit on current request, but only
 *   return true if the bit is set for all requests in page group
 * @req - request in page group
 * @bit - PG_* bit that is used to sync page group
 */
bool nfs_page_group_sync_on_bit(struct nfs_page *req, unsigned int bit)
{
	bool ret;

	nfs_page_group_lock(req);
	ret = nfs_page_group_sync_on_bit_locked(req, bit);
	nfs_page_group_unlock(req);

	return ret;
}

/*
 * nfs_page_group_init - Initialize the page group linkage for @req
 * @req - a new nfs request
 * @prev - the previous request in page group, or NULL if @req is the first
 *         or only request in the group (the head).
 */
static inline void
nfs_page_group_init(struct nfs_page *req, struct nfs_page *prev)
{
	WARN_ON_ONCE(prev == req);

	if (!prev) {
		/* a head request */
		req->wb_head = req;
		req->wb_this_page = req;
	} else {
		/* a subrequest */
		WARN_ON_ONCE(prev->wb_this_page != prev->wb_head);
		WARN_ON_ONCE(!test_bit(PG_HEADLOCK, &prev->wb_head->wb_flags));
		req->wb_head = prev->wb_head;
		req->wb_this_page = prev->wb_this_page;
		prev->wb_this_page = req;

		/* All subrequests take a ref on the head request until
		 * nfs_page_group_destroy is called */
		kref_get(&req->wb_head->wb_kref);

		/* grab extra ref if head request has extra ref from
		 * the write/commit path to handle handoff between write
		 * and commit lists */
		if (test_bit(PG_INODE_REF, &prev->wb_head->wb_flags)) {
			set_bit(PG_INODE_REF, &req->wb_flags);
			kref_get(&req->wb_kref);
		}
	}
}

/*
 * nfs_page_group_destroy - sync the destruction of page groups
 * @req - request that no longer needs the page group
 *
 * releases the page group reference from each member once all
 * members have called this function.
 */
static void
nfs_page_group_destroy(struct kref *kref)
{
	struct nfs_page *req = container_of(kref, struct nfs_page, wb_kref);
	struct nfs_page *tmp, *next;

	/* subrequests must release the ref on the head request */
	if (req->wb_head != req)
		nfs_release_request(req->wb_head);

	if (!nfs_page_group_sync_on_bit(req, PG_TEARDOWN))
		return;

	tmp = req;
	do {
		next = tmp->wb_this_page;
		/* unlink and free */
		tmp->wb_this_page = tmp;
		tmp->wb_head = tmp;
		nfs_free_request(tmp);
		tmp = next;
	} while (tmp != req);
}

/**
 * nfs_create_request - Create an NFS read/write request.
 * @ctx: open context to use
 * @page: page to write
 * @last: last nfs request created for this page group or NULL if head
 * @offset: starting offset within the page for the write
 * @count: number of bytes to read/write
 *
 * The page must be locked by the caller. This makes sure we never
 * create two different requests for the same page.
 * User should ensure it is safe to sleep in this function.
 */
struct nfs_page *
nfs_create_request(struct nfs_open_context *ctx, struct page *page,
		   struct nfs_page *last, unsigned int offset,
		   unsigned int count)
{
	struct nfs_page		*req;
	struct nfs_lock_context *l_ctx;

	if (test_bit(NFS_CONTEXT_BAD, &ctx->flags))
		return ERR_PTR(-EBADF);
	/* try to allocate the request struct */
	req = nfs_page_alloc();
	if (req == NULL)
		return ERR_PTR(-ENOMEM);

	/* get lock context early so we can deal with alloc failures */
	l_ctx = nfs_get_lock_context(ctx);
	if (IS_ERR(l_ctx)) {
		nfs_page_free(req);
		return ERR_CAST(l_ctx);
	}
	req->wb_lock_context = l_ctx;
	nfs_iocounter_inc(&l_ctx->io_count);

	/* Initialize the request struct. Initially, we assume a
	 * long write-back delay. This will be adjusted in
	 * update_nfs_request below if the region is not locked. */
	req->wb_page    = page;
	req->wb_index	= page_file_index(page);
	page_cache_get(page);
	req->wb_offset  = offset;
	req->wb_pgbase	= offset;
	req->wb_bytes   = count;
	req->wb_context = get_nfs_open_context(ctx);
	kref_init(&req->wb_kref);
	nfs_page_group_init(req, last);
	return req;
}

/**
 * nfs_unlock_request - Unlock request and wake up sleepers.
 * @req:
 */
void nfs_unlock_request(struct nfs_page *req)
{
	if (!NFS_WBACK_BUSY(req)) {
		printk(KERN_ERR "NFS: Invalid unlock attempted\n");
		BUG();
	}
	smp_mb__before_atomic();
	clear_bit(PG_BUSY, &req->wb_flags);
	smp_mb__after_atomic();
	wake_up_bit(&req->wb_flags, PG_BUSY);
}

/**
 * nfs_unlock_and_release_request - Unlock request and release the nfs_page
 * @req:
 */
void nfs_unlock_and_release_request(struct nfs_page *req)
{
	nfs_unlock_request(req);
	nfs_release_request(req);
}

/*
 * nfs_clear_request - Free up all resources allocated to the request
 * @req:
 *
 * Release page and open context resources associated with a read/write
 * request after it has completed.
 */
static void nfs_clear_request(struct nfs_page *req)
{
	struct page *page = req->wb_page;
	struct nfs_open_context *ctx = req->wb_context;
	struct nfs_lock_context *l_ctx = req->wb_lock_context;

	if (page != NULL) {
		page_cache_release(page);
		req->wb_page = NULL;
	}
	if (l_ctx != NULL) {
		nfs_iocounter_dec(&l_ctx->io_count);
		nfs_put_lock_context(l_ctx);
		req->wb_lock_context = NULL;
	}
	if (ctx != NULL) {
		put_nfs_open_context(ctx);
		req->wb_context = NULL;
	}
}

/**
 * nfs_release_request - Release the count on an NFS read/write request
 * @req: request to release
 *
 * Note: Should never be called with the spinlock held!
 */
static void nfs_free_request(struct nfs_page *req)
{
	WARN_ON_ONCE(req->wb_this_page != req);

	/* extra debug: make sure no sync bits are still set */
	WARN_ON_ONCE(test_bit(PG_TEARDOWN, &req->wb_flags));
	WARN_ON_ONCE(test_bit(PG_UNLOCKPAGE, &req->wb_flags));
	WARN_ON_ONCE(test_bit(PG_UPTODATE, &req->wb_flags));
	WARN_ON_ONCE(test_bit(PG_WB_END, &req->wb_flags));
	WARN_ON_ONCE(test_bit(PG_REMOVE, &req->wb_flags));

	/* Release struct file and open context */
	nfs_clear_request(req);
	nfs_page_free(req);
}

void nfs_release_request(struct nfs_page *req)
{
	kref_put(&req->wb_kref, nfs_page_group_destroy);
}

/**
 * nfs_wait_on_request - Wait for a request to complete.
 * @req: request to wait upon.
 *
 * Interruptible by fatal signals only.
 * The user is responsible for holding a count on the request.
 */
int
nfs_wait_on_request(struct nfs_page *req)
{
	return wait_on_bit(&req->wb_flags, PG_BUSY,
			nfs_wait_bit_uninterruptible,
			TASK_UNINTERRUPTIBLE);
}

/*
 * nfs_generic_pg_test - determine if requests can be coalesced
 * @desc: pointer to descriptor
 * @prev: previous request in desc, or NULL
 * @req: this request
 *
 * Returns zero if @req can be coalesced into @desc, otherwise it returns
 * the size of the request.
 */
size_t nfs_generic_pg_test(struct nfs_pageio_descriptor *desc,
			   struct nfs_page *prev, struct nfs_page *req)
{
	if (desc->pg_count > desc->pg_bsize) {
		/* should never happen */
		WARN_ON_ONCE(1);
		return 0;
	}

	return min(desc->pg_bsize - desc->pg_count, (size_t)req->wb_bytes);
}
EXPORT_SYMBOL_GPL(nfs_generic_pg_test);

static inline struct nfs_rw_header *NFS_RW_HEADER(struct nfs_pgio_header *hdr)
{
	return container_of(hdr, struct nfs_rw_header, header);
}

/**
 * nfs_rw_header_alloc - Allocate a header for a read or write
 * @ops: Read or write function vector
 */
struct nfs_rw_header *nfs_rw_header_alloc(const struct nfs_rw_ops *ops)
{
	struct nfs_rw_header *header = ops->rw_alloc_header();

	if (header) {
		struct nfs_pgio_header *hdr = &header->header;

		INIT_LIST_HEAD(&hdr->pages);
		spin_lock_init(&hdr->lock);
		atomic_set(&hdr->refcnt, 0);
		hdr->rw_ops = ops;
	}
	return header;
}
EXPORT_SYMBOL_GPL(nfs_rw_header_alloc);

/*
 * nfs_rw_header_free - Free a read or write header
 * @hdr: The header to free
 */
void nfs_rw_header_free(struct nfs_pgio_header *hdr)
{
	hdr->rw_ops->rw_free_header(NFS_RW_HEADER(hdr));
}
EXPORT_SYMBOL_GPL(nfs_rw_header_free);

/**
 * nfs_pgio_data_alloc - Allocate pageio data
 * @hdr: The header making a request
 * @pagecount: Number of pages to create
 */
static struct nfs_pgio_data *nfs_pgio_data_alloc(struct nfs_pgio_header *hdr,
						 unsigned int pagecount)
{
	struct nfs_pgio_data *data, *prealloc;

	prealloc = &NFS_RW_HEADER(hdr)->rpc_data;
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

/**
 * nfs_pgio_data_release - Properly free pageio data
 * @data: The data to release
 */
void nfs_pgio_data_release(struct nfs_pgio_data *data)
{
	struct nfs_pgio_header *hdr = data->header;
	struct nfs_rw_header *pageio_header = NFS_RW_HEADER(hdr);

	put_nfs_open_context(data->args.context);
	if (data->pages.pagevec != data->pages.page_array)
		kfree(data->pages.pagevec);
	if (data == &pageio_header->rpc_data) {
		data->header = NULL;
		data = NULL;
	}
	if (atomic_dec_and_test(&hdr->refcnt))
		hdr->completion_ops->completion(hdr);
	/* Note: we only free the rpc_task after callbacks are done.
	 * See the comment in rpc_free_task() for why
	 */
	kfree(data);
}
EXPORT_SYMBOL_GPL(nfs_pgio_data_release);

/**
 * nfs_pgio_rpcsetup - Set up arguments for a pageio call
 * @data: The pageio data
 * @count: Number of bytes to read
 * @offset: Initial offset
 * @how: How to commit data (writes only)
 * @cinfo: Commit information for the call (writes only)
 */
static void nfs_pgio_rpcsetup(struct nfs_pgio_data *data,
			      unsigned int count, unsigned int offset,
			      int how, struct nfs_commit_info *cinfo)
{
	struct nfs_page *req = data->header->req;

	/* Set up the RPC argument and reply structs
	 * NB: take care not to mess about with data->commit et al. */

	data->args.fh     = NFS_FH(data->header->inode);
	data->args.offset = req_offset(req) + offset;
	/* pnfs_set_layoutcommit needs this */
	data->mds_offset = data->args.offset;
	data->args.pgbase = req->wb_pgbase + offset;
	data->args.pages  = data->pages.pagevec;
	data->args.count  = count;
	data->args.context = get_nfs_open_context(req->wb_context);
	data->args.lock_context = req->wb_lock_context;
	data->args.stable  = NFS_UNSTABLE;
	switch (how & (FLUSH_STABLE | FLUSH_COND_STABLE)) {
	case 0:
		break;
	case FLUSH_COND_STABLE:
		if (nfs_reqs_to_commit(cinfo))
			break;
	default:
		data->args.stable = NFS_FILE_SYNC;
	}

	data->res.fattr   = &data->fattr;
	data->res.count   = count;
	data->res.eof     = 0;
	data->res.verf    = &data->verf;
	nfs_fattr_init(&data->fattr);
}

/**
 * nfs_pgio_prepare - Prepare pageio data to go over the wire
 * @task: The current task
 * @calldata: pageio data to prepare
 */
static void nfs_pgio_prepare(struct rpc_task *task, void *calldata)
{
	struct nfs_pgio_data *data = calldata;
	int err;
	err = NFS_PROTO(data->header->inode)->pgio_rpc_prepare(task, data);
	if (err)
		rpc_exit(task, err);
}

int nfs_initiate_pgio(struct rpc_clnt *clnt, struct nfs_pgio_data *data,
		      const struct rpc_call_ops *call_ops, int how, int flags)
{
	struct rpc_task *task;
	struct rpc_message msg = {
		.rpc_argp = &data->args,
		.rpc_resp = &data->res,
		.rpc_cred = data->header->cred,
	};
	struct rpc_task_setup task_setup_data = {
		.rpc_client = clnt,
		.task = &data->task,
		.rpc_message = &msg,
		.callback_ops = call_ops,
		.callback_data = data,
		.workqueue = nfsiod_workqueue,
		.flags = RPC_TASK_ASYNC | flags,
	};
	int ret = 0;

	data->header->rw_ops->rw_initiate(data, &msg, &task_setup_data, how);

	dprintk("NFS: %5u initiated pgio call "
		"(req %s/%llu, %u bytes @ offset %llu)\n",
		data->task.tk_pid,
		data->header->inode->i_sb->s_id,
		(unsigned long long)NFS_FILEID(data->header->inode),
		data->args.count,
		(unsigned long long)data->args.offset);

	task = rpc_run_task(&task_setup_data);
	if (IS_ERR(task)) {
		ret = PTR_ERR(task);
		goto out;
	}
	if (how & FLUSH_SYNC) {
		ret = rpc_wait_for_completion_task(task);
		if (ret == 0)
			ret = task->tk_status;
	}
	rpc_put_task(task);
out:
	return ret;
}
EXPORT_SYMBOL_GPL(nfs_initiate_pgio);

/**
 * nfs_pgio_error - Clean up from a pageio error
 * @desc: IO descriptor
 * @hdr: pageio header
 */
static int nfs_pgio_error(struct nfs_pageio_descriptor *desc,
			  struct nfs_pgio_header *hdr)
{
	set_bit(NFS_IOHDR_REDO, &hdr->flags);
	nfs_pgio_data_release(hdr->data);
	hdr->data = NULL;
	desc->pg_completion_ops->error_cleanup(&desc->pg_list);
	return -ENOMEM;
}

/**
 * nfs_pgio_release - Release pageio data
 * @calldata: The pageio data to release
 */
static void nfs_pgio_release(void *calldata)
{
	struct nfs_pgio_data *data = calldata;
	if (data->header->rw_ops->rw_release)
		data->header->rw_ops->rw_release(data);
	nfs_pgio_data_release(data);
}

/**
 * nfs_pageio_init - initialise a page io descriptor
 * @desc: pointer to descriptor
 * @inode: pointer to inode
 * @doio: pointer to io function
 * @bsize: io block size
 * @io_flags: extra parameters for the io function
 */
void nfs_pageio_init(struct nfs_pageio_descriptor *desc,
		     struct inode *inode,
		     const struct nfs_pageio_ops *pg_ops,
		     const struct nfs_pgio_completion_ops *compl_ops,
		     const struct nfs_rw_ops *rw_ops,
		     size_t bsize,
		     int io_flags)
{
	INIT_LIST_HEAD(&desc->pg_list);
	desc->pg_bytes_written = 0;
	desc->pg_count = 0;
	desc->pg_bsize = bsize;
	desc->pg_base = 0;
	desc->pg_moreio = 0;
	desc->pg_recoalesce = 0;
	desc->pg_inode = inode;
	desc->pg_ops = pg_ops;
	desc->pg_completion_ops = compl_ops;
	desc->pg_rw_ops = rw_ops;
	desc->pg_ioflags = io_flags;
	desc->pg_error = 0;
	desc->pg_lseg = NULL;
	desc->pg_dreq = NULL;
	desc->pg_layout_private = NULL;
}
EXPORT_SYMBOL_GPL(nfs_pageio_init);

/**
 * nfs_pgio_result - Basic pageio error handling
 * @task: The task that ran
 * @calldata: Pageio data to check
 */
static void nfs_pgio_result(struct rpc_task *task, void *calldata)
{
	struct nfs_pgio_data *data = calldata;
	struct inode *inode = data->header->inode;

	dprintk("NFS: %s: %5u, (status %d)\n", __func__,
		task->tk_pid, task->tk_status);

	if (data->header->rw_ops->rw_done(task, data, inode) != 0)
		return;
	if (task->tk_status < 0)
		nfs_set_pgio_error(data->header, task->tk_status, data->args.offset);
	else
		data->header->rw_ops->rw_result(task, data);
}

/*
 * Create an RPC task for the given read or write request and kick it.
 * The page must have been locked by the caller.
 *
 * It may happen that the page we're passed is not marked dirty.
 * This is the case if nfs_updatepage detects a conflicting request
 * that has been written but not committed.
 */
int nfs_generic_pgio(struct nfs_pageio_descriptor *desc,
		     struct nfs_pgio_header *hdr)
{
	struct nfs_page		*req;
	struct page		**pages;
	struct nfs_pgio_data	*data;
	struct list_head *head = &desc->pg_list;
	struct nfs_commit_info cinfo;

	data = nfs_pgio_data_alloc(hdr, nfs_page_array_len(desc->pg_base,
							   desc->pg_count));
	if (!data)
		return nfs_pgio_error(desc, hdr);

	nfs_init_cinfo(&cinfo, desc->pg_inode, desc->pg_dreq);
	pages = data->pages.pagevec;
	while (!list_empty(head)) {
		req = nfs_list_entry(head->next);
		nfs_list_remove_request(req);
		nfs_list_add_request(req, &hdr->pages);
		*pages++ = req->wb_page;
	}

	if ((desc->pg_ioflags & FLUSH_COND_STABLE) &&
	    (desc->pg_moreio || nfs_reqs_to_commit(&cinfo)))
		desc->pg_ioflags &= ~FLUSH_COND_STABLE;

	/* Set up the argument struct */
	nfs_pgio_rpcsetup(data, desc->pg_count, 0, desc->pg_ioflags, &cinfo);
	hdr->data = data;
	desc->pg_rpc_callops = &nfs_pgio_common_ops;
	return 0;
}
EXPORT_SYMBOL_GPL(nfs_generic_pgio);

static int nfs_generic_pg_pgios(struct nfs_pageio_descriptor *desc)
{
	struct nfs_rw_header *rw_hdr;
	struct nfs_pgio_header *hdr;
	int ret;

	rw_hdr = nfs_rw_header_alloc(desc->pg_rw_ops);
	if (!rw_hdr) {
		desc->pg_completion_ops->error_cleanup(&desc->pg_list);
		return -ENOMEM;
	}
	hdr = &rw_hdr->header;
	nfs_pgheader_init(desc, hdr, nfs_rw_header_free);
	atomic_inc(&hdr->refcnt);
	ret = nfs_generic_pgio(desc, hdr);
	if (ret == 0)
		ret = nfs_initiate_pgio(NFS_CLIENT(hdr->inode),
					hdr->data, desc->pg_rpc_callops,
					desc->pg_ioflags, 0);
	if (atomic_dec_and_test(&hdr->refcnt))
		hdr->completion_ops->completion(hdr);
	return ret;
}

static bool nfs_match_open_context(const struct nfs_open_context *ctx1,
		const struct nfs_open_context *ctx2)
{
	return ctx1->cred == ctx2->cred && ctx1->state == ctx2->state;
}

static bool nfs_match_lock_context(const struct nfs_lock_context *l1,
		const struct nfs_lock_context *l2)
{
	return l1->lockowner.l_owner == l2->lockowner.l_owner
		&& l1->lockowner.l_pid == l2->lockowner.l_pid;
}

/**
 * nfs_can_coalesce_requests - test two requests for compatibility
 * @prev: pointer to nfs_page
 * @req: pointer to nfs_page
 *
 * The nfs_page structures 'prev' and 'req' are compared to ensure that the
 * page data area they describe is contiguous, and that their RPC
 * credentials, NFSv4 open state, and lockowners are the same.
 *
 * Return 'true' if this is the case, else return 'false'.
 */
static bool nfs_can_coalesce_requests(struct nfs_page *prev,
				      struct nfs_page *req,
				      struct nfs_pageio_descriptor *pgio)
{
	size_t size;

	if (prev) {
		if (!nfs_match_open_context(req->wb_context, prev->wb_context))
			return false;
		if (req->wb_context->dentry->d_inode->i_flock != NULL &&
		    !nfs_match_lock_context(req->wb_lock_context,
					    prev->wb_lock_context))
			return false;
		if (req_offset(req) != req_offset(prev) + prev->wb_bytes)
			return false;
	}
	size = pgio->pg_ops->pg_test(pgio, prev, req);
	WARN_ON_ONCE(size > req->wb_bytes);
	if (size && size < req->wb_bytes)
		req->wb_bytes = size;
	return size > 0;
}

/**
 * nfs_pageio_do_add_request - Attempt to coalesce a request into a page list.
 * @desc: destination io descriptor
 * @req: request
 *
 * Returns true if the request 'req' was successfully coalesced into the
 * existing list of pages 'desc'.
 */
static int nfs_pageio_do_add_request(struct nfs_pageio_descriptor *desc,
				     struct nfs_page *req)
{
	struct nfs_page *prev = NULL;
	if (desc->pg_count != 0) {
		prev = nfs_list_entry(desc->pg_list.prev);
	} else {
		if (desc->pg_ops->pg_init)
			desc->pg_ops->pg_init(desc, req);
		desc->pg_base = req->wb_pgbase;
	}
	if (!nfs_can_coalesce_requests(prev, req, desc))
		return 0;
	nfs_list_remove_request(req);
	nfs_list_add_request(req, &desc->pg_list);
	desc->pg_count += req->wb_bytes;
	return 1;
}

/*
 * Helper for nfs_pageio_add_request and nfs_pageio_complete
 */
static void nfs_pageio_doio(struct nfs_pageio_descriptor *desc)
{
	if (!list_empty(&desc->pg_list)) {
		int error = desc->pg_ops->pg_doio(desc);
		if (error < 0)
			desc->pg_error = error;
		else
			desc->pg_bytes_written += desc->pg_count;
	}
	if (list_empty(&desc->pg_list)) {
		desc->pg_count = 0;
		desc->pg_base = 0;
	}
}

/**
 * nfs_pageio_add_request - Attempt to coalesce a request into a page list.
 * @desc: destination io descriptor
 * @req: request
 *
 * This may split a request into subrequests which are all part of the
 * same page group.
 *
 * Returns true if the request 'req' was successfully coalesced into the
 * existing list of pages 'desc'.
 */
static int __nfs_pageio_add_request(struct nfs_pageio_descriptor *desc,
			   struct nfs_page *req)
{
	struct nfs_page *subreq;
	unsigned int bytes_left = 0;
	unsigned int offset, pgbase;

	nfs_page_group_lock(req);

	subreq = req;
	bytes_left = subreq->wb_bytes;
	offset = subreq->wb_offset;
	pgbase = subreq->wb_pgbase;

	do {
		if (!nfs_pageio_do_add_request(desc, subreq)) {
			/* make sure pg_test call(s) did nothing */
			WARN_ON_ONCE(subreq->wb_bytes != bytes_left);
			WARN_ON_ONCE(subreq->wb_offset != offset);
			WARN_ON_ONCE(subreq->wb_pgbase != pgbase);

			nfs_page_group_unlock(req);
			desc->pg_moreio = 1;
			nfs_pageio_doio(desc);
			if (desc->pg_error < 0)
				return 0;
			desc->pg_moreio = 0;
			if (desc->pg_recoalesce)
				return 0;
			/* retry add_request for this subreq */
			nfs_page_group_lock(req);
			continue;
		}

		/* check for buggy pg_test call(s) */
		WARN_ON_ONCE(subreq->wb_bytes + subreq->wb_pgbase > PAGE_SIZE);
		WARN_ON_ONCE(subreq->wb_bytes > bytes_left);
		WARN_ON_ONCE(subreq->wb_bytes == 0);

		bytes_left -= subreq->wb_bytes;
		offset += subreq->wb_bytes;
		pgbase += subreq->wb_bytes;

		if (bytes_left) {
			subreq = nfs_create_request(req->wb_context,
					req->wb_page,
					subreq, pgbase, bytes_left);
			if (IS_ERR(subreq))
				goto err_ptr;
			nfs_lock_request(subreq);
			subreq->wb_offset  = offset;
			subreq->wb_index = req->wb_index;
		}
	} while (bytes_left > 0);

	nfs_page_group_unlock(req);
	return 1;
err_ptr:
	desc->pg_error = PTR_ERR(subreq);
	nfs_page_group_unlock(req);
	return 0;
}

static int nfs_do_recoalesce(struct nfs_pageio_descriptor *desc)
{
	LIST_HEAD(head);

	do {
		list_splice_init(&desc->pg_list, &head);
		desc->pg_bytes_written -= desc->pg_count;
		desc->pg_count = 0;
		desc->pg_base = 0;
		desc->pg_recoalesce = 0;

		while (!list_empty(&head)) {
			struct nfs_page *req;

			req = list_first_entry(&head, struct nfs_page, wb_list);
			nfs_list_remove_request(req);
			if (__nfs_pageio_add_request(desc, req))
				continue;
			if (desc->pg_error < 0)
				return 0;
			break;
		}
	} while (desc->pg_recoalesce);
	return 1;
}

int nfs_pageio_add_request(struct nfs_pageio_descriptor *desc,
		struct nfs_page *req)
{
	int ret;

	do {
		ret = __nfs_pageio_add_request(desc, req);
		if (ret)
			break;
		if (desc->pg_error < 0)
			break;
		ret = nfs_do_recoalesce(desc);
	} while (ret);
	return ret;
}
EXPORT_SYMBOL_GPL(nfs_pageio_add_request);

/**
 * nfs_pageio_complete - Complete I/O on an nfs_pageio_descriptor
 * @desc: pointer to io descriptor
 */
void nfs_pageio_complete(struct nfs_pageio_descriptor *desc)
{
	for (;;) {
		nfs_pageio_doio(desc);
		if (!desc->pg_recoalesce)
			break;
		if (!nfs_do_recoalesce(desc))
			break;
	}
}
EXPORT_SYMBOL_GPL(nfs_pageio_complete);

/**
 * nfs_pageio_cond_complete - Conditional I/O completion
 * @desc: pointer to io descriptor
 * @index: page index
 *
 * It is important to ensure that processes don't try to take locks
 * on non-contiguous ranges of pages as that might deadlock. This
 * function should be called before attempting to wait on a locked
 * nfs_page. It will complete the I/O if the page index 'index'
 * is not contiguous with the existing list of pages in 'desc'.
 */
void nfs_pageio_cond_complete(struct nfs_pageio_descriptor *desc, pgoff_t index)
{
	if (!list_empty(&desc->pg_list)) {
		struct nfs_page *prev = nfs_list_entry(desc->pg_list.prev);
		if (index != prev->wb_index + 1)
			nfs_pageio_complete(desc);
	}
}

int __init nfs_init_nfspagecache(void)
{
	nfs_page_cachep = kmem_cache_create("nfs_page",
					    sizeof(struct nfs_page),
					    0, SLAB_HWCACHE_ALIGN,
					    NULL);
	if (nfs_page_cachep == NULL)
		return -ENOMEM;

	return 0;
}

void nfs_destroy_nfspagecache(void)
{
	kmem_cache_destroy(nfs_page_cachep);
}

static const struct rpc_call_ops nfs_pgio_common_ops = {
	.rpc_call_prepare = nfs_pgio_prepare,
	.rpc_call_done = nfs_pgio_result,
	.rpc_release = nfs_pgio_release,
};

const struct nfs_pageio_ops nfs_pgio_rw_ops = {
	.pg_test = nfs_generic_pg_test,
	.pg_doio = nfs_generic_pg_pgios,
};
