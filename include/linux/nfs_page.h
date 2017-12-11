/* SPDX-License-Identifier: GPL-2.0 */
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
	PG_BUSY = 0,		/* nfs_{un}lock_request */
	PG_MAPPED,		/* page private set for buffered io */
	PG_CLEAN,		/* write succeeded */
	PG_COMMIT_TO_DS,	/* used by pnfs layouts */
	PG_INODE_REF,		/* extra ref held by inode when in writeback */
	PG_HEADLOCK,		/* page group lock of wb_head */
	PG_TEARDOWN,		/* page group sync for destroy */
	PG_UNLOCKPAGE,		/* page group sync bit in read path */
	PG_UPTODATE,		/* page group sync bit in read path */
	PG_WB_END,		/* page group sync bit in write path */
	PG_REMOVE,		/* page group sync bit in write path */
	PG_CONTENDED1,		/* Is someone waiting for a lock? */
	PG_CONTENDED2,		/* Is someone waiting for a lock? */
};

struct nfs_inode;
struct nfs_page {
	struct list_head	wb_list;	/* Defines state of page: */
	struct page		*wb_page;	/* page to read in/write out */
	struct nfs_open_context	*wb_context;	/* File state context info */
	struct nfs_lock_context	*wb_lock_context;	/* lock context info */
	pgoff_t			wb_index;	/* Offset >> PAGE_SHIFT */
	unsigned int		wb_offset,	/* Offset & ~PAGE_MASK */
				wb_pgbase,	/* Start of page data */
				wb_bytes;	/* Length of request */
	struct kref		wb_kref;	/* reference count */
	unsigned long		wb_flags;
	struct nfs_write_verifier	wb_verf;	/* Commit cookie */
	struct nfs_page		*wb_this_page;  /* list of reqs for this page */
	struct nfs_page		*wb_head;       /* head pointer for req list */
};

struct nfs_pageio_descriptor;
struct nfs_pageio_ops {
	void	(*pg_init)(struct nfs_pageio_descriptor *, struct nfs_page *);
	size_t	(*pg_test)(struct nfs_pageio_descriptor *, struct nfs_page *,
			   struct nfs_page *);
	int	(*pg_doio)(struct nfs_pageio_descriptor *);
	unsigned int	(*pg_get_mirror_count)(struct nfs_pageio_descriptor *,
				       struct nfs_page *);
	void	(*pg_cleanup)(struct nfs_pageio_descriptor *);
};

struct nfs_rw_ops {
	struct nfs_pgio_header *(*rw_alloc_header)(void);
	void (*rw_free_header)(struct nfs_pgio_header *);
	int  (*rw_done)(struct rpc_task *, struct nfs_pgio_header *,
			struct inode *);
	void (*rw_result)(struct rpc_task *, struct nfs_pgio_header *);
	void (*rw_initiate)(struct nfs_pgio_header *, struct rpc_message *,
			    const struct nfs_rpc_ops *,
			    struct rpc_task_setup *, int);
};

struct nfs_pgio_mirror {
	struct list_head	pg_list;
	unsigned long		pg_bytes_written;
	size_t			pg_count;
	size_t			pg_bsize;
	unsigned int		pg_base;
	unsigned char		pg_recoalesce : 1;
};

struct nfs_pageio_descriptor {
	unsigned char		pg_moreio : 1;
	struct inode		*pg_inode;
	const struct nfs_pageio_ops *pg_ops;
	const struct nfs_rw_ops *pg_rw_ops;
	int 			pg_ioflags;
	int			pg_error;
	const struct rpc_call_ops *pg_rpc_callops;
	const struct nfs_pgio_completion_ops *pg_completion_ops;
	struct pnfs_layout_segment *pg_lseg;
	struct nfs_io_completion *pg_io_completion;
	struct nfs_direct_req	*pg_dreq;
	unsigned int		pg_bsize;	/* default bsize for mirrors */

	u32			pg_mirror_count;
	struct nfs_pgio_mirror	*pg_mirrors;
	struct nfs_pgio_mirror	pg_mirrors_static[1];
	struct nfs_pgio_mirror	*pg_mirrors_dynamic;
	u32			pg_mirror_idx;	/* current mirror */
};

/* arbitrarily selected limit to number of mirrors */
#define NFS_PAGEIO_DESCRIPTOR_MIRROR_MAX 16

#define NFS_WBACK_BUSY(req)	(test_bit(PG_BUSY,&(req)->wb_flags))

extern	struct nfs_page *nfs_create_request(struct nfs_open_context *ctx,
					    struct page *page,
					    struct nfs_page *last,
					    unsigned int offset,
					    unsigned int count);
extern	void nfs_release_request(struct nfs_page *);


extern	void nfs_pageio_init(struct nfs_pageio_descriptor *desc,
			     struct inode *inode,
			     const struct nfs_pageio_ops *pg_ops,
			     const struct nfs_pgio_completion_ops *compl_ops,
			     const struct nfs_rw_ops *rw_ops,
			     size_t bsize,
			     int how);
extern	int nfs_pageio_add_request(struct nfs_pageio_descriptor *,
				   struct nfs_page *);
extern  int nfs_pageio_resend(struct nfs_pageio_descriptor *,
			      struct nfs_pgio_header *);
extern	void nfs_pageio_complete(struct nfs_pageio_descriptor *desc);
extern	void nfs_pageio_cond_complete(struct nfs_pageio_descriptor *, pgoff_t);
extern size_t nfs_generic_pg_test(struct nfs_pageio_descriptor *desc,
				struct nfs_page *prev,
				struct nfs_page *req);
extern  int nfs_wait_on_request(struct nfs_page *);
extern	void nfs_unlock_request(struct nfs_page *req);
extern	void nfs_unlock_and_release_request(struct nfs_page *);
extern int nfs_page_group_lock(struct nfs_page *);
extern void nfs_page_group_unlock(struct nfs_page *);
extern bool nfs_page_group_sync_on_bit(struct nfs_page *, unsigned int);
extern bool nfs_async_iocounter_wait(struct rpc_task *, struct nfs_lock_context *);

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
	return (((loff_t)req->wb_index) << PAGE_SHIFT) + req->wb_offset;
}

#endif /* _LINUX_NFS_PAGE_H */
