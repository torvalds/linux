/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Network filesystem support services.
 *
 * Copyright (C) 2021 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * See:
 *
 *	Documentation/filesystems/netfs_library.rst
 *
 * for a description of the network filesystem interface declared here.
 */

#ifndef _LINUX_NETFS_H
#define _LINUX_NETFS_H

#include <linux/workqueue.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/uio.h>

enum netfs_sreq_ref_trace;

/*
 * Overload PG_private_2 to give us PG_fscache - this is used to indicate that
 * a page is currently backed by a local disk cache
 */
#define folio_test_fscache(folio)	folio_test_private_2(folio)
#define PageFsCache(page)		PagePrivate2((page))
#define SetPageFsCache(page)		SetPagePrivate2((page))
#define ClearPageFsCache(page)		ClearPagePrivate2((page))
#define TestSetPageFsCache(page)	TestSetPagePrivate2((page))
#define TestClearPageFsCache(page)	TestClearPagePrivate2((page))

/**
 * folio_start_fscache - Start an fscache write on a folio.
 * @folio: The folio.
 *
 * Call this function before writing a folio to a local cache.  Starting a
 * second write before the first one finishes is not allowed.
 */
static inline void folio_start_fscache(struct folio *folio)
{
	VM_BUG_ON_FOLIO(folio_test_private_2(folio), folio);
	folio_get(folio);
	folio_set_private_2(folio);
}

/**
 * folio_end_fscache - End an fscache write on a folio.
 * @folio: The folio.
 *
 * Call this function after the folio has been written to the local cache.
 * This will wake any sleepers waiting on this folio.
 */
static inline void folio_end_fscache(struct folio *folio)
{
	folio_end_private_2(folio);
}

/**
 * folio_wait_fscache - Wait for an fscache write on this folio to end.
 * @folio: The folio.
 *
 * If this folio is currently being written to a local cache, wait for
 * the write to finish.  Another write may start after this one finishes,
 * unless the caller holds the folio lock.
 */
static inline void folio_wait_fscache(struct folio *folio)
{
	folio_wait_private_2(folio);
}

/**
 * folio_wait_fscache_killable - Wait for an fscache write on this folio to end.
 * @folio: The folio.
 *
 * If this folio is currently being written to a local cache, wait
 * for the write to finish or for a fatal signal to be received.
 * Another write may start after this one finishes, unless the caller
 * holds the folio lock.
 *
 * Return:
 * - 0 if successful.
 * - -EINTR if a fatal signal was encountered.
 */
static inline int folio_wait_fscache_killable(struct folio *folio)
{
	return folio_wait_private_2_killable(folio);
}

static inline void set_page_fscache(struct page *page)
{
	folio_start_fscache(page_folio(page));
}

static inline void end_page_fscache(struct page *page)
{
	folio_end_private_2(page_folio(page));
}

static inline void wait_on_page_fscache(struct page *page)
{
	folio_wait_private_2(page_folio(page));
}

static inline int wait_on_page_fscache_killable(struct page *page)
{
	return folio_wait_private_2_killable(page_folio(page));
}

/* Marks used on xarray-based buffers */
#define NETFS_BUF_PUT_MARK	XA_MARK_0	/* - Page needs putting  */
#define NETFS_BUF_PAGECACHE_MARK XA_MARK_1	/* - Page needs wb/dirty flag wrangling */

enum netfs_io_source {
	NETFS_FILL_WITH_ZEROES,
	NETFS_DOWNLOAD_FROM_SERVER,
	NETFS_READ_FROM_CACHE,
	NETFS_INVALID_READ,
	NETFS_UPLOAD_TO_SERVER,
	NETFS_WRITE_TO_CACHE,
	NETFS_INVALID_WRITE,
} __mode(byte);

typedef void (*netfs_io_terminated_t)(void *priv, ssize_t transferred_or_error,
				      bool was_async);

/*
 * Per-inode context.  This wraps the VFS inode.
 */
