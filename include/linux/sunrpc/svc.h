/* SPDX-License-Identifier: GPL-2.0 */
/*
 * linux/include/linux/sunrpc/svc.h
 *
 * RPC server declarations.
 *
 * Copyright (C) 1995, 1996 Olaf Kirch <okir@monad.swb.de>
 */


#ifndef SUNRPC_SVC_H
#define SUNRPC_SVC_H

#include <linux/in.h>
#include <linux/in6.h>
#include <linux/sunrpc/types.h>
#include <linux/sunrpc/xdr.h>
#include <linux/sunrpc/auth.h>
#include <linux/sunrpc/svcauth.h>
#include <linux/wait.h>
#include <linux/mm.h>

/* statistics for svc_pool structures */
struct svc_pool_stats {
	atomic_long_t	packets;
	unsigned long	sockets_queued;
	atomic_long_t	threads_woken;
	atomic_long_t	threads_timedout;
};

/*
 *
 * RPC service thread pool.
 *
 * Pool of threads and temporary sockets.  Generally there is only
 * a single one of these per RPC service, but on NUMA machines those
 * services that can benefit from it (i.e. nfs but not lockd) will
 * have one pool per NUMA node.  This optimisation reduces cross-
 * node traffic on multi-node NUMA NFS servers.
 */
struct svc_pool {
	unsigned int		sp_id;	    	/* pool id; also node id on NUMA */
	spinlock_t		sp_lock;	/* protects all fields */
	struct list_head	sp_sockets;	/* pending sockets */
	unsigned int		sp_nrthreads;	/* # of threads in pool */
	struct list_head	sp_all_threads;	/* all server threads */
	struct svc_pool_stats	sp_stats;	/* statistics on pool operation */
#define	SP_TASK_PENDING		(0)		/* still work to do even if no
						 * xprt is queued. */
#define SP_CONGESTED		(1)
	unsigned long		sp_flags;
} ____cacheline_aligned_in_smp;

struct svc_serv;

struct svc_serv_ops {
	/* Callback to use when last thread exits. */
	void		(*svo_shutdown)(struct svc_serv *, struct net *);

	/* function for service threads to run */
	int		(*svo_function)(void *);

	/* queue up a transport for servicing */
	void		(*svo_enqueue_xprt)(struct svc_xprt *);

	/* set up thread (or whatever) execution context */
	int		(*svo_setup)(struct svc_serv *, struct svc_pool *, int);

	/* optional module to count when adding threads (pooled svcs only) */
	struct module	*svo_module;
};

/*
 * RPC service.
 *
 * An RPC service is a ``daemon,'' possibly multithreaded, which
 * receives and processes incoming RPC messages.
 * It has one or more transport sockets associated with it, and maintains
 * a list of idle threads waiting for input.
 *
 * We currently do not support more than one RPC program per daemon.
 */
struct svc_serv {
	struct svc_program *	sv_program;	/* RPC program */
	struct svc_stat *	sv_stats;	/* RPC statistics */
	spinlock_t		sv_lock;
	unsigned int		sv_nrthreads;	/* # of server threads */
	unsigned int		sv_maxconn;	/* max connections allowed or
						 * '0' causing max to be based
						 * on number of threads. */

	unsigned int		sv_max_payload;	/* datagram payload size */
	unsigned int		sv_max_mesg;	/* max_payload + 1 page for overheads */
	unsigned int		sv_xdrsize;	/* XDR buffer size */
	struct list_head	sv_permsocks;	/* all permanent sockets */
	struct list_head	sv_tempsocks;	/* all temporary sockets */
	int			sv_tmpcnt;	/* count of temporary sockets */
	struct timer_list	sv_temptimer;	/* timer for aging temporary sockets */

	char *			sv_name;	/* service name */

