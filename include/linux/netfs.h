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
#include <linux/rolling_buffer.h>

enum netfs_sreq_ref_trace;
typedef struct mempool mempool_t;
struct folio_queue;

/**
 * folio_start_private_2 - Start an fscache write on a folio.  [DEPRECATED]
 * @folio: The folio.
 *
 * Call this function before writing a folio to a local cache.  Starting a
 * second write before the first one finishes is not allowed.
 *
 * Note that this should no longer be used.
 */
static inline void folio_start_private_2(struct folio *folio)
{
	VM_BUG_ON_FOLIO(folio_test_private_2(folio), folio);
	folio_get(folio);
	folio_set_private_2(folio);
}

enum netfs_io_source {
	NETFS_SOURCE_UNKNOWN,
	NETFS_FILL_WITH_ZEROES,
	NETFS_DOWNLOAD_FROM_SERVER,
	NETFS_READ_FROM_CACHE,
	NETFS_INVALID_READ,
	NETFS_UPLOAD_TO_SERVER,
	NETFS_WRITE_TO_CACHE,
} __mode(byte);

typedef void (*netfs_io_terminated_t)(void *priv, ssize_t transferred_or_error);

/*
 * Per-inode context.  This wraps the VFS inode.
 */
struct netfs_inode {
	struct inode		inode;		/* The VFS inode */
	const struct netfs_request_ops *ops;
#if IS_ENABLED(CONFIG_FSCACHE)
	struct fscache_cookie	*cache;
#endif
	struct mutex		wb_lock;	/* Writeback serialisation */
	loff_t			remote_i_size;	/* Size of the remote file */
	loff_t			zero_point;	/* Size after which we assume there's no data
						 * on the server */
	atomic_t		io_count;	/* Number of outstanding reqs */
	unsigned long		flags;
#define NETFS_ICTX_ODIRECT	0		/* The file has DIO in progress */
#define NETFS_ICTX_UNBUFFERED	1		/* I/O should not use the pagecache */
#define NETFS_ICTX_MODIFIED_ATTR 3		/* Indicate change in mtime/ctime */
#define NETFS_ICTX_SINGLE_NO_UPLOAD 4		/* Monolithic payload, cache but no upload */
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
#define NETFS_FOLIO_COPY_TO_CACHE ((struct netfs_group *)0x356UL) /* Write to the cache only */

static inline bool netfs_is_folio_info(const void *priv)
{
	return (unsigned long)priv & NETFS_FOLIO_INFO;
}

static inline struct netfs_folio *__netfs_folio_info(const void *priv)
{
	if (netfs_is_folio_info(priv))
		return (struct netfs_folio *)((unsigned long)priv & ~NETFS_FOLIO_INFO);
	return NULL;
}

static inline struct netfs_folio *netfs_folio_info(struct folio *folio)
{
	return __netfs_folio_info(folio_get_private(folio));
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
 * Stream of I/O subrequests going to a particular destination, such as the
 * server or the local cache.  This is mainly intended for writing where we may
 * have to write to multiple destinations concurrently.
 */
struct netfs_io_stream {
	/* Submission tracking */
	struct netfs_io_subrequest *construct;	/* Op being constructed */
	size_t			sreq_max_len;	/* Maximum size of a subrequest */
	unsigned int		sreq_max_segs;	/* 0 or max number of segments in an iterator */
	unsigned int		submit_off;	/* Folio offset we're submitting from */
	unsigned int		submit_len;	/* Amount of data left to submit */
	unsigned int		submit_extendable_to; /* Amount I/O can be rounded up to */
	void (*prepare_write)(struct netfs_io_subrequest *subreq);
	void (*issue_write)(struct netfs_io_subrequest *subreq);
	/* Collection tracking */
	struct list_head	subrequests;	/* Contributory I/O operations */
	struct netfs_io_subrequest *front;	/* Op being collected */
	unsigned long long	collected_to;	/* Position we've collected results to */
	size_t			transferred;	/* The amount transferred from this stream */
	unsigned short		error;		/* Aggregate error for the stream */
	enum netfs_io_source	source;		/* Where to read from/write to */
	unsigned char		stream_nr;	/* Index of stream in parent table */
	bool			avail;		/* T if stream is available */
	bool			active;		/* T if stream is active */
	bool			need_retry;	/* T if this stream needs retrying */
	bool			failed;		/* T if this stream failed */
	bool			transferred_valid; /* T is ->transferred is valid */
};

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
	unsigned long long	start;		/* Where to start the I/O */
	size_t			len;		/* Size of the I/O */
	size_t			transferred;	/* Amount of data transferred */
	refcount_t		ref;
	short			error;		/* 0 or error that occurred */
	unsigned short		debug_index;	/* Index in list (for debugging output) */
	unsigned int		nr_segs;	/* Number of segs in io_iter */
	u8			retry_count;	/* The number of retries (0 on initial pass) */
	enum netfs_io_source	source;		/* Where to read from/write to */
	unsigned char		stream_nr;	/* I/O stream this belongs to */
	unsigned long		flags;
#define NETFS_SREQ_COPY_TO_CACHE	0	/* Set if should copy the data to the cache */
#define NETFS_SREQ_CLEAR_TAIL		1	/* Set if the rest of the read should be cleared */
#define NETFS_SREQ_MADE_PROGRESS	4	/* Set if we transferred at least some data */
#define NETFS_SREQ_ONDEMAND		5	/* Set if it's from on-demand read mode */
#define NETFS_SREQ_BOUNDARY		6	/* Set if ends on hard boundary (eg. ceph object) */
#define NETFS_SREQ_HIT_EOF		7	/* Set if short due to EOF */
#define NETFS_SREQ_IN_PROGRESS		8	/* Unlocked when the subrequest completes */
#define NETFS_SREQ_NEED_RETRY		9	/* Set if the filesystem requests a retry */
#define NETFS_SREQ_FAILED		10	/* Set if the subreq failed unretryably */
};

enum netfs_io_origin {
	NETFS_READAHEAD,		/* This read was triggered by readahead */
	NETFS_READPAGE,			/* This read is a synchronous read */
	NETFS_READ_GAPS,		/* This read is a synchronous read to fill gaps */
	NETFS_READ_SINGLE,		/* This read should be treated as a single object */
	NETFS_READ_FOR_WRITE,		/* This read is to prepare a write */
	NETFS_UNBUFFERED_READ,		/* This is an unbuffered read */
	NETFS_DIO_READ,			/* This is a direct I/O read */
	NETFS_WRITEBACK,		/* This write was triggered by writepages */
	NETFS_WRITEBACK_SINGLE,		/* This monolithic write was triggered by writepages */
	NETFS_WRITETHROUGH,		/* This write was made by netfs_perform_write() */
	NETFS_UNBUFFERED_WRITE,		/* This is an unbuffered write */
	NETFS_DIO_WRITE,		/* This is a direct I/O write */
	NETFS_PGPRIV2_COPY_TO_CACHE,	/* [DEPRECATED] This is writing read data to the cache */
	nr__netfs_io_origin
} __mode(byte);

/*
 * Descriptor for an I/O helper request.  This is used to make multiple I/O
 * operations to a variety of data stores and then stitch the result together.
 */
struct netfs_io_request {
	union {
		struct work_struct cleanup_work; /* Deferred cleanup work */
		struct rcu_head rcu;
	};
	struct work_struct	work;		/* Result collector work */
	struct inode		*inode;		/* The file being accessed */
	struct address_space	*mapping;	/* The mapping being accessed */
	struct kiocb		*iocb;		/* AIO completion vector */
	struct netfs_cache_resources cache_resources;
	struct netfs_io_request	*copy_to_cache;	/* Request to write just-read data to the cache */
#ifdef CONFIG_PROC_FS
	struct list_head	proc_link;	/* Link in netfs_iorequests */
#endif
	struct netfs_io_stream	io_streams[2];	/* Streams of parallel I/O operations */
#define NR_IO_STREAMS 2 //wreq->nr_io_streams
	struct netfs_group	*group;		/* Writeback group being written back */
	struct rolling_buffer	buffer;		/* Unencrypted buffer */
#define NETFS_ROLLBUF_PUT_MARK		ROLLBUF_MARK_1
#define NETFS_ROLLBUF_PAGECACHE_MARK	ROLLBUF_MARK_2
	wait_queue_head_t	waitq;		/* Processor waiter */
	void			*netfs_priv;	/* Private data for the netfs */
	void			*netfs_priv2;	/* Private data for the netfs */
	struct bio_vec		*direct_bv;	/* DIO buffer list (when handling iovec-iter) */
	unsigned long long	submitted;	/* Amount submitted for I/O so far */
	unsigned long long	len;		/* Length of the request */
	size_t			transferred;	/* Amount to be indicated as transferred */
	long			error;		/* 0 or error that occurred */
	unsigned long long	i_size;		/* Size of the file */
	unsigned long long	start;		/* Start position */
	atomic64_t		issued_to;	/* Write issuer folio cursor */
	unsigned long long	collected_to;	/* Point we've collected to */
	unsigned long long	cleaned_to;	/* Position we've cleaned folios to */
	unsigned long long	abandon_to;	/* Position to abandon folios to */
	pgoff_t			no_unlock_folio; /* Don't unlock this folio after read */
	unsigned int		direct_bv_count; /* Number of elements in direct_bv[] */
	unsigned int		debug_id;
	unsigned int		rsize;		/* Maximum read size (0 for none) */
	unsigned int		wsize;		/* Maximum write size (0 for none) */
	atomic_t		subreq_counter;	/* Next subreq->debug_index */
	unsigned int		nr_group_rel;	/* Number of refs to release on ->group */
	spinlock_t		lock;		/* Lock for queuing subreqs */
	unsigned char		front_folio_order; /* Order (size) of front folio */
	enum netfs_io_origin	origin;		/* Origin of the request */
	bool			direct_bv_unpin; /* T if direct_bv[] must be unpinned */
	refcount_t		ref;
	unsigned long		flags;
#define NETFS_RREQ_IN_PROGRESS		0	/* Unlocked when the request completes (has ref) */
#define NETFS_RREQ_ALL_QUEUED		1	/* All subreqs are now queued */
#define NETFS_RREQ_PAUSE		2	/* Pause subrequest generation */
#define NETFS_RREQ_FAILED		3	/* The request failed */
#define NETFS_RREQ_RETRYING		4	/* Set if we're in the retry path */
#define NETFS_RREQ_SHORT_TRANSFER	5	/* Set if we have a short transfer */
#define NETFS_RREQ_OFFLOAD_COLLECTION	8	/* Offload collection to workqueue */
#define NETFS_RREQ_NO_UNLOCK_FOLIO	9	/* Don't unlock no_unlock_folio on completion */
#define NETFS_RREQ_FOLIO_COPY_TO_CACHE	10	/* Copy current folio to cache from read */
#define NETFS_RREQ_UPLOAD_TO_SERVER	11	/* Need to write to the server */
#define NETFS_RREQ_USE_IO_ITER		12	/* Use ->io_iter rather than ->i_pages */
#define NETFS_RREQ_USE_PGPRIV2		31	/* [DEPRECATED] Use PG_private_2 to mark
						 * write to cache on read */
	const struct netfs_request_ops *netfs_ops;
};

/*
 * Operations the network filesystem can/must provide to the helpers.
 */
struct netfs_request_ops {
	mempool_t *request_pool;
	mempool_t *subrequest_pool;
	int (*init_request)(struct netfs_io_request *rreq, struct file *file);
	void (*free_request)(struct netfs_io_request *rreq);
	void (*free_subrequest)(struct netfs_io_subrequest *rreq);

