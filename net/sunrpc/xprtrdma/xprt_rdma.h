/*
 * Copyright (c) 2003-2007 Network Appliance, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the BSD-type
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *      Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *
 *      Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 *      Neither the name of the Network Appliance, Inc. nor the names of
 *      its contributors may be used to endorse or promote products
 *      derived from this software without specific prior written
 *      permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _LINUX_SUNRPC_XPRT_RDMA_H
#define _LINUX_SUNRPC_XPRT_RDMA_H

#include <linux/wait.h> 		/* wait_queue_head_t, etc */
#include <linux/spinlock.h> 		/* spinlock_t, etc */
#include <linux/atomic.h>			/* atomic_t, etc */
#include <linux/workqueue.h>		/* struct work_struct */

#include <rdma/rdma_cm.h>		/* RDMA connection api */
#include <rdma/ib_verbs.h>		/* RDMA verbs api */

#include <linux/sunrpc/clnt.h> 		/* rpc_xprt */
#include <linux/sunrpc/rpc_rdma.h> 	/* RPC/RDMA protocol */
#include <linux/sunrpc/xprtrdma.h> 	/* xprt parameters */
#include <linux/sunrpc/svc.h>		/* RPCSVC_MAXPAYLOAD */

#define RDMA_RESOLVE_TIMEOUT	(5000)	/* 5 seconds */
#define RDMA_CONNECT_RETRY_MAX	(2)	/* retries if no listener backlog */

/*
 * Interface Adapter -- one per transport instance
 */
struct rpcrdma_ia {
	rwlock_t		ri_qplock;
	struct rdma_cm_id 	*ri_id;
	struct ib_pd		*ri_pd;
	struct ib_mr		*ri_bind_mem;
	u32			ri_dma_lkey;
	int			ri_have_dma_lkey;
	struct completion	ri_done;
	int			ri_async_rc;
	enum rpcrdma_memreg	ri_memreg_strategy;
	unsigned int		ri_max_frmr_depth;
};

/*
 * RDMA Endpoint -- one per transport instance
 */

#define RPCRDMA_WC_BUDGET	(128)
#define RPCRDMA_POLLSIZE	(16)

struct rpcrdma_ep {
	atomic_t		rep_cqcount;
	int			rep_cqinit;
	int			rep_connected;
	struct rpcrdma_ia	*rep_ia;
	struct ib_qp_init_attr	rep_attr;
	wait_queue_head_t 	rep_connect_wait;
	struct ib_sge		rep_pad;	/* holds zeroed pad */
	struct ib_mr		*rep_pad_mr;	/* holds zeroed pad */
	void			(*rep_func)(struct rpcrdma_ep *);
	struct rpc_xprt		*rep_xprt;	/* for rep_func */
	struct rdma_conn_param	rep_remote_cma;
	struct sockaddr_storage	rep_remote_addr;
	struct delayed_work	rep_connect_worker;
	struct ib_wc		rep_send_wcs[RPCRDMA_POLLSIZE];
	struct ib_wc		rep_recv_wcs[RPCRDMA_POLLSIZE];
};

#define INIT_CQCOUNT(ep) atomic_set(&(ep)->rep_cqcount, (ep)->rep_cqinit)
#define DECR_CQCOUNT(ep) atomic_sub_return(1, &(ep)->rep_cqcount)

enum rpcrdma_chunktype {
	rpcrdma_noch = 0,
	rpcrdma_readch,
	rpcrdma_areadch,
	rpcrdma_writech,
	rpcrdma_replych
};

/*
 * struct rpcrdma_rep -- this structure encapsulates state required to recv
 * and complete a reply, asychronously. It needs several pieces of
 * state:
 *   o recv buffer (posted to provider)
 *   o ib_sge (also donated to provider)
 *   o status of reply (length, success or not)
 *   o bookkeeping state to get run by tasklet (list, etc)
 *
 * These are allocated during initialization, per-transport instance;
 * however, the tasklet execution list itself is global, as it should
 * always be pretty short.
 *
 * N of these are associated with a transport instance, and stored in
 * struct rpcrdma_buffer. N is the max number of outstanding requests.
 */