	unsigned int		sv_nrpools;	/* number of thread pools */
	struct svc_pool *	sv_pools;	/* array of thread pools */
	const struct svc_serv_ops *sv_ops;	/* server operations */
#if defined(CONFIG_SUNRPC_BACKCHANNEL)
	struct list_head	sv_cb_list;	/* queue for callback requests
						 * that arrive over the same
						 * connection */
	spinlock_t		sv_cb_lock;	/* protects the svc_cb_list */
	wait_queue_head_t	sv_cb_waitq;	/* sleep here if there are no
						 * entries in the svc_cb_list */
	bool			sv_bc_enabled;	/* service uses backchannel */
#endif /* CONFIG_SUNRPC_BACKCHANNEL */
};

/*
 * We use sv_nrthreads as a reference count.  svc_destroy() drops
 * this refcount, so we need to bump it up around operations that
 * change the number of threads.  Horrible, but there it is.
 * Should be called with the "service mutex" held.
 */
static inline void svc_get(struct svc_serv *serv)
{
	serv->sv_nrthreads++;
}

/*
 * Maximum payload size supported by a kernel RPC server.
 * This is use to determine the max number of pages nfsd is
 * willing to return in a single READ operation.
 *
 * These happen to all be powers of 2, which is not strictly
 * necessary but helps enforce the real limitation, which is
 * that they should be multiples of PAGE_SIZE.
 *
 * For UDP transports, a block plus NFS,RPC, and UDP headers
 * has to fit into the IP datagram limit of 64K.  The largest
 * feasible number for all known page sizes is probably 48K,
 * but we choose 32K here.  This is the same as the historical
 * Linux limit; someone who cares more about NFS/UDP performance
 * can test a larger number.
 *
 * For TCP transports we have more freedom.  A size of 1MB is
 * chosen to match the client limit.  Other OSes are known to
 * have larger limits, but those numbers are probably beyond
 * the point of diminishing returns.
 */
#define RPCSVC_MAXPAYLOAD	(1*1024*1024u)
#define RPCSVC_MAXPAYLOAD_TCP	RPCSVC_MAXPAYLOAD
#define RPCSVC_MAXPAYLOAD_UDP	(32*1024u)

extern u32 svc_max_payload(const struct svc_rqst *rqstp);

/*
 * RPC Requsts and replies are stored in one or more pages.
 * We maintain an array of pages for each server thread.
 * Requests are copied into these pages as they arrive.  Remaining
 * pages are available to write the reply into.
 *
 * Pages are sent using ->sendpage so each server thread needs to
 * allocate more to replace those used in sending.  To help keep track
 * of these pages we have a receive list where all pages initialy live,
 * and a send list where pages are moved to when there are to be part
 * of a reply.
 *
 * We use xdr_buf for holding responses as it fits well with NFS
 * read responses (that have a header, and some data pages, and possibly
 * a tail) and means we can share some client side routines.
 *
 * The xdr_buf.head kvec always points to the first page in the rq_*pages
 * list.  The xdr_buf.pages pointer points to the second page on that
 * list.  xdr_buf.tail points to the end of the first page.
 * This assumes that the non-page part of an rpc reply will fit
 * in a page - NFSd ensures this.  lockd also has no trouble.
 *
 * Each request/reply pair can have at most one "payload", plus two pages,
 * one for the request, and one for the reply.
 * We using ->sendfile to return read data, we might need one extra page
 * if the request is not page-aligned.  So add another '1'.
 */
#define RPCSVC_MAXPAGES		((RPCSVC_MAXPAYLOAD+PAGE_SIZE-1)/PAGE_SIZE \
				+ 2 + 1)

static inline u32 svc_getnl(struct kvec *iov)
{
	__be32 val, *vp;
	vp = iov->iov_base;
	val = *vp++;
	iov->iov_base = (void*)vp;
	iov->iov_len -= sizeof(__be32);
	return ntohl(val);
}

static inline void svc_putnl(struct kvec *iov, u32 val)
{
	__be32 *vp = iov->iov_base + iov->iov_len;
	*vp = htonl(val);
	iov->iov_len += sizeof(__be32);
}

