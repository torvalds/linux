/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Internal definitions for network filesystem support
 *
 * Copyright (C) 2021 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/folio_queue.h>
#include <linux/netfs.h>
#include <linux/fscache.h>
#include <linux/fscache-cache.h>
#include <trace/events/netfs.h>
#include <trace/events/fscache.h>

#ifdef pr_fmt
#undef pr_fmt
#endif

#define pr_fmt(fmt) "netfs: " fmt

/*
 * buffered_read.c
 */
void netfs_cache_read_terminated(void *priv, ssize_t transferred_or_error);
int netfs_prefetch_for_write(struct file *file, struct folio *folio,
			     size_t offset, size_t len);

/*
 * main.c
 */
extern unsigned int netfs_debug;
extern struct list_head netfs_io_requests;
extern spinlock_t netfs_proc_lock;
extern mempool_t netfs_request_pool;
extern mempool_t netfs_subrequest_pool;

#ifdef CONFIG_PROC_FS
static inline void netfs_proc_add_rreq(struct netfs_io_request *rreq)
{
	spin_lock(&netfs_proc_lock);
	list_add_tail_rcu(&rreq->proc_link, &netfs_io_requests);
	spin_unlock(&netfs_proc_lock);
}
static inline void netfs_proc_del_rreq(struct netfs_io_request *rreq)
{
	if (!list_empty(&rreq->proc_link)) {
		spin_lock(&netfs_proc_lock);
		list_del_rcu(&rreq->proc_link);
		spin_unlock(&netfs_proc_lock);
	}
}
#else
static inline void netfs_proc_add_rreq(struct netfs_io_request *rreq) {}
static inline void netfs_proc_del_rreq(struct netfs_io_request *rreq) {}
#endif

/*
 * misc.c
 */
struct folio_queue *netfs_buffer_make_space(struct netfs_io_request *rreq,
					    enum netfs_folioq_trace trace);
void netfs_reset_iter(struct netfs_io_subrequest *subreq);
void netfs_wake_collector(struct netfs_io_request *rreq);
void netfs_subreq_clear_in_progress(struct netfs_io_subrequest *subreq);
void netfs_wait_for_in_progress_stream(struct netfs_io_request *rreq,
				       struct netfs_io_stream *stream);
ssize_t netfs_wait_for_read(struct netfs_io_request *rreq);
ssize_t netfs_wait_for_write(struct netfs_io_request *rreq);
void netfs_wait_for_paused_read(struct netfs_io_request *rreq);
void netfs_wait_for_paused_write(struct netfs_io_request *rreq);

/*
 * objects.c
 */
struct netfs_io_request *netfs_alloc_request(struct address_space *mapping,
					     struct file *file,
					     loff_t start, size_t len,
					     enum netfs_io_origin origin);
void netfs_get_request(struct netfs_io_request *rreq, enum netfs_rreq_ref_trace what);
void netfs_clear_subrequests(struct netfs_io_request *rreq);
void netfs_put_request(struct netfs_io_request *rreq, enum netfs_rreq_ref_trace what);
struct netfs_io_subrequest *netfs_alloc_subrequest(struct netfs_io_request *rreq);

static inline void netfs_see_request(struct netfs_io_request *rreq,
				     enum netfs_rreq_ref_trace what)
{
	trace_netfs_rreq_ref(rreq->debug_id, refcount_read(&rreq->ref), what);
}

static inline void netfs_see_subrequest(struct netfs_io_subrequest *subreq,
					enum netfs_sreq_ref_trace what)
{
	trace_netfs_sreq_ref(subreq->rreq->debug_id, subreq->debug_index,
			     refcount_read(&subreq->ref), what);
}

/*
 * read_collect.c
 */
bool netfs_read_collection(struct netfs_io_request *rreq);
void netfs_read_collection_worker(struct work_struct *work);
void netfs_cache_read_terminated(void *priv, ssize_t transferred_or_error);

/*
 * read_pgpriv2.c
 */
void netfs_pgpriv2_copy_to_cache(struct netfs_io_request *rreq, struct folio *folio);
void netfs_pgpriv2_end_copy_to_cache(struct netfs_io_request *rreq);
bool netfs_pgpriv2_unlock_copied_folios(struct netfs_io_request *wreq);

/*
 * read_retry.c
 */
void netfs_retry_reads(struct netfs_io_request *rreq);
void netfs_unlock_abandoned_read_pages(struct netfs_io_request *rreq);

/*
 * stats.c
 */
