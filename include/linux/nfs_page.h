/*
 * linux/include/linux/nfs_page.h
 *
 * Copyright (C) 2000 Trond Myklebust
 *
 * NFS page cache wrapper.
 */

#ifndef _LINUX_NFS_PAGE_H
#define _LINUX_NFS_PAGE_H


#include <linux/list.h>
#include <linux/pagemap.h>
#include <linux/wait.h>
#include <linux/sunrpc/auth.h>
#include <linux/nfs_xdr.h>

#include <linux/kref.h>

/*
 * Valid flags for a dirty buffer
 */
enum {
	PG_BUSY = 0,
	PG_MAPPED,
	PG_CLEAN,
	PG_NEED_COMMIT,
	PG_NEED_RESCHED,
	PG_COMMIT_TO_DS,
};

struct nfs_inode;
struct nfs_page {
	struct list_head	wb_list;	/* Defines state of page: */
	struct page		*wb_page;	/* page to read in/write out */
	struct nfs_open_context	*wb_context;	/* File state context info */
	struct nfs_lock_context	*wb_lock_context;	/* lock context info */
	pgoff_t			wb_index;	/* Offset >> PAGE_CACHE_SHIFT */
	unsigned int		wb_offset,	/* Offset & ~PAGE_CACHE_MASK */
				wb_pgbase,	/* Start of page data */
				wb_bytes;	/* Length of request */
	struct kref		wb_kref;	/* reference count */
	unsigned long		wb_flags;
	struct nfs_write_verifier	wb_verf;	/* Commit cookie */
};

struct nfs_pageio_descriptor;
struct nfs_pageio_ops {
	void	(*pg_init)(struct nfs_pageio_descriptor *, struct nfs_page *);
	bool	(*pg_test)(struct nfs_pageio_descriptor *, struct nfs_page *, struct nfs_page *);
	int	(*pg_doio)(struct nfs_pageio_descriptor *);
};

struct nfs_pageio_descriptor {
	struct list_head	pg_list;
	unsigned long		pg_bytes_written;
	size_t			pg_count;
	size_t			pg_bsize;
	unsigned int		pg_base;
	unsigned char		pg_moreio : 1,
				pg_recoalesce : 1;

	struct inode		*pg_inode;
	const struct nfs_pageio_ops *pg_ops;
	int 			pg_ioflags;
	int			pg_error;
	const struct rpc_call_ops *pg_rpc_callops;
	const struct nfs_pgio_completion_ops *pg_completion_ops;
	struct pnfs_layout_segment *pg_lseg;
	struct nfs_direct_req	*pg_dreq;
};

#define NFS_WBACK_BUSY(req)	(test_bit(PG_BUSY,&(req)->wb_flags))

extern	struct nfs_page *nfs_create_request(struct nfs_open_context *ctx,
					    struct inode *inode,
					    struct page *page,
					    unsigned int offset,
					    unsigned int count);
extern	void nfs_release_request(struct nfs_page *req);


extern	void nfs_pageio_init(struct nfs_pageio_descriptor *desc,
			     struct inode *inode,
			     const struct nfs_pageio_ops *pg_ops,
			     const struct nfs_pgio_completion_ops *compl_ops,
			     size_t bsize,
			     int how);
extern	int nfs_pageio_add_request(struct nfs_pageio_descriptor *,
				   struct nfs_page *);
extern	void nfs_pageio_complete(struct nfs_pageio_descriptor *desc);
extern	void nfs_pageio_cond_complete(struct nfs_pageio_descriptor *, pgoff_t);
extern bool nfs_generic_pg_test(struct nfs_pageio_descriptor *desc,
				struct nfs_page *prev,
				struct nfs_page *req);
extern  int nfs_wait_on_request(struct nfs_page *);
extern	void nfs_unlock_request(struct nfs_page *req);
extern	void nfs_unlock_and_release_request(struct nfs_page *req);

/*
 * Lock the page of an asynchronous request
 */
static inline int
nfs_lock_request(struct nfs_page *req)
{
	return !test_and_set_bit(PG_BUSY, &req->wb_flags);
}

/**
 * nfs_list_add_request - Insert a request into a list
 * @req: request
 * @head: head of list into which to insert the request.
 */
static inline void
nfs_list_add_request(struct nfs_page *req, struct list_head *head)
{
	list_add_tail(&req->wb_list, head);
}


/**
 * nfs_list_remove_request - Remove a request from its wb_list
 * @req: request
 */
static inline void
nfs_list_remove_request(struct nfs_page *req)
{
	if (list_empty(&req->wb_list))
		return;
	list_del_init(&req->wb_list);
}

static inline struct nfs_page *
nfs_list_entry(struct list_head *head)
{
	return list_entry(head, struct nfs_page, wb_list);
}

static inline
loff_t req_offset(struct nfs_page *req)
{
	return (((loff_t)req->wb_index) << PAGE_CACHE_SHIFT) + req->wb_offset;
}

#endif /* _LINUX_NFS_PAGE_H */