static inline __be32 svc_getu32(struct kvec *iov)
{
	__be32 val, *vp;
	vp = iov->iov_base;
	val = *vp++;
	iov->iov_base = (void*)vp;
	iov->iov_len -= sizeof(__be32);
	return val;
}

static inline void svc_ungetu32(struct kvec *iov)
{
	__be32 *vp = (__be32 *)iov->iov_base;
	iov->iov_base = (void *)(vp - 1);
	iov->iov_len += sizeof(*vp);
}

static inline void svc_putu32(struct kvec *iov, __be32 val)
{
	__be32 *vp = iov->iov_base + iov->iov_len;
	*vp = val;
	iov->iov_len += sizeof(__be32);
}

/*
 * The context of a single thread, including the request currently being
 * processed.
 */
struct svc_rqst {
	struct list_head	rq_all;		/* all threads list */
	struct rcu_head		rq_rcu_head;	/* for RCU deferred kfree */
	struct svc_xprt *	rq_xprt;	/* transport ptr */

	struct sockaddr_storage	rq_addr;	/* peer address */
	size_t			rq_addrlen;
	struct sockaddr_storage	rq_daddr;	/* dest addr of request
						 *  - reply from here */
	size_t			rq_daddrlen;

	struct svc_serv *	rq_server;	/* RPC service definition */
	struct svc_pool *	rq_pool;	/* thread pool */
	const struct svc_procedure *rq_procinfo;/* procedure info */
	struct auth_ops *	rq_authop;	/* authentication flavour */
	struct svc_cred		rq_cred;	/* auth info */
	void *			rq_xprt_ctxt;	/* transport specific context ptr */
	struct svc_deferred_req*rq_deferred;	/* deferred request we are replaying */

	size_t			rq_xprt_hlen;	/* xprt header len */
	struct xdr_buf		rq_arg;
	struct xdr_stream	rq_arg_stream;
	struct xdr_stream	rq_res_stream;
	struct page		*rq_scratch_page;
	struct xdr_buf		rq_res;
	struct page		*rq_pages[RPCSVC_MAXPAGES + 1];
	struct page *		*rq_respages;	/* points into rq_pages */
	struct page *		*rq_next_page; /* next reply page to use */
	struct page *		*rq_page_end;  /* one past the last page */

	struct kvec		rq_vec[RPCSVC_MAXPAGES]; /* generally useful.. */
	struct bio_vec		rq_bvec[RPCSVC_MAXPAGES];

	__be32			rq_xid;		/* transmission id */
	u32			rq_prog;	/* program number */
	u32			rq_vers;	/* program version */
	u32			rq_proc;	/* procedure number */
	u32			rq_prot;	/* IP protocol */
	int			rq_cachetype;	/* catering to nfsd */
#define	RQ_SECURE	(0)			/* secure port */
#define	RQ_LOCAL	(1)			/* local request */
#define	RQ_USEDEFERRAL	(2)			/* use deferral */
#define	RQ_DROPME	(3)			/* drop current reply */
#define	RQ_SPLICE_OK	(4)			/* turned off in gss privacy
						 * to prevent encrypting page
						 * cache pages */
#define	RQ_VICTIM	(5)			/* about to be shut down */
#define	RQ_BUSY		(6)			/* request is busy */
#define	RQ_DATA		(7)			/* request has data */
#define RQ_AUTHERR	(8)			/* Request status is auth error */
	unsigned long		rq_flags;	/* flags field */
	ktime_t			rq_qtime;	/* enqueue time */

	void *			rq_argp;	/* decoded arguments */
	void *			rq_resp;	/* xdr'd results */
	void *			rq_auth_data;	/* flavor-specific data */
	int			rq_auth_slack;	/* extra space xdr code
						 * should leave in head
						 * for krb5i, krb5p.
						 */
	int			rq_reserved;	/* space on socket outq
						 * reserved for this request
						 */
	ktime_t			rq_stime;	/* start time */