struct netfs_inode {
	struct inode		inode;		/* The VFS inode */
	const struct netfs_request_ops *ops;
#if IS_ENABLED(CONFIG_FSCACHE)
	struct fscache_cookie	*cache;
#endif
	loff_t			remote_i_size;	/* Size of the remote file */
	loff_t			zero_point;	/* Size after which we assume there's no data
						 * on the server */
	unsigned long		flags;
#define NETFS_ICTX_ODIRECT	0		/* The file has DIO in progress */
#define NETFS_ICTX_UNBUFFERED	1		/* I/O should not use the pagecache */
#define NETFS_ICTX_WRITETHROUGH	2		/* Write-through caching */
#define NETFS_ICTX_NO_WRITE_STREAMING	3	/* Don't engage in write-streaming */
};

/*
 * A netfs group - for instance a ceph snap.  This is marked on dirty pages and
 * pages marked with a group must be flushed before they can be written under
 * the domain of another group.
 */
struct netfs_group {
	refcount_t		ref;
	void (*free)(struct netfs_group *netfs_group);
};

/*
 * Information about a dirty page (attached only if necessary).
 * folio->private
 */
struct netfs_folio {
	struct netfs_group	*netfs_group;	/* Filesystem's grouping marker (or NULL). */
	unsigned int		dirty_offset;	/* Write-streaming dirty data offset */
	unsigned int		dirty_len;	/* Write-streaming dirty data length */
};
#define NETFS_FOLIO_INFO	0x1UL	/* OR'd with folio->private. */

static inline struct netfs_folio *netfs_folio_info(struct folio *folio)
{
	void *priv = folio_get_private(folio);

	if ((unsigned long)priv & NETFS_FOLIO_INFO)
		return (struct netfs_folio *)((unsigned long)priv & ~NETFS_FOLIO_INFO);
	return NULL;
}

static inline struct netfs_group *netfs_folio_group(struct folio *folio)
{
	struct netfs_folio *finfo;
	void *priv = folio_get_private(folio);

	finfo = netfs_folio_info(folio);
	if (finfo)
		return finfo->netfs_group;
	return priv;
}

/*
 * Resources required to do operations on a cache.
 */
struct netfs_cache_resources {
	const struct netfs_cache_ops	*ops;
	void				*cache_priv;
	void				*cache_priv2;
	unsigned int			debug_id;	/* Cookie debug ID */
	unsigned int			inval_counter;	/* object->inval_counter at begin_op */
};

/*
 * Descriptor for a single component subrequest.  Each operation represents an
 * individual read/write from/to a server, a cache, a journal, etc..
 *
 * The buffer iterator is persistent for the life of the subrequest struct and
 * the pages it points to can be relied on to exist for the duration.
 */
struct netfs_io_subrequest {
	struct netfs_io_request *rreq;		/* Supervising I/O request */
	struct work_struct	work;
	struct list_head	rreq_link;	/* Link in rreq->subrequests */
	struct iov_iter		io_iter;	/* Iterator for this subrequest */
	loff_t			start;		/* Where to start the I/O */
	size_t			len;		/* Size of the I/O */
	size_t			transferred;	/* Amount of data transferred */
	refcount_t		ref;
	short			error;		/* 0 or error that occurred */
	unsigned short		debug_index;	/* Index in list (for debugging output) */
	unsigned int		max_nr_segs;	/* 0 or max number of segments in an iterator */
	enum netfs_io_source	source;		/* Where to read from/write to */
	unsigned long		flags;
#define NETFS_SREQ_COPY_TO_CACHE	0	/* Set if should copy the data to the cache */
#define NETFS_SREQ_CLEAR_TAIL		1	/* Set if the rest of the read should be cleared */
#define NETFS_SREQ_SHORT_IO		2	/* Set if the I/O was short */
#define NETFS_SREQ_SEEK_DATA_READ	3	/* Set if ->read() should SEEK_DATA first */
#define NETFS_SREQ_NO_PROGRESS		4	/* Set if we didn't manage to read any data */
#define NETFS_SREQ_ONDEMAND		5	/* Set if it's from on-demand read mode */
};

enum netfs_io_origin {
	NETFS_READAHEAD,		/* This read was triggered by readahead */
	NETFS_READPAGE,			/* This read is a synchronous read */
	NETFS_READ_FOR_WRITE,		/* This read is to prepare a write */
	NETFS_WRITEBACK,		/* This write was triggered by writepages */
	NETFS_WRITETHROUGH,		/* This write was made by netfs_perform_write() */
	NETFS_LAUNDER_WRITE,		/* This is triggered by ->launder_folio() */
	NETFS_UNBUFFERED_WRITE,		/* This is an unbuffered write */
	NETFS_DIO_READ,			/* This is a direct I/O read */
	NETFS_DIO_WRITE,		/* This is a direct I/O write */
	nr__netfs_io_origin
} __mode(byte);