	/* Read request handling */
	void (*expand_readahead)(struct netfs_io_request *rreq);
	int (*prepare_read)(struct netfs_io_subrequest *subreq);
	void (*issue_read)(struct netfs_io_subrequest *subreq);
	bool (*is_still_valid)(struct netfs_io_request *rreq);
	int (*check_write_begin)(struct file *file, loff_t pos, unsigned len,
				 struct folio **foliop, void **_fsdata);
	void (*done)(struct netfs_io_request *rreq);

	/* Modification handling */
	void (*update_i_size)(struct inode *inode, loff_t i_size);
	void (*post_modify)(struct inode *inode);

	/* Write request handling */
	void (*begin_writeback)(struct netfs_io_request *wreq);
	void (*prepare_write)(struct netfs_io_subrequest *subreq);
	void (*issue_write)(struct netfs_io_subrequest *subreq);
	void (*retry_request)(struct netfs_io_request *wreq, struct netfs_io_stream *stream);
	void (*invalidate_cache)(struct netfs_io_request *wreq);
};

/*
 * How to handle reading from a hole.
 */
enum netfs_read_from_hole {
	NETFS_READ_HOLE_IGNORE,
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

	/* Write data to the cache from a netfs subrequest. */
	void (*issue_write)(struct netfs_io_subrequest *subreq);