/* temporary static scatter/gather max */
#define RPCRDMA_MAX_DATA_SEGS	(64)	/* max scatter/gather */
#define RPCRDMA_MAX_SEGS 	(RPCRDMA_MAX_DATA_SEGS + 2) /* head+tail = 2 */
#define MAX_RPCRDMAHDR	(\
	/* max supported RPC/RDMA header */ \
	sizeof(struct rpcrdma_msg) + (2 * sizeof(u32)) + \
	(sizeof(struct rpcrdma_read_chunk) * RPCRDMA_MAX_SEGS) + sizeof(u32))

struct rpcrdma_buffer;

struct rpcrdma_rep {
	unsigned int	rr_len;		/* actual received reply length */
	struct rpcrdma_buffer *rr_buffer; /* home base for this structure */
	struct rpc_xprt	*rr_xprt;	/* needed for request/reply matching */
	void (*rr_func)(struct rpcrdma_rep *);/* called by tasklet in softint */
	struct list_head rr_list;	/* tasklet list */
	struct ib_sge	rr_iov;		/* for posting */
	struct ib_mr	*rr_handle;	/* handle for mem in rr_iov */
	char	rr_base[MAX_RPCRDMAHDR]; /* minimal inline receive buffer */
};

/*
 * struct rpcrdma_mw - external memory region metadata
 *
 * An external memory region is any buffer or page that is registered
 * on the fly (ie, not pre-registered).
 *
 * Each rpcrdma_buffer has a list of free MWs anchored in rb_mws. During
 * call_allocate, rpcrdma_buffer_get() assigns one to each segment in
 * an rpcrdma_req. Then rpcrdma_register_external() grabs these to keep
 * track of registration metadata while each RPC is pending.
 * rpcrdma_deregister_external() uses this metadata to unmap and
 * release these resources when an RPC is complete.
 */
enum rpcrdma_frmr_state {
	FRMR_IS_INVALID,	/* ready to be used */
	FRMR_IS_VALID,		/* in use */
	FRMR_IS_STALE,		/* failed completion */
};

struct rpcrdma_frmr {
	struct ib_fast_reg_page_list	*fr_pgl;
	struct ib_mr			*fr_mr;
	enum rpcrdma_frmr_state		fr_state;
};

struct rpcrdma_mw {
	union {
		struct ib_fmr		*fmr;
		struct rpcrdma_frmr	frmr;
	} r;
	struct list_head	mw_list;
	struct list_head	mw_all;
};

/*
 * struct rpcrdma_req -- structure central to the request/reply sequence.
 *
 * N of these are associated with a transport instance, and stored in
 * struct rpcrdma_buffer. N is the max number of outstanding requests.
 *
 * It includes pre-registered buffer memory for send AND recv.
 * The recv buffer, however, is not owned by this structure, and
 * is "donated" to the hardware when a recv is posted. When a
 * reply is handled, the recv buffer used is given back to the
 * struct rpcrdma_req associated with the request.
 *
 * In addition to the basic memory, this structure includes an array
 * of iovs for send operations. The reason is that the iovs passed to
 * ib_post_{send,recv} must not be modified until the work request
 * completes.
 *
 * NOTES:
 *   o RPCRDMA_MAX_SEGS is the max number of addressible chunk elements we
 *     marshal. The number needed varies depending on the iov lists that
 *     are passed to us, the memory registration mode we are in, and if
 *     physical addressing is used, the layout.
 */

struct rpcrdma_mr_seg {		/* chunk descriptors */
	union {				/* chunk memory handles */
		struct ib_mr	*rl_mr;		/* if registered directly */
		struct rpcrdma_mw *rl_mw;	/* if registered from region */
	} mr_chunk;
	u64		mr_base;	/* registration result */
	u32		mr_rkey;	/* registration result */
	u32		mr_len;		/* length of chunk or segment */
	int		mr_nsegs;	/* number of segments in chunk or 0 */
	enum dma_data_direction	mr_dir;	/* segment mapping direction */
	dma_addr_t	mr_dma;		/* segment mapping address */
	size_t		mr_dmalen;	/* segment mapping length */
	struct page	*mr_page;	/* owning page, if any */
	char		*mr_offset;	/* kva if no page, else offset */
};