#ifdef CONFIG_NETFS_STATS
extern atomic_t netfs_n_rh_dio_read;
extern atomic_t netfs_n_rh_readahead;
extern atomic_t netfs_n_rh_read_folio;
extern atomic_t netfs_n_rh_read_single;
extern atomic_t netfs_n_rh_rreq;
extern atomic_t netfs_n_rh_sreq;
extern atomic_t netfs_n_rh_download;
extern atomic_t netfs_n_rh_download_done;
extern atomic_t netfs_n_rh_download_failed;
extern atomic_t netfs_n_rh_download_instead;
extern atomic_t netfs_n_rh_read;
extern atomic_t netfs_n_rh_read_done;
extern atomic_t netfs_n_rh_read_failed;
extern atomic_t netfs_n_rh_zero;
extern atomic_t netfs_n_rh_short_read;
extern atomic_t netfs_n_rh_write;
extern atomic_t netfs_n_rh_write_begin;
extern atomic_t netfs_n_rh_write_done;
extern atomic_t netfs_n_rh_write_failed;
extern atomic_t netfs_n_rh_write_zskip;
extern atomic_t netfs_n_rh_retry_read_req;
extern atomic_t netfs_n_rh_retry_read_subreq;
extern atomic_t netfs_n_wh_buffered_write;
extern atomic_t netfs_n_wh_writethrough;
extern atomic_t netfs_n_wh_dio_write;
extern atomic_t netfs_n_wh_writepages;
extern atomic_t netfs_n_wh_copy_to_cache;
extern atomic_t netfs_n_wh_wstream_conflict;
extern atomic_t netfs_n_wh_upload;
extern atomic_t netfs_n_wh_upload_done;
extern atomic_t netfs_n_wh_upload_failed;
extern atomic_t netfs_n_wh_write;
extern atomic_t netfs_n_wh_write_done;
extern atomic_t netfs_n_wh_write_failed;
extern atomic_t netfs_n_wh_retry_write_req;
extern atomic_t netfs_n_wh_retry_write_subreq;
extern atomic_t netfs_n_wb_lock_skip;
extern atomic_t netfs_n_wb_lock_wait;
extern atomic_t netfs_n_folioq;

int netfs_stats_show(struct seq_file *m, void *v);

static inline void netfs_stat(atomic_t *stat)
{
	atomic_inc(stat);
}

static inline void netfs_stat_d(atomic_t *stat)
{
	atomic_dec(stat);
}

#else
#define netfs_stat(x) do {} while(0)
#define netfs_stat_d(x) do {} while(0)
#endif

/*
 * write_collect.c
 */
int netfs_folio_written_back(struct folio *folio);
bool netfs_write_collection(struct netfs_io_request *wreq);
void netfs_write_collection_worker(struct work_struct *work);

/*
 * write_issue.c
 */
struct netfs_io_request *netfs_create_write_req(struct address_space *mapping,
						struct file *file,
						loff_t start,
						enum netfs_io_origin origin);
void netfs_reissue_write(struct netfs_io_stream *stream,
			 struct netfs_io_subrequest *subreq,
			 struct iov_iter *source);
void netfs_issue_write(struct netfs_io_request *wreq,
		       struct netfs_io_stream *stream);
size_t netfs_advance_write(struct netfs_io_request *wreq,
			   struct netfs_io_stream *stream,
			   loff_t start, size_t len, bool to_eof);
struct netfs_io_request *netfs_begin_writethrough(struct kiocb *iocb, size_t len);
int netfs_advance_writethrough(struct netfs_io_request *wreq, struct writeback_control *wbc,
			       struct folio *folio, size_t copied, bool to_page_end,
			       struct folio **writethrough_cache);
ssize_t netfs_end_writethrough(struct netfs_io_request *wreq, struct writeback_control *wbc,
			       struct folio *writethrough_cache);
int netfs_unbuffered_write(struct netfs_io_request *wreq, bool may_wait, size_t len);

/*
 * write_retry.c
 */
void netfs_retry_writes(struct netfs_io_request *wreq);

/*
 * Miscellaneous functions.
 */
static inline bool netfs_is_cache_enabled(struct netfs_inode *ctx)
{
#if IS_ENABLED(CONFIG_FSCACHE)
	struct fscache_cookie *cookie = ctx->cache;

	return fscache_cookie_valid(cookie) && cookie->cache_priv &&
		fscache_cookie_enabled(cookie);
#else
	return false;
#endif
}

/*
 * Get a ref on a netfs group attached to a dirty page (e.g. a ceph snap).
 */
static inline struct netfs_group *netfs_get_group(struct netfs_group *netfs_group)
{
	if (netfs_group && netfs_group != NETFS_FOLIO_COPY_TO_CACHE)
		refcount_inc(&netfs_group->ref);
	return netfs_group;
}