	/* Expand readahead request */
	void (*expand_readahead)(struct netfs_cache_resources *cres,
				 unsigned long long *_start,
				 unsigned long long *_len,
				 unsigned long long i_size);

	/* Prepare a read operation, shortening it to a cached/uncached
	 * boundary as appropriate.
	 */
	enum netfs_io_source (*prepare_read)(struct netfs_io_subrequest *subreq,
					     unsigned long long i_size);

	/* Prepare a write subrequest, working out if we're allowed to do it
	 * and finding out the maximum amount of data to gather before
	 * attempting to submit.  If we're not permitted to do it, the
	 * subrequest should be marked failed.
	 */
	void (*prepare_write_subreq)(struct netfs_io_subrequest *subreq);

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
ssize_t netfs_unbuffered_read_iter_locked(struct kiocb *iocb, struct iov_iter *iter);
ssize_t netfs_unbuffered_read_iter(struct kiocb *iocb, struct iov_iter *iter);
ssize_t netfs_buffered_read_iter(struct kiocb *iocb, struct iov_iter *iter);
ssize_t netfs_file_read_iter(struct kiocb *iocb, struct iov_iter *iter);

/* High-level write API */
ssize_t netfs_perform_write(struct kiocb *iocb, struct iov_iter *iter,
			    struct netfs_group *netfs_group);
ssize_t netfs_buffered_write_iter_locked(struct kiocb *iocb, struct iov_iter *from,
					 struct netfs_group *netfs_group);
ssize_t netfs_unbuffered_write_iter(struct kiocb *iocb, struct iov_iter *from);
ssize_t netfs_unbuffered_write_iter_locked(struct kiocb *iocb, struct iov_iter *iter,
					   struct netfs_group *netfs_group);
ssize_t netfs_file_write_iter(struct kiocb *iocb, struct iov_iter *from);

/* Single, monolithic object read/write API. */
void netfs_single_mark_inode_dirty(struct inode *inode);
ssize_t netfs_read_single(struct inode *inode, struct file *file, struct iov_iter *iter);
int netfs_writeback_single(struct address_space *mapping,
			   struct writeback_control *wbc,
			   struct iov_iter *iter);

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

/* VMA operations API. */
vm_fault_t netfs_page_mkwrite(struct vm_fault *vmf, struct netfs_group *netfs_group);

/* (Sub)request management API. */
void netfs_read_subreq_progress(struct netfs_io_subrequest *subreq);
void netfs_read_subreq_terminated(struct netfs_io_subrequest *subreq);
void netfs_get_subrequest(struct netfs_io_subrequest *subreq,
			  enum netfs_sreq_ref_trace what);
void netfs_put_subrequest(struct netfs_io_subrequest *subreq,
			  enum netfs_sreq_ref_trace what);
ssize_t netfs_extract_user_iter(struct iov_iter *orig, size_t orig_len,
				struct iov_iter *new,
				iov_iter_extraction_t extraction_flags);
size_t netfs_limit_iter(const struct iov_iter *iter, size_t start_offset,
			size_t max_size, size_t max_segs);
void netfs_prepare_write_failed(struct netfs_io_subrequest *subreq);
void netfs_write_subrequest_terminated(void *_op, ssize_t transferred_or_error);

int netfs_start_io_read(struct inode *inode);
void netfs_end_io_read(struct inode *inode);
int netfs_start_io_write(struct inode *inode);
void netfs_end_io_write(struct inode *inode);
int netfs_start_io_direct(struct inode *inode);
void netfs_end_io_direct(struct inode *inode);

/* Miscellaneous APIs. */
struct folio_queue *netfs_folioq_alloc(unsigned int rreq_id, gfp_t gfp,
				       unsigned int trace /*enum netfs_folioq_trace*/);
void netfs_folioq_free(struct folio_queue *folioq,
		       unsigned int trace /*enum netfs_trace_folioq*/);

/* Buffer wrangling helpers API. */
int netfs_alloc_folioq_buffer(struct address_space *mapping,
			      struct folio_queue **_buffer,
			      size_t *_cur_size, ssize_t size, gfp_t gfp);
void netfs_free_folioq_buffer(struct folio_queue *fq);

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
	atomic_set(&ctx->io_count, 0);
#if IS_ENABLED(CONFIG_FSCACHE)
	ctx->cache = NULL;
#endif
	mutex_init(&ctx->wb_lock);
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

/**
 * netfs_wait_for_outstanding_io - Wait for outstanding I/O to complete
 * @inode: The netfs inode to wait on
 *
 * Wait for outstanding I/O requests of any type to complete.  This is intended
 * to be called from inode eviction routines.  This makes sure that any
 * resources held by those requests are cleaned up before we let the inode get
 * cleaned up.
 */
static inline void netfs_wait_for_outstanding_io(struct inode *inode)
{
	struct netfs_inode *ictx = netfs_inode(inode);

	wait_var_event(&ictx->io_count, atomic_read(&ictx->io_count) == 0);
}

#endif /* _LINUX_NETFS_H */