	struct cache_req	rq_chandle;	/* handle passed to caches for 
						 * request delaying 
						 */
	/* Catering to nfsd */
	struct auth_domain *	rq_client;	/* RPC peer info */
	struct auth_domain *	rq_gssclient;	/* "gss/"-style peer info */
	struct svc_cacherep *	rq_cacherep;	/* cache info */
	struct task_struct	*rq_task;	/* service thread */
	spinlock_t		rq_lock;	/* per-request lock */
	struct net		*rq_bc_net;	/* pointer to backchannel's
						 * net namespace
						 */
	void **			rq_lease_breaker; /* The v4 client breaking a lease */
};

#define SVC_NET(rqst) (rqst->rq_xprt ? rqst->rq_xprt->xpt_net : rqst->rq_bc_net)

/*
 * Rigorous type checking on sockaddr type conversions
 */
static inline struct sockaddr_in *svc_addr_in(const struct svc_rqst *rqst)
{
	return (struct sockaddr_in *) &rqst->rq_addr;
}

static inline struct sockaddr_in6 *svc_addr_in6(const struct svc_rqst *rqst)
{
	return (struct sockaddr_in6 *) &rqst->rq_addr;
}

static inline struct sockaddr *svc_addr(const struct svc_rqst *rqst)
{
	return (struct sockaddr *) &rqst->rq_addr;
}

static inline struct sockaddr_in *svc_daddr_in(const struct svc_rqst *rqst)
{
	return (struct sockaddr_in *) &rqst->rq_daddr;
}

static inline struct sockaddr_in6 *svc_daddr_in6(const struct svc_rqst *rqst)
{
	return (struct sockaddr_in6 *) &rqst->rq_daddr;
}

static inline struct sockaddr *svc_daddr(const struct svc_rqst *rqst)
{
	return (struct sockaddr *) &rqst->rq_daddr;
}

/*
 * Check buffer bounds after decoding arguments
 */
static inline int
xdr_argsize_check(struct svc_rqst *rqstp, __be32 *p)
{
	char *cp = (char *)p;
	struct kvec *vec = &rqstp->rq_arg.head[0];
	return cp >= (char*)vec->iov_base
		&& cp <= (char*)vec->iov_base + vec->iov_len;
}

static inline int
xdr_ressize_check(struct svc_rqst *rqstp, __be32 *p)
{
	struct kvec *vec = &rqstp->rq_res.head[0];
	char *cp = (char*)p;

	vec->iov_len = cp - (char*)vec->iov_base;

	return vec->iov_len <= PAGE_SIZE;
}

static inline void svc_free_res_pages(struct svc_rqst *rqstp)
{
	while (rqstp->rq_next_page != rqstp->rq_respages) {
		struct page **pp = --rqstp->rq_next_page;
		if (*pp) {
			put_page(*pp);
			*pp = NULL;
		}
	}
}

struct svc_deferred_req {
	u32			prot;	/* protocol (UDP or TCP) */
	struct svc_xprt		*xprt;
	struct sockaddr_storage	addr;	/* where reply must go */
	size_t			addrlen;
	struct sockaddr_storage	daddr;	/* where reply must come from */
	size_t			daddrlen;
	struct cache_deferred_req handle;
	size_t			xprt_hlen;
	int			argslen;
	__be32			args[];
};

struct svc_process_info {
	union {
		int  (*dispatch)(struct svc_rqst *, __be32 *);
		struct {
			unsigned int lovers;
			unsigned int hivers;
		} mismatch;
	};
};

/*
 * List of RPC programs on the same transport endpoint
 */