/*
 * Dispose of a netfs group attached to a dirty page (e.g. a ceph snap).
 */
static inline void netfs_put_group(struct netfs_group *netfs_group)
{
	if (netfs_group &&
	    netfs_group != NETFS_FOLIO_COPY_TO_CACHE &&
	    refcount_dec_and_test(&netfs_group->ref))
		netfs_group->free(netfs_group);
}

/*
 * Dispose of a netfs group attached to a dirty page (e.g. a ceph snap).
 */
static inline void netfs_put_group_many(struct netfs_group *netfs_group, int nr)
{
	if (netfs_group &&
	    netfs_group != NETFS_FOLIO_COPY_TO_CACHE &&
	    refcount_sub_and_test(nr, &netfs_group->ref))
		netfs_group->free(netfs_group);
}

/*
 * Clear and wake up a NETFS_RREQ_* flag bit on a request.
 */
static inline void netfs_wake_rreq_flag(struct netfs_io_request *rreq,
					unsigned int rreq_flag,
					enum netfs_rreq_trace trace)
{
	if (test_bit(rreq_flag, &rreq->flags)) {
		trace_netfs_rreq(rreq, trace);
		clear_bit_unlock(rreq_flag, &rreq->flags);
		smp_mb__after_atomic(); /* Set flag before task state */
		wake_up(&rreq->waitq);
	}
}

/*
 * fscache-cache.c
 */
#ifdef CONFIG_PROC_FS
extern const struct seq_operations fscache_caches_seq_ops;
#endif
bool fscache_begin_cache_access(struct fscache_cache *cache, enum fscache_access_trace why);
void fscache_end_cache_access(struct fscache_cache *cache, enum fscache_access_trace why);
struct fscache_cache *fscache_lookup_cache(const char *name, bool is_cache);
void fscache_put_cache(struct fscache_cache *cache, enum fscache_cache_trace where);

static inline enum fscache_cache_state fscache_cache_state(const struct fscache_cache *cache)
{
	return smp_load_acquire(&cache->state);
}

static inline bool fscache_cache_is_live(const struct fscache_cache *cache)
{
	return fscache_cache_state(cache) == FSCACHE_CACHE_IS_ACTIVE;
}

static inline void fscache_set_cache_state(struct fscache_cache *cache,
					   enum fscache_cache_state new_state)
{
	smp_store_release(&cache->state, new_state);

}

static inline bool fscache_set_cache_state_maybe(struct fscache_cache *cache,
						 enum fscache_cache_state old_state,
						 enum fscache_cache_state new_state)
{
	return try_cmpxchg_release(&cache->state, &old_state, new_state);
}

/*
 * fscache-cookie.c
 */
extern struct kmem_cache *fscache_cookie_jar;
#ifdef CONFIG_PROC_FS
extern const struct seq_operations fscache_cookies_seq_ops;
#endif
extern struct timer_list fscache_cookie_lru_timer;

extern void fscache_print_cookie(struct fscache_cookie *cookie, char prefix);
extern bool fscache_begin_cookie_access(struct fscache_cookie *cookie,
					enum fscache_access_trace why);

static inline void fscache_see_cookie(struct fscache_cookie *cookie,
				      enum fscache_cookie_trace where)
{
	trace_fscache_cookie(cookie->debug_id, refcount_read(&cookie->ref),
			     where);
}

/*
 * fscache-main.c
 */
extern unsigned int fscache_hash(unsigned int salt, const void *data, size_t len);
#ifdef CONFIG_FSCACHE
int __init fscache_init(void);
void __exit fscache_exit(void);
#else
static inline int fscache_init(void) { return 0; }
static inline void fscache_exit(void) {}
#endif

/*
 * fscache-proc.c
 */
#ifdef CONFIG_PROC_FS
extern int __init fscache_proc_init(void);
extern void fscache_proc_cleanup(void);
#else
#define fscache_proc_init()	(0)
#define fscache_proc_cleanup()	do {} while (0)
#endif

/*
 * fscache-stats.c
 */
#ifdef CONFIG_FSCACHE_STATS
extern atomic_t fscache_n_volumes;
extern atomic_t fscache_n_volumes_collision;
extern atomic_t fscache_n_volumes_nomem;
extern atomic_t fscache_n_cookies;
extern atomic_t fscache_n_cookies_lru;
extern atomic_t fscache_n_cookies_lru_expired;
extern atomic_t fscache_n_cookies_lru_removed;
extern atomic_t fscache_n_cookies_lru_dropped;

extern atomic_t fscache_n_acquires;
extern atomic_t fscache_n_acquires_ok;
extern atomic_t fscache_n_acquires_oom;

