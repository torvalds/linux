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

/*
 * This is the RPC server thread function prototype
 */
typedef int		(*svc_thread_fn)(void *);

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
	struct list_head	sp_threads;	/* idle server threads */
	struct list_head	sp_sockets;	/* pending sockets */
	unsigned int		sp_nrthreads;	/* # of threads in pool */
	struct list_head	sp_all_threads;	/* all server threads */
} ____cacheline_aligned_in_smp;

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
	unsigned int		sv_max_payload;	/* datagram payload size */
	unsigned int		sv_max_mesg;	/* max_payload + 1 page for overheads */
	unsigned int		sv_xdrsize;	/* XDR buffer size */

	struct list_head	sv_permsocks;	/* all permanent sockets */
	struct list_head	sv_tempsocks;	/* all temporary sockets */
	int			sv_tmpcnt;	/* count of temporary sockets */
	struct timer_list	sv_temptimer;	/* timer for aging temporary sockets */
	sa_family_t		sv_family;	/* listener's address family */

	char *			sv_name;	/* service name */

	unsigned int		sv_nrpools;	/* number of thread pools */
	struct svc_pool *	sv_pools;	/* array of thread pools */

	void			(*sv_shutdown)(struct svc_serv *serv);
						/* Callback to use when last thread
						 * exits.
						 */

	struct module *		sv_module;	/* optional module to count when
						 * adding threads */
	svc_thread_fn		sv_function;	/* main function for threads */
};

/*
 * We use sv_nrthreads as a reference count.  svc_destroy() drops
 * this refcount, so we need to bump it up around operations that
 * change the number of threads.  Horrible, but there it is.
 * Should be called with the BKL held.
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
 * that they should be multiples of PAGE_CACHE_SIZE.
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

union svc_addr_u {
    struct in_addr	addr;
    struct in6_addr	addr6;
};

/*
 * The context of a single thread, including the request currently being
 * processed.
 */
struct svc_rqst {
	struct list_head	rq_list;	/* idle list */
	struct list_head	rq_all;		/* all threads list */
	struct svc_xprt *	rq_xprt;	/* transport ptr */
	struct sockaddr_storage	rq_addr;	/* peer address */
	size_t			rq_addrlen;

	struct svc_serv *	rq_server;	/* RPC service definition */
	struct svc_pool *	rq_pool;	/* thread pool */
	struct svc_procedure *	rq_procinfo;	/* procedure info */
	struct auth_ops *	rq_authop;	/* authentication flavour */
	u32			rq_flavor;	/* pseudoflavor */
	struct svc_cred		rq_cred;	/* auth info */
	void *			rq_xprt_ctxt;	/* transport specific context ptr */
	struct svc_deferred_req*rq_deferred;	/* deferred request we are replaying */

	size_t			rq_xprt_hlen;	/* xprt header len */
	struct xdr_buf		rq_arg;
	struct xdr_buf		rq_res;
	struct page *		rq_pages[RPCSVC_MAXPAGES];
	struct page *		*rq_respages;	/* points into rq_pages */
	int			rq_resused;	/* number of pages used for result */

	struct kvec		rq_vec[RPCSVC_MAXPAGES]; /* generally useful.. */

	__be32			rq_xid;		/* transmission id */
	u32			rq_prog;	/* program number */
	u32			rq_vers;	/* program version */
	u32			rq_proc;	/* procedure number */
	u32			rq_prot;	/* IP protocol */
	unsigned short
				rq_secure  : 1;	/* secure port */

	union svc_addr_u	rq_daddr;	/* dest addr of request
						 *  - reply from here */

	void *			rq_argp;	/* decoded arguments */
	void *			rq_resp;	/* xdr'd results */
	void *			rq_auth_data;	/* flavor-specific data */

	int			rq_reserved;	/* space on socket outq
						 * reserved for this request
						 */

	struct cache_req	rq_chandle;	/* handle passed to caches for 
						 * request delaying 
						 */
	/* Catering to nfsd */
	struct auth_domain *	rq_client;	/* RPC peer info */
	struct auth_domain *	rq_gssclient;	/* "gss/"-style peer info */
	struct svc_cacherep *	rq_cacherep;	/* cache info */
	struct knfsd_fh *	rq_reffh;	/* Referrence filehandle, used to
						 * determine what device number
						 * to report (real or virtual)
						 */
	int			rq_splice_ok;   /* turned off in gss privacy
						 * to prevent encrypting page
						 * cache pages */
	wait_queue_head_t	rq_wait;	/* synchronization */
	struct task_struct	*rq_task;	/* service thread */
};