struct svc_program {
	struct svc_program *	pg_next;	/* other programs (same xprt) */
	u32			pg_prog;	/* program number */
	unsigned int		pg_lovers;	/* lowest version */
	unsigned int		pg_hivers;	/* highest version */
	unsigned int		pg_nvers;	/* number of versions */
	const struct svc_version **pg_vers;	/* version array */
	char *			pg_name;	/* service name */
	char *			pg_class;	/* class name: services sharing authentication */
	struct svc_stat *	pg_stats;	/* rpc statistics */
	int			(*pg_authenticate)(struct svc_rqst *);
	__be32			(*pg_init_request)(struct svc_rqst *,
						   const struct svc_program *,
						   struct svc_process_info *);
	int			(*pg_rpcbind_set)(struct net *net,
						  const struct svc_program *,
						  u32 version, int family,
						  unsigned short proto,
						  unsigned short port);
};

/*
 * RPC program version
 */
struct svc_version {
	u32			vs_vers;	/* version number */
	u32			vs_nproc;	/* number of procedures */
	const struct svc_procedure *vs_proc;	/* per-procedure info */
	unsigned int		*vs_count;	/* call counts */
	u32			vs_xdrsize;	/* xdrsize needed for this version */

	/* Don't register with rpcbind */
	bool			vs_hidden;

	/* Don't care if the rpcbind registration fails */
	bool			vs_rpcb_optnl;

	/* Need xprt with congestion control */
	bool			vs_need_cong_ctrl;

	/* Override dispatch function (e.g. when caching replies).
	 * A return value of 0 means drop the request. 
	 * vs_dispatch == NULL means use default dispatcher.
	 */
	int			(*vs_dispatch)(struct svc_rqst *, __be32 *);
};

/*
 * RPC procedure info
 */
struct svc_procedure {
	/* process the request: */
	__be32			(*pc_func)(struct svc_rqst *);
	/* XDR decode args: */
	int			(*pc_decode)(struct svc_rqst *, __be32 *data);
	/* XDR encode result: */
	int			(*pc_encode)(struct svc_rqst *, __be32 *data);
	/* XDR free result: */
	void			(*pc_release)(struct svc_rqst *);
	unsigned int		pc_argsize;	/* argument struct size */
	unsigned int		pc_ressize;	/* result struct size */
	unsigned int		pc_cachetype;	/* cache info (NFS) */
	unsigned int		pc_xdrressize;	/* maximum size of XDR reply */
	const char *		pc_name;	/* for display */
};

/*
 * Mode for mapping cpus to pools.
 */
enum {
	SVC_POOL_AUTO = -1,	/* choose one of the others */
	SVC_POOL_GLOBAL,	/* no mapping, just a single global pool
				 * (legacy & UP mode) */
	SVC_POOL_PERCPU,	/* one pool per cpu */
	SVC_POOL_PERNODE	/* one pool per numa node */
};

struct svc_pool_map {
	int count;			/* How many svc_servs use us */
	int mode;			/* Note: int not enum to avoid
					 * warnings about "enumeration value
					 * not handled in switch" */
	unsigned int npools;
	unsigned int *pool_to;		/* maps pool id to cpu or node */
	unsigned int *to_pool;		/* maps cpu or node to pool id */
};

extern struct svc_pool_map svc_pool_map;

/*
 * Function prototypes.
 */
int svc_rpcb_setup(struct svc_serv *serv, struct net *net);
void svc_rpcb_cleanup(struct svc_serv *serv, struct net *net);
int svc_bind(struct svc_serv *serv, struct net *net);
struct svc_serv *svc_create(struct svc_program *, unsigned int,
			    const struct svc_serv_ops *);
struct svc_rqst *svc_rqst_alloc(struct svc_serv *serv,
					struct svc_pool *pool, int node);
struct svc_rqst *svc_prepare_thread(struct svc_serv *serv,
					struct svc_pool *pool, int node);
void		   svc_rqst_free(struct svc_rqst *);
void		   svc_exit_thread(struct svc_rqst *);
unsigned int	   svc_pool_map_get(void);
void		   svc_pool_map_put(void);
struct svc_serv *  svc_create_pooled(struct svc_program *, unsigned int,
			const struct svc_serv_ops *);