/*
 * Descriptor for an I/O helper request.  This is used to make multiple I/O
 * operations to a variety of data stores and then stitch the result together.
 */
struct netfs_io_request {
	union {
		struct work_struct work;
		struct rcu_head rcu;
	};
	struct inode		*inode;		/* The file being accessed */
	struct address_space	*mapping;	/* The mapping being accessed */
	struct kiocb		*iocb;		/* AIO completion vector */
	struct netfs_cache_resources cache_resources;
	struct list_head	proc_link;	/* Link in netfs_iorequests */
	struct list_head	subrequests;	/* Contributory I/O operations */
	struct iov_iter		iter;		/* Unencrypted-side iterator */
	struct iov_iter		io_iter;	/* I/O (Encrypted-side) iterator */
	void			*netfs_priv;	/* Private data for the netfs */
	struct bio_vec		*direct_bv;	/* DIO buffer list (when handling iovec-iter) */
	unsigned int		direct_bv_count; /* Number of elements in direct_bv[] */
	unsigned int		debug_id;
	unsigned int		rsize;		/* Maximum read size (0 for none) */
	unsigned int		wsize;		/* Maximum write size (0 for none) */
	unsigned int		subreq_counter;	/* Next subreq->debug_index */
	atomic_t		nr_outstanding;	/* Number of ops in progress */
	atomic_t		nr_copy_ops;	/* Number of copy-to-cache ops in progress */
	size_t			submitted;	/* Amount submitted for I/O so far */
	size_t			len;		/* Length of the request */
	size_t			upper_len;	/* Length can be extended to here */
	size_t			transferred;	/* Amount to be indicated as transferred */
	short			error;		/* 0 or error that occurred */
	enum netfs_io_origin	origin;		/* Origin of the request */
	bool			direct_bv_unpin; /* T if direct_bv[] must be unpinned */
	loff_t			i_size;		/* Size of the file */
	loff_t			start;		/* Start position */
	pgoff_t			no_unlock_folio; /* Don't unlock this folio after read */
	refcount_t		ref;
	unsigned long		flags;
#define NETFS_RREQ_INCOMPLETE_IO	0	/* Some ioreqs terminated short or with error */
#define NETFS_RREQ_COPY_TO_CACHE	1	/* Need to write to the cache */
#define NETFS_RREQ_NO_UNLOCK_FOLIO	2	/* Don't unlock no_unlock_folio on completion */
#define NETFS_RREQ_DONT_UNLOCK_FOLIOS	3	/* Don't unlock the folios on completion */
#define NETFS_RREQ_FAILED		4	/* The request failed */
#define NETFS_RREQ_IN_PROGRESS		5	/* Unlocked when the request completes */
#define NETFS_RREQ_WRITE_TO_CACHE	7	/* Need to write to the cache */
#define NETFS_RREQ_UPLOAD_TO_SERVER	8	/* Need to write to the server */
#define NETFS_RREQ_NONBLOCK		9	/* Don't block if possible (O_NONBLOCK) */
#define NETFS_RREQ_BLOCKED		10	/* We blocked */
	const struct netfs_request_ops *netfs_ops;
	void (*cleanup)(struct netfs_io_request *req);
};

/*
 * Operations the network filesystem can/must provide to the helpers.
 */
struct netfs_request_ops {
	unsigned int	io_request_size;	/* Alloc size for netfs_io_request struct */
	unsigned int	io_subrequest_size;	/* Alloc size for netfs_io_subrequest struct */
	int (*init_request)(struct netfs_io_request *rreq, struct file *file);
	void (*free_request)(struct netfs_io_request *rreq);
	void (*free_subrequest)(struct netfs_io_subrequest *rreq);