/*
 * Rigorous type checking on sockaddr type conversions
 */
static inline struct sockaddr_in *svc_addr_in(struct svc_rqst *rqst)
{
	return (struct sockaddr_in *) &rqst->rq_addr;
}

static inline struct sockaddr_in6 *svc_addr_in6(struct svc_rqst *rqst)
{
	return (struct sockaddr_in6 *) &rqst->rq_addr;
}

static inline struct sockaddr *svc_addr(struct svc_rqst *rqst)
{
	return (struct sockaddr *) &rqst->rq_addr;
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
	while (rqstp->rq_resused) {
		struct page **pp = (rqstp->rq_respages +
				    --rqstp->rq_resused);
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
	union svc_addr_u	daddr;	/* where reply must come from */
	struct cache_deferred_req handle;
	size_t			xprt_hlen;
	int			argslen;
	__be32			args[0];
};

/*
 * List of RPC programs on the same transport endpoint
 */
struct svc_program {
	struct svc_program *	pg_next;	/* other programs (same xprt) */
	u32			pg_prog;	/* program number */
	unsigned int		pg_lovers;	/* lowest version */
	unsigned int		pg_hivers;	/* lowest version */
	unsigned int		pg_nvers;	/* number of versions */
	struct svc_version **	pg_vers;	/* version array */
	char *			pg_name;	/* service name */
	char *			pg_class;	/* class name: services sharing authentication */
	struct svc_stat *	pg_stats;	/* rpc statistics */
	int			(*pg_authenticate)(struct svc_rqst *);
};

/*
 * RPC program version
 */
struct svc_version {
	u32			vs_vers;	/* version number */
	u32			vs_nproc;	/* number of procedures */
	struct svc_procedure *	vs_proc;	/* per-procedure info */
	u32			vs_xdrsize;	/* xdrsize needed for this version */

	unsigned int		vs_hidden : 1;	/* Don't register with portmapper.
						 * Only used for nfsacl so far. */

	/* Override dispatch function (e.g. when caching replies).
	 * A return value of 0 means drop the request. 
	 * vs_dispatch == NULL means use default dispatcher.
	 */
	int			(*vs_dispatch)(struct svc_rqst *, __be32 *);
};

/*
 * RPC procedure info
 */
typedef __be32	(*svc_procfunc)(struct svc_rqst *, void *argp, void *resp);
struct svc_procedure {
	svc_procfunc		pc_func;	/* process the request */
	kxdrproc_t		pc_decode;	/* XDR decode args */
	kxdrproc_t		pc_encode;	/* XDR encode result */
	kxdrproc_t		pc_release;	/* XDR free result */
	unsigned int		pc_argsize;	/* argument struct size */
	unsigned int		pc_ressize;	/* result struct size */
	unsigned int		pc_count;	/* call count */
	unsigned int		pc_cachetype;	/* cache info (NFS) */
	unsigned int		pc_xdrressize;	/* maximum size of XDR reply */
};

/*
 * Function prototypes.
 */
struct svc_serv *svc_create(struct svc_program *, unsigned int, sa_family_t,
			    void (*shutdown)(struct svc_serv *));
struct svc_rqst *svc_prepare_thread(struct svc_serv *serv,
					struct svc_pool *pool);
void		   svc_exit_thread(struct svc_rqst *);
struct svc_serv *  svc_create_pooled(struct svc_program *, unsigned int,
			sa_family_t, void (*shutdown)(struct svc_serv *),
			svc_thread_fn, struct module *);
int		   svc_set_num_threads(struct svc_serv *, struct svc_pool *, int);
void		   svc_destroy(struct svc_serv *);
int		   svc_process(struct svc_rqst *);
int		   svc_register(struct svc_serv *, int, unsigned short);
void		   svc_wake_up(struct svc_serv *);
void		   svc_reserve(struct svc_rqst *rqstp, int space);
struct svc_pool *  svc_pool_for_cpu(struct svc_serv *serv, int cpu);
char *		   svc_print_addr(struct svc_rqst *, char *, size_t);

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
	int added_space = 0;

	if (rqstp->rq_authop->flavour)
		added_space = RPC_MAX_AUTH_SIZE;
	svc_reserve(rqstp, space + added_space);
}

#endif /* SUNRPC_SVC_H */