struct rpcrdma_req {
	size_t 		rl_size;	/* actual length of buffer */
	unsigned int	rl_niovs;	/* 0, 2 or 4 */
	unsigned int	rl_nchunks;	/* non-zero if chunks */
	unsigned int	rl_connect_cookie;	/* retry detection */
	enum rpcrdma_chunktype	rl_rtype, rl_wtype;
	struct rpcrdma_buffer *rl_buffer; /* home base for this structure */
	struct rpcrdma_rep	*rl_reply;/* holder for reply buffer */
	struct rpcrdma_mr_seg rl_segments[RPCRDMA_MAX_SEGS];/* chunk segments */
	struct ib_sge	rl_send_iov[4];	/* for active requests */
	struct ib_sge	rl_iov;		/* for posting */
	struct ib_mr	*rl_handle;	/* handle for mem in rl_iov */
	char		rl_base[MAX_RPCRDMAHDR]; /* start of actual buffer */
	__u32 		rl_xdr_buf[0];	/* start of returned rpc rq_buffer */
};
#define rpcr_to_rdmar(r) \
	container_of((r)->rq_buffer, struct rpcrdma_req, rl_xdr_buf[0])

/*
 * struct rpcrdma_buffer -- holds list/queue of pre-registered memory for
 * inline requests/replies, and client/server credits.
 *
 * One of these is associated with a transport instance
 */
struct rpcrdma_buffer {
	spinlock_t	rb_lock;	/* protects indexes */
	atomic_t	rb_credits;	/* most recent server credits */
	int		rb_max_requests;/* client max requests */
	struct list_head rb_mws;	/* optional memory windows/fmrs/frmrs */
	struct list_head rb_all;
	int		rb_send_index;
	struct rpcrdma_req	**rb_send_bufs;
	int		rb_recv_index;
	struct rpcrdma_rep	**rb_recv_bufs;
	char		*rb_pool;
};
#define rdmab_to_ia(b) (&container_of((b), struct rpcrdma_xprt, rx_buf)->rx_ia)

/*
 * Internal structure for transport instance creation. This
 * exists primarily for modularity.
 *
 * This data should be set with mount options
 */
struct rpcrdma_create_data_internal {
	struct sockaddr_storage	addr;	/* RDMA server address */
	unsigned int	max_requests;	/* max requests (slots) in flight */
	unsigned int	rsize;		/* mount rsize - max read hdr+data */
	unsigned int	wsize;		/* mount wsize - max write hdr+data */
	unsigned int	inline_rsize;	/* max non-rdma read data payload */
	unsigned int	inline_wsize;	/* max non-rdma write data payload */
	unsigned int	padding;	/* non-rdma write header padding */
};

#define RPCRDMA_INLINE_READ_THRESHOLD(rq) \
	(rpcx_to_rdmad(rq->rq_xprt).inline_rsize)

#define RPCRDMA_INLINE_WRITE_THRESHOLD(rq)\
	(rpcx_to_rdmad(rq->rq_xprt).inline_wsize)

#define RPCRDMA_INLINE_PAD_VALUE(rq)\
	rpcx_to_rdmad(rq->rq_xprt).padding

/*
 * Statistics for RPCRDMA
 */
struct rpcrdma_stats {
	unsigned long		read_chunk_count;
	unsigned long		write_chunk_count;
	unsigned long		reply_chunk_count;

	unsigned long long	total_rdma_request;
	unsigned long long	total_rdma_reply;

	unsigned long long	pullup_copy_count;
	unsigned long long	fixup_copy_count;
	unsigned long		hardway_register_count;
	unsigned long		failed_marshal_count;
	unsigned long		bad_reply_count;
};

/*
 * RPCRDMA transport -- encapsulates the structures above for
 * integration with RPC.
 *
 * The contained structures are embedded, not pointers,
 * for convenience. This structure need not be visible externally.
 *
 * It is allocated and initialized during mount, and released
 * during unmount.
 */
struct rpcrdma_xprt {
	struct rpc_xprt		xprt;
	struct rpcrdma_ia	rx_ia;
	struct rpcrdma_ep	rx_ep;
	struct rpcrdma_buffer	rx_buf;
	struct rpcrdma_create_data_internal rx_data;
	struct delayed_work	rdma_connect;
	struct rpcrdma_stats	rx_stats;
};