extern atomic_t fscache_n_invalidates;

extern atomic_t fscache_n_relinquishes;
extern atomic_t fscache_n_relinquishes_retire;
extern atomic_t fscache_n_relinquishes_dropped;

extern atomic_t fscache_n_resizes;
extern atomic_t fscache_n_resizes_null;

static inline void fscache_stat(atomic_t *stat)
{
	atomic_inc(stat);
}

static inline void fscache_stat_d(atomic_t *stat)
{
	atomic_dec(stat);
}

#define __fscache_stat(stat) (stat)

int fscache_stats_show(struct seq_file *m);
#else

#define __fscache_stat(stat) (NULL)
#define fscache_stat(stat) do {} while (0)
#define fscache_stat_d(stat) do {} while (0)

static inline int fscache_stats_show(struct seq_file *m) { return 0; }
#endif

/*
 * fscache-volume.c
 */
#ifdef CONFIG_PROC_FS
extern const struct seq_operations fscache_volumes_seq_ops;
#endif

struct fscache_volume *fscache_get_volume(struct fscache_volume *volume,
					  enum fscache_volume_trace where);
bool fscache_begin_volume_access(struct fscache_volume *volume,
				 struct fscache_cookie *cookie,
				 enum fscache_access_trace why);
void fscache_create_volume(struct fscache_volume *volume, bool wait);

/*****************************************************************************/
/*
 * debug tracing
 */
#define dbgprintk(FMT, ...) \
	printk("[%-6.6s] "FMT"\n", current->comm, ##__VA_ARGS__)

#define kenter(FMT, ...) dbgprintk("==> %s("FMT")", __func__, ##__VA_ARGS__)
#define kleave(FMT, ...) dbgprintk("<== %s()"FMT"", __func__, ##__VA_ARGS__)
#define kdebug(FMT, ...) dbgprintk(FMT, ##__VA_ARGS__)

#ifdef __KDEBUG
#define _enter(FMT, ...) kenter(FMT, ##__VA_ARGS__)
#define _leave(FMT, ...) kleave(FMT, ##__VA_ARGS__)
#define _debug(FMT, ...) kdebug(FMT, ##__VA_ARGS__)

#elif defined(CONFIG_NETFS_DEBUG)
#define _enter(FMT, ...)			\
do {						\
	if (netfs_debug)			\
		kenter(FMT, ##__VA_ARGS__);	\
} while (0)

#define _leave(FMT, ...)			\
do {						\
	if (netfs_debug)			\
		kleave(FMT, ##__VA_ARGS__);	\
} while (0)

#define _debug(FMT, ...)			\
do {						\
	if (netfs_debug)			\
		kdebug(FMT, ##__VA_ARGS__);	\
} while (0)

#else
#define _enter(FMT, ...) no_printk("==> %s("FMT")", __func__, ##__VA_ARGS__)
#define _leave(FMT, ...) no_printk("<== %s()"FMT"", __func__, ##__VA_ARGS__)
#define _debug(FMT, ...) no_printk(FMT, ##__VA_ARGS__)
#endif

/*
 * assertions
 */
#if 1 /* defined(__KDEBUGALL) */

#define ASSERT(X)							\
do {									\
	if (unlikely(!(X))) {						\
		pr_err("\n");					\
		pr_err("Assertion failed\n");	\
		BUG();							\
	}								\
} while (0)

#define ASSERTCMP(X, OP, Y)						\
do {									\
	if (unlikely(!((X) OP (Y)))) {					\
		pr_err("\n");					\
		pr_err("Assertion failed\n");	\
		pr_err("%lx " #OP " %lx is false\n",		\
		       (unsigned long)(X), (unsigned long)(Y));		\
		BUG();							\
	}								\
} while (0)

#define ASSERTIF(C, X)							\
do {									\
	if (unlikely((C) && !(X))) {					\
		pr_err("\n");					\
		pr_err("Assertion failed\n");	\
		BUG();							\
	}								\
} while (0)

#define ASSERTIFCMP(C, X, OP, Y)					\
do {									\
	if (unlikely((C) && !((X) OP (Y)))) {				\
		pr_err("\n");					\
		pr_err("Assertion failed\n");	\
		pr_err("%lx " #OP " %lx is false\n",		\
		       (unsigned long)(X), (unsigned long)(Y));		\
		BUG();							\
	}								\
} while (0)

#else

#define ASSERT(X)			do {} while (0)
#define ASSERTCMP(X, OP, Y)		do {} while (0)
#define ASSERTIF(C, X)			do {} while (0)
#define ASSERTIFCMP(C, X, OP, Y)	do {} while (0)

#endif /* assert or not */