	/* Read request handling */
	void (*expand_readahead)(struct netfs_io_request *rreq);
	bool (*clamp_length)(struct netfs_io_subrequest *subreq);
	void (*issue_read)(struct netfs_io_subrequest *subreq);
	bool (*is_still_valid)(struct netfs_io_request *rreq);
	int (*check_write_begin)(struct file *file, loff_t pos, unsigned len,
				 struct folio **foliop, void **_fsdata);
	void (*done)(struct netfs_io_request *rreq);

	/* Modification handling */
	void (*update_i_size)(struct inode *inode, loff_t i_size);

	/* Write request handling */
	void (*create_write_requests)(struct netfs_io_request *wreq,
				      loff_t start, size_t len);
	void (*invalidate_cache)(struct netfs_io_request *wreq);
};

/*
 * How to handle reading from a hole.
 */
enum netfs_read_from_hole {
	NETFS_READ_HOLE_IGNORE,
	NETFS_READ_HOLE_CLEAR,
	NETFS_READ_HOLE_FAIL,
};

/*
 * Table of operations for access to a cache.
 */
struct netfs_cache_ops {
	/* End an operation */
	void (*end_operation)(struct netfs_cache_resources *cres);

	/* Read data from the cache */
	int (*read)(struct netfs_cache_resources *cres,
		    loff_t start_pos,
		    struct iov_iter *iter,
		    enum netfs_read_from_hole read_hole,
		    netfs_io_terminated_t term_func,
		    void *term_func_priv);

	/* Write data to the cache */
	int (*write)(struct netfs_cache_resources *cres,
		     loff_t start_pos,
		     struct iov_iter *iter,
		     netfs_io_terminated_t term_func,
		     void *term_func_priv);

	/* Expand readahead request */
	void (*expand_readahead)(struct netfs_cache_resources *cres,
				 loff_t *_start, size_t *_len, loff_t i_size);

	/* Prepare a read operation, shortening it to a cached/uncached
	 * boundary as appropriate.
	 */
	enum netfs_io_source (*prepare_read)(struct netfs_io_subrequest *subreq,
					     loff_t i_size);

	/* Prepare a write operation, working out what part of the write we can
	 * actually do.
	 */
	int (*prepare_write)(struct netfs_cache_resources *cres,
			     loff_t *_start, size_t *_len, size_t upper_len,
			     loff_t i_size, bool no_space_allocated_yet);

	/* Prepare an on-demand read operation, shortening it to a cached/uncached
	 * boundary as appropriate.
	 */
	enum netfs_io_source (*prepare_ondemand_read)(struct netfs_cache_resources *cres,
						      loff_t start, size_t *_len,
						      loff_t i_size,
						      unsigned long *_flags, ino_t ino);

	/* Query the occupancy of the cache in a region, returning where the
	 * next chunk of data starts and how long it is.
	 */
	int (*query_occupancy)(struct netfs_cache_resources *cres,
			       loff_t start, size_t len, size_t granularity,
			       loff_t *_data_start, size_t *_data_len);
};

/* High-level read API. */
ssize_t netfs_unbuffered_read_iter(struct kiocb *iocb, struct iov_iter *iter);
ssize_t netfs_buffered_read_iter(struct kiocb *iocb, struct iov_iter *iter);
ssize_t netfs_file_read_iter(struct kiocb *iocb, struct iov_iter *iter);

/* High-level write API */
ssize_t netfs_perform_write(struct kiocb *iocb, struct iov_iter *iter,
			    struct netfs_group *netfs_group);
ssize_t netfs_buffered_write_iter_locked(struct kiocb *iocb, struct iov_iter *from,
					 struct netfs_group *netfs_group);
ssize_t netfs_unbuffered_write_iter(struct kiocb *iocb, struct iov_iter *from);
ssize_t netfs_file_write_iter(struct kiocb *iocb, struct iov_iter *from);

/* Address operations API */
struct readahead_control;
void netfs_readahead(struct readahead_control *);
int netfs_read_folio(struct file *, struct folio *);
int netfs_write_begin(struct netfs_inode *, struct file *,
		      struct address_space *, loff_t pos, unsigned int len,
		      struct folio **, void **fsdata);
int netfs_writepages(struct address_space *mapping,
		     struct writeback_control *wbc);