#define rpcx_to_rdmax(x) container_of(x, struct rpcrdma_xprt, xprt)
#define rpcx_to_rdmad(x) (rpcx_to_rdmax(x)->rx_data)

/* Setting this to 0 ensures interoperability with early servers.
 * Setting this to 1 enhances certain unaligned read/write performance.
 * Default is 0, see sysctl entry and rpc_rdma.c rpcrdma_convert_iovs() */
extern int xprt_rdma_pad_optimize;

/*
 * Interface Adapter calls - xprtrdma/verbs.c
 */
int rpcrdma_ia_open(struct rpcrdma_xprt *, struct sockaddr *, int);
void rpcrdma_ia_close(struct rpcrdma_ia *);

/*
 * Endpoint calls - xprtrdma/verbs.c
 */
int rpcrdma_ep_create(struct rpcrdma_ep *, struct rpcrdma_ia *,
				struct rpcrdma_create_data_internal *);
void rpcrdma_ep_destroy(struct rpcrdma_ep *, struct rpcrdma_ia *);
int rpcrdma_ep_connect(struct rpcrdma_ep *, struct rpcrdma_ia *);
void rpcrdma_ep_disconnect(struct rpcrdma_ep *, struct rpcrdma_ia *);

int rpcrdma_ep_post(struct rpcrdma_ia *, struct rpcrdma_ep *,
				struct rpcrdma_req *);
int rpcrdma_ep_post_recv(struct rpcrdma_ia *, struct rpcrdma_ep *,
				struct rpcrdma_rep *);

/*
 * Buffer calls - xprtrdma/verbs.c
 */
int rpcrdma_buffer_create(struct rpcrdma_buffer *, struct rpcrdma_ep *,
				struct rpcrdma_ia *,
				struct rpcrdma_create_data_internal *);
void rpcrdma_buffer_destroy(struct rpcrdma_buffer *);

struct rpcrdma_req *rpcrdma_buffer_get(struct rpcrdma_buffer *);
void rpcrdma_buffer_put(struct rpcrdma_req *);
void rpcrdma_recv_buffer_get(struct rpcrdma_req *);
void rpcrdma_recv_buffer_put(struct rpcrdma_rep *);

int rpcrdma_register_internal(struct rpcrdma_ia *, void *, int,
				struct ib_mr **, struct ib_sge *);
int rpcrdma_deregister_internal(struct rpcrdma_ia *,
				struct ib_mr *, struct ib_sge *);

int rpcrdma_register_external(struct rpcrdma_mr_seg *,
				int, int, struct rpcrdma_xprt *);
int rpcrdma_deregister_external(struct rpcrdma_mr_seg *,
				struct rpcrdma_xprt *);

/*
 * RPC/RDMA connection management calls - xprtrdma/rpc_rdma.c
 */
void rpcrdma_connect_worker(struct work_struct *);
void rpcrdma_conn_func(struct rpcrdma_ep *);
void rpcrdma_reply_handler(struct rpcrdma_rep *);

/*
 * RPC/RDMA protocol calls - xprtrdma/rpc_rdma.c
 */
ssize_t rpcrdma_marshal_chunks(struct rpc_rqst *, ssize_t);
int rpcrdma_marshal_req(struct rpc_rqst *);
size_t rpcrdma_max_payload(struct rpcrdma_xprt *);

/* Temporary NFS request map cache. Created in svc_rdma.c  */
extern struct kmem_cache *svc_rdma_map_cachep;
/* WR context cache. Created in svc_rdma.c  */
extern struct kmem_cache *svc_rdma_ctxt_cachep;
/* Workqueue created in svc_rdma.c */
extern struct workqueue_struct *svc_rdma_wq;

#if RPCSVC_MAXPAYLOAD < (RPCRDMA_MAX_DATA_SEGS << PAGE_SHIFT)
#define RPCSVC_MAXPAYLOAD_RDMA RPCSVC_MAXPAYLOAD
#else
#define RPCSVC_MAXPAYLOAD_RDMA (RPCRDMA_MAX_DATA_SEGS << PAGE_SHIFT)
#endif

#endif				/* _LINUX_SUNRPC_XPRT_RDMA_H */
