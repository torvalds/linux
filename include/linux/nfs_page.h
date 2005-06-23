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
#include <linux/nfs_fs_sb.h>
#include <linux/sunrpc/auth.h>
#include <linux/nfs_xdr.h>

#include <asm/atomic.h>

/*
 * Valid flags for the radix tree
 */
#define NFS_PAGE_TAG_DIRTY	0
#define NFS_PAGE_TAG_WRITEBACK	1

/*
 * Valid flags for a dirty buffer
 */
#define PG_BUSY			0
#define PG_NEED_COMMIT		1
#define PG_NEED_RESCHED		2

struct nfs_inode;
struct nfs_page {
	struct list_head	wb_list,	/* Defines state of page: */
				*wb_list_head;	/*      read/write/commit */
	struct page		*wb_page;	/* page to read in/write out */
	struct nfs_open_context	*wb_context;	/* File state context info */
	atomic_t		wb_complete;	/* i/os we're waiting for */
	unsigned long		wb_index;	/* Offset >> PAGE_CACHE_SHIFT */
	unsigned int		wb_offset,	/* Offset & ~PAGE_CACHE_MASK */
				wb_pgbase,	/* Start of page data */
				wb_bytes;	/* Length of request */
	atomic_t		wb_count;	/* reference count */
	unsigned long		wb_flags;
	struct nfs_writeverf	wb_verf;	/* Commit cookie */
};

#define NFS_WBACK_BUSY(req)	(test_bit(PG_BUSY,&(req)->wb_flags))
#define NFS_NEED_COMMIT(req)	(test_bit(PG_NEED_COMMIT,&(req)->wb_flags))
#define NFS_NEED_RESCHED(req)	(test_bit(PG_NEED_RESCHED,&(req)->wb_flags))

extern	struct nfs_page *nfs_create_request(struct nfs_open_context *ctx,
					    struct inode *inode,
					    struct page *page,
					    unsigned int offset,
					    unsigned int count);
extern	void nfs_clear_request(struct nfs_page *req);
extern	void nfs_release_request(struct nfs_page *req);


extern  int nfs_scan_lock_dirty(struct nfs_inode *nfsi, struct list_head *dst,
				unsigned long idx_start, unsigned int npages);
extern	int nfs_scan_list(struct list_head *, struct list_head *,
			  unsigned long, unsigned int);
extern	int nfs_coalesce_requests(struct list_head *, struct list_head *,
				  unsigned int);
extern  int nfs_wait_on_request(struct nfs_page *);
extern	void nfs_unlock_request(struct nfs_page *req);
extern  int nfs_set_page_writeback_locked(struct nfs_page *req);
extern  void nfs_clear_page_writeback(struct nfs_page *req);


/*
 * Lock the page of an asynchronous request without incrementing the wb_count
 */
static inline int
nfs_lock_request_dontget(struct nfs_page *req)
{
	if (test_and_set_bit(PG_BUSY, &req->wb_flags))
		return 0;
	return 1;
}

/*
 * Lock the page of an asynchronous request
 */
static inline int
nfs_lock_request(struct nfs_page *req)
{
	if (test_and_set_bit(PG_BUSY, &req->wb_flags))
		return 0;
	atomic_inc(&req->wb_count);
	return 1;
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
	req->wb_list_head = head;
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
	req->wb_list_head = NULL;
}

static inline int
nfs_defer_commit(struct nfs_page *req)
{
	if (test_and_set_bit(PG_NEED_COMMIT, &req->wb_flags))
		return 0;
	return 1;
}

static inline void
nfs_clear_commit(struct nfs_page *req)
{
	smp_mb__before_clear_bit();
	clear_bit(PG_NEED_COMMIT, &req->wb_flags);
	smp_mb__after_clear_bit();
}

static inline int
nfs_defer_reschedule(struct nfs_page *req)
{
	if (test_and_set_bit(PG_NEED_RESCHED, &req->wb_flags))
		return 0;
	return 1;
}

static inline void
nfs_clear_reschedule(struct nfs_page *req)
{
	smp_mb__before_clear_bit();
	clear_bit(PG_NEED_RESCHED, &req->wb_flags);
	smp_mb__after_clear_bit();
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