bool netfs_dirty_folio(struct address_space *mapping, struct folio *folio);
int netfs_unpin_writeback(struct inode *inode, struct writeback_control *wbc);
void netfs_clear_inode_writeback(struct inode *inode, const void *aux);
void netfs_invalidate_folio(struct folio *folio, size_t offset, size_t length);
bool netfs_release_folio(struct folio *folio, gfp_t gfp);
int netfs_launder_folio(struct folio *folio);

/* VMA operations API. */
vm_fault_t netfs_page_mkwrite(struct vm_fault *vmf, struct netfs_group *netfs_group);

/* (Sub)request management API. */
void netfs_subreq_terminated(struct netfs_io_subrequest *, ssize_t, bool);
void netfs_get_subrequest(struct netfs_io_subrequest *subreq,
			  enum netfs_sreq_ref_trace what);
void netfs_put_subrequest(struct netfs_io_subrequest *subreq,
			  bool was_async, enum netfs_sreq_ref_trace what);
ssize_t netfs_extract_user_iter(struct iov_iter *orig, size_t orig_len,
				struct iov_iter *new,
				iov_iter_extraction_t extraction_flags);
size_t netfs_limit_iter(const struct iov_iter *iter, size_t start_offset,
			size_t max_size, size_t max_segs);
struct netfs_io_subrequest *netfs_create_write_request(
	struct netfs_io_request *wreq, enum netfs_io_source dest,
	loff_t start, size_t len, work_func_t worker);
void netfs_write_subrequest_terminated(void *_op, ssize_t transferred_or_error,
				       bool was_async);
void netfs_queue_write_request(struct netfs_io_subrequest *subreq);

int netfs_start_io_read(struct inode *inode);
void netfs_end_io_read(struct inode *inode);
int netfs_start_io_write(struct inode *inode);
void netfs_end_io_write(struct inode *inode);
int netfs_start_io_direct(struct inode *inode);
void netfs_end_io_direct(struct inode *inode);

/**
 * netfs_inode - Get the netfs inode context from the inode
 * @inode: The inode to query
 *
 * Get the netfs lib inode context from the network filesystem's inode.  The
 * context struct is expected to directly follow on from the VFS inode struct.
 */
static inline struct netfs_inode *netfs_inode(struct inode *inode)
{
	return container_of(inode, struct netfs_inode, inode);
}

/**
 * netfs_inode_init - Initialise a netfslib inode context
 * @ctx: The netfs inode to initialise
 * @ops: The netfs's operations list
 * @use_zero_point: True to use the zero_point read optimisation
 *
 * Initialise the netfs library context struct.  This is expected to follow on
 * directly from the VFS inode struct.
 */
static inline void netfs_inode_init(struct netfs_inode *ctx,
				    const struct netfs_request_ops *ops,
				    bool use_zero_point)
{
	ctx->ops = ops;
	ctx->remote_i_size = i_size_read(&ctx->inode);
	ctx->zero_point = LLONG_MAX;
	ctx->flags = 0;
#if IS_ENABLED(CONFIG_FSCACHE)
	ctx->cache = NULL;
#endif
	/* ->releasepage() drives zero_point */
	if (use_zero_point) {
		ctx->zero_point = ctx->remote_i_size;
		mapping_set_release_always(ctx->inode.i_mapping);
	}
}

/**
 * netfs_resize_file - Note that a file got resized
 * @ctx: The netfs inode being resized
 * @new_i_size: The new file size
 * @changed_on_server: The change was applied to the server
 *
 * Inform the netfs lib that a file got resized so that it can adjust its state.
 */
static inline void netfs_resize_file(struct netfs_inode *ctx, loff_t new_i_size,
				     bool changed_on_server)
{
	if (changed_on_server)
		ctx->remote_i_size = new_i_size;
	if (new_i_size < ctx->zero_point)
		ctx->zero_point = new_i_size;
}

/**
 * netfs_i_cookie - Get the cache cookie from the inode
 * @ctx: The netfs inode to query
 *
 * Get the caching cookie (if enabled) from the network filesystem's inode.
 */
static inline struct fscache_cookie *netfs_i_cookie(struct netfs_inode *ctx)
{
#if IS_ENABLED(CONFIG_FSCACHE)
	return ctx->cache;
#else
	return NULL;
#endif
}

#endif /* _LINUX_NETFS_H */