int		   svc_set_num_threads(struct svc_serv *, struct svc_pool *, int);
int		   svc_set_num_threads_sync(struct svc_serv *, struct svc_pool *, int);
int		   svc_pool_stats_open(struct svc_serv *serv, struct file *file);
void		   svc_destroy(struct svc_serv *);
void		   svc_shutdown_net(struct svc_serv *, struct net *);
int		   svc_process(struct svc_rqst *);
int		   bc_svc_process(struct svc_serv *, struct rpc_rqst *,
			struct svc_rqst *);
int		   svc_register(const struct svc_serv *, struct net *, const int,
				const unsigned short, const unsigned short);

void		   svc_wake_up(struct svc_serv *);
void		   svc_reserve(struct svc_rqst *rqstp, int space);
struct svc_pool *  svc_pool_for_cpu(struct svc_serv *serv, int cpu);
char *		   svc_print_addr(struct svc_rqst *, char *, size_t);
int		   svc_encode_result_payload(struct svc_rqst *rqstp,
					     unsigned int offset,
					     unsigned int length);
unsigned int	   svc_fill_write_vector(struct svc_rqst *rqstp,
					 struct page **pages,
					 struct kvec *first, size_t total);
char		  *svc_fill_symlink_pathname(struct svc_rqst *rqstp,
					     struct kvec *first, void *p,
					     size_t total);
__be32		   svc_return_autherr(struct svc_rqst *rqstp, __be32 auth_err);
__be32		   svc_generic_init_request(struct svc_rqst *rqstp,
					    const struct svc_program *progp,
					    struct svc_process_info *procinfo);
int		   svc_generic_rpcbind_set(struct net *net,
					   const struct svc_program *progp,
					   u32 version, int family,
					   unsigned short proto,
					   unsigned short port);
int		   svc_rpcbind_set_version(struct net *net,
					   const struct svc_program *progp,
					   u32 version, int family,
					   unsigned short proto,
					   unsigned short port);

#define	RPC_MAX_ADDRBUFLEN	(63U)

/*
 * When we want to reduce the size of the reserved space in the response
 * buffer, we need to take into account the size of any checksum data that
 * may be at the end of the packet. This is difficult to determine exactly
 * for all cases without actually generating the checksum, so we just use a
 * static value.
 */
static inline void svc_reserve_auth(struct svc_rqst *rqstp, int space)
{
	svc_reserve(rqstp, space + rqstp->rq_auth_slack);
}

/**
 * svcxdr_init_decode - Prepare an xdr_stream for svc Call decoding
 * @rqstp: controlling server RPC transaction context
 *
 */
static inline void svcxdr_init_decode(struct svc_rqst *rqstp)
{
	struct xdr_stream *xdr = &rqstp->rq_arg_stream;
	struct kvec *argv = rqstp->rq_arg.head;

	xdr_init_decode(xdr, &rqstp->rq_arg, argv->iov_base, NULL);
	xdr_set_scratch_page(xdr, rqstp->rq_scratch_page);
}

/**
 * svcxdr_init_encode - Prepare an xdr_stream for svc Reply encoding
 * @rqstp: controlling server RPC transaction context
 *
 */
static inline void svcxdr_init_encode(struct svc_rqst *rqstp)
{
	struct xdr_stream *xdr = &rqstp->rq_res_stream;
	struct xdr_buf *buf = &rqstp->rq_res;
	struct kvec *resv = buf->head;

	xdr_reset_scratch_buffer(xdr);

	xdr->buf = buf;
	xdr->iov = resv;
	xdr->p   = resv->iov_base + resv->iov_len;
	xdr->end = resv->iov_base + PAGE_SIZE - rqstp->rq_auth_slack;
	buf->len = resv->iov_len;
	xdr->page_ptr = buf->pages - 1;
	buf->buflen = PAGE_SIZE * (1 + rqstp->rq_page_end - buf->pages);
	buf->buflen -= rqstp->rq_auth_slack;
	xdr->rqst = NULL;
}

#endif /* SUNRPC_SVC_H */
