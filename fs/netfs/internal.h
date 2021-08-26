/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Internal definitions for network filesystem support
 *
 * Copyright (C) 2021 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/netfs.h>
#include <trace/events/netfs.h>

#ifdef pr_fmt
#undef pr_fmt
#endif

#define pr_fmt(fmt) "netfs: " fmt

/*
 * objects.c
 */
struct netfs_io_request *netfs_alloc_request(struct address_space *mapping,
					     struct file *file,
					     const struct netfs_request_ops *ops,
					     void *netfs_priv,
					     loff_t start, size_t len,
					     enum netfs_io_origin origin);
void netfs_get_request(struct netfs_io_request *rreq, enum netfs_rreq_ref_trace what);
void netfs_clear_subrequests(struct netfs_io_request *rreq, bool was_async);
void netfs_put_request(struct netfs_io_request *rreq, bool was_async,
		       enum netfs_rreq_ref_trace what);
struct netfs_io_subrequest *netfs_alloc_subrequest(struct netfs_io_request *rreq);

static inline void netfs_see_request(struct netfs_io_request *rreq,
				     enum netfs_rreq_ref_trace what)
{
	trace_netfs_rreq_ref(rreq->debug_id, refcount_read(&rreq->ref), what);
}

/*
 * read_helper.c
 */
extern unsigned int netfs_debug;

void netfs_rreq_work(struct work_struct *work);

/*
 * stats.c
 */
#ifdef CONFIG_NETFS_STATS
extern atomic_t netfs_n_rh_readahead;
extern atomic_t netfs_n_rh_readpage;
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
