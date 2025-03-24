/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (c) 2005-2006 Network Appliance, Inc. All rights reserved.
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
 *
 * Author: Tom Tucker <tom@opengridcomputing.com>
 */

#ifndef SVC_RDMA_H
#define SVC_RDMA_H
#include <linux/llist.h>
#include <linux/sunrpc/xdr.h>
#include <linux/sunrpc/svcsock.h>
#include <linux/sunrpc/rpc_rdma.h>
#include <linux/sunrpc/rpc_rdma_cid.h>
#include <linux/sunrpc/svc_rdma_pcl.h>
#include <linux/sunrpc/rdma_rn.h>

#include <linux/percpu_counter.h>
#include <rdma/ib_verbs.h>
#include <rdma/rdma_cm.h>

/* Default and maximum inline threshold sizes */
enum {
	RPCRDMA_PULLUP_THRESH = RPCRDMA_V1_DEF_INLINE_SIZE >> 1,
	RPCRDMA_DEF_INLINE_THRESH = 4096,
	RPCRDMA_MAX_INLINE_THRESH = 65536
};

/* RPC/RDMA parameters and stats */
extern unsigned int svcrdma_ord;
extern unsigned int svcrdma_max_requests;
extern unsigned int svcrdma_max_bc_requests;
extern unsigned int svcrdma_max_req_size;
extern struct workqueue_struct *svcrdma_wq;

extern struct percpu_counter svcrdma_stat_read;
extern struct percpu_counter svcrdma_stat_recv;
extern struct percpu_counter svcrdma_stat_sq_starve;
extern struct percpu_counter svcrdma_stat_write;

struct svcxprt_rdma {
	struct svc_xprt      sc_xprt;		/* SVC transport structure */
	struct rdma_cm_id    *sc_cm_id;		/* RDMA connection id */
	struct list_head     sc_accept_q;	/* Conn. waiting accept */
	struct rpcrdma_notification sc_rn;	/* removal notification */
	int		     sc_ord;		/* RDMA read limit */
	int                  sc_max_send_sges;
	bool		     sc_snd_w_inv;	/* OK to use Send With Invalidate */

	atomic_t             sc_sq_avail;	/* SQEs ready to be consumed */
	unsigned int	     sc_sq_depth;	/* Depth of SQ */
	__be32		     sc_fc_credits;	/* Forward credits */
	u32		     sc_max_requests;	/* Max requests */
	u32		     sc_max_bc_requests;/* Backward credits */
	int                  sc_max_req_size;	/* Size of each RQ WR buf */
	u8		     sc_port_num;

	struct ib_pd         *sc_pd;

	spinlock_t	     sc_send_lock;
	struct llist_head    sc_send_ctxts;
	spinlock_t	     sc_rw_ctxt_lock;
	struct llist_head    sc_rw_ctxts;

	u32		     sc_pending_recvs;
	u32		     sc_recv_batch;
	struct list_head     sc_rq_dto_q;
	struct list_head     sc_read_complete_q;
	spinlock_t	     sc_rq_dto_lock;
	struct ib_qp         *sc_qp;
	struct ib_cq         *sc_rq_cq;
	struct ib_cq         *sc_sq_cq;

	spinlock_t	     sc_lock;		/* transport lock */

	wait_queue_head_t    sc_send_wait;	/* SQ exhaustion waitlist */
	unsigned long	     sc_flags;
	struct work_struct   sc_work;

	struct llist_head    sc_recv_ctxts;

	atomic_t	     sc_completion_ids;
};
/* sc_flags */
#define RDMAXPRT_CONN_PENDING	3

static inline struct svcxprt_rdma *svc_rdma_rqst_rdma(struct svc_rqst *rqstp)
{
	struct svc_xprt *xprt = rqstp->rq_xprt;

	return container_of(xprt, struct svcxprt_rdma, sc_xprt);
}

/*
 * Default connection parameters
 */
enum {
	RPCRDMA_LISTEN_BACKLOG	= 10,
	RPCRDMA_MAX_REQUESTS	= 64,
	RPCRDMA_MAX_BC_REQUESTS	= 2,
};

#define RPCSVC_MAXPAYLOAD_RDMA	RPCSVC_MAXPAYLOAD

/**
 * svc_rdma_send_cid_init - Initialize a Receive Queue completion ID
 * @rdma: controlling transport
 * @cid: completion ID to initialize
 */
static inline void svc_rdma_recv_cid_init(struct svcxprt_rdma *rdma,
					  struct rpc_rdma_cid *cid)
{
	cid->ci_queue_id = rdma->sc_rq_cq->res.id;
	cid->ci_completion_id = atomic_inc_return(&rdma->sc_completion_ids);
}

/**
 * svc_rdma_send_cid_init - Initialize a Send Queue completion ID
 * @rdma: controlling transport
 * @cid: completion ID to initialize
 */
static inline void svc_rdma_send_cid_init(struct svcxprt_rdma *rdma,
					  struct rpc_rdma_cid *cid)
{
	cid->ci_queue_id = rdma->sc_sq_cq->res.id;
	cid->ci_completion_id = atomic_inc_return(&rdma->sc_completion_ids);
}

/*
 * A chunk context tracks all I/O for moving one Read or Write
 * chunk. This is a set of rdma_rw's that handle data movement
 * for all segments of one chunk.
 */
struct svc_rdma_chunk_ctxt {
	struct rpc_rdma_cid	cc_cid;
	struct ib_cqe		cc_cqe;
	struct list_head	cc_rwctxts;
	ktime_t			cc_posttime;
	int			cc_sqecount;
};

struct svc_rdma_recv_ctxt {
	struct llist_node	rc_node;
	struct list_head	rc_list;
	struct ib_recv_wr	rc_recv_wr;
	struct ib_cqe		rc_cqe;
	struct rpc_rdma_cid	rc_cid;
	struct ib_sge		rc_recv_sge;
	void			*rc_recv_buf;
	struct xdr_stream	rc_stream;
	u32			rc_byte_len;
	u32			rc_inv_rkey;
	__be32			rc_msgtype;

	/* State for pulling a Read chunk */
	unsigned int		rc_pageoff;
	unsigned int		rc_curpage;
	unsigned int		rc_readbytes;
	struct xdr_buf		rc_saved_arg;
	struct svc_rdma_chunk_ctxt	rc_cc;

	struct svc_rdma_pcl	rc_call_pcl;

	struct svc_rdma_pcl	rc_read_pcl;
	struct svc_rdma_chunk	*rc_cur_result_payload;
	struct svc_rdma_pcl	rc_write_pcl;
	struct svc_rdma_pcl	rc_reply_pcl;

	unsigned int		rc_page_count;
	struct page		*rc_pages[RPCSVC_MAXPAGES];
};

/*
 * State for sending a Write chunk.
 *  - Tracks progress of writing one chunk over all its segments
 *  - Stores arguments for the SGL constructor functions
 */
struct svc_rdma_write_info {
	struct svcxprt_rdma	*wi_rdma;

	const struct svc_rdma_chunk	*wi_chunk;

	/* write state of this chunk */
	unsigned int		wi_seg_off;
	unsigned int		wi_seg_no;

	/* SGL constructor arguments */
	const struct xdr_buf	*wi_xdr;
	unsigned char		*wi_base;
	unsigned int		wi_next_off;

	struct svc_rdma_chunk_ctxt	wi_cc;
	struct work_struct	wi_work;
};

struct svc_rdma_send_ctxt {
	struct llist_node	sc_node;
	struct rpc_rdma_cid	sc_cid;
	struct work_struct	sc_work;

	struct svcxprt_rdma	*sc_rdma;
	struct ib_send_wr	sc_send_wr;
	struct ib_send_wr	*sc_wr_chain;
	int			sc_sqecount;
	struct ib_cqe		sc_cqe;
	struct xdr_buf		sc_hdrbuf;
	struct xdr_stream	sc_stream;
	struct svc_rdma_write_info sc_reply_info;
	void			*sc_xprt_buf;
	int			sc_page_count;
	int			sc_cur_sge_no;
	struct page		*sc_pages[RPCSVC_MAXPAGES];
	struct ib_sge		sc_sges[];
};

/* svc_rdma_backchannel.c */
extern void svc_rdma_handle_bc_reply(struct svc_rqst *rqstp,
				     struct svc_rdma_recv_ctxt *rctxt);

/* svc_rdma_recvfrom.c */
extern void svc_rdma_recv_ctxts_destroy(struct svcxprt_rdma *rdma);
extern bool svc_rdma_post_recvs(struct svcxprt_rdma *rdma);
extern struct svc_rdma_recv_ctxt *
		svc_rdma_recv_ctxt_get(struct svcxprt_rdma *rdma);
extern void svc_rdma_recv_ctxt_put(struct svcxprt_rdma *rdma,
				   struct svc_rdma_recv_ctxt *ctxt);
extern void svc_rdma_flush_recv_queues(struct svcxprt_rdma *rdma);
extern void svc_rdma_release_ctxt(struct svc_xprt *xprt, void *ctxt);
extern int svc_rdma_recvfrom(struct svc_rqst *);

/* svc_rdma_rw.c */
extern void svc_rdma_cc_init(struct svcxprt_rdma *rdma,
			     struct svc_rdma_chunk_ctxt *cc);
extern void svc_rdma_destroy_rw_ctxts(struct svcxprt_rdma *rdma);
extern void svc_rdma_cc_init(struct svcxprt_rdma *rdma,
			     struct svc_rdma_chunk_ctxt *cc);
extern void svc_rdma_cc_release(struct svcxprt_rdma *rdma,
				struct svc_rdma_chunk_ctxt *cc,
				enum dma_data_direction dir);
extern void svc_rdma_reply_chunk_release(struct svcxprt_rdma *rdma,
					 struct svc_rdma_send_ctxt *ctxt);
extern int svc_rdma_send_write_list(struct svcxprt_rdma *rdma,
				    const struct svc_rdma_recv_ctxt *rctxt,
				    const struct xdr_buf *xdr);
extern int svc_rdma_prepare_reply_chunk(struct svcxprt_rdma *rdma,
					const struct svc_rdma_pcl *write_pcl,
					const struct svc_rdma_pcl *reply_pcl,
					struct svc_rdma_send_ctxt *sctxt,
					const struct xdr_buf *xdr);
extern int svc_rdma_process_read_list(struct svcxprt_rdma *rdma,
				      struct svc_rqst *rqstp,
				      struct svc_rdma_recv_ctxt *head);

/* svc_rdma_sendto.c */
extern void svc_rdma_send_ctxts_destroy(struct svcxprt_rdma *rdma);
extern struct svc_rdma_send_ctxt *
		svc_rdma_send_ctxt_get(struct svcxprt_rdma *rdma);
extern void svc_rdma_send_ctxt_put(struct svcxprt_rdma *rdma,
				   struct svc_rdma_send_ctxt *ctxt);
extern int svc_rdma_post_send(struct svcxprt_rdma *rdma,
			      struct svc_rdma_send_ctxt *ctxt);
extern int svc_rdma_map_reply_msg(struct svcxprt_rdma *rdma,
				  struct svc_rdma_send_ctxt *sctxt,
				  const struct svc_rdma_pcl *write_pcl,
				  const struct svc_rdma_pcl *reply_pcl,
				  const struct xdr_buf *xdr);
extern void svc_rdma_send_error_msg(struct svcxprt_rdma *rdma,
				    struct svc_rdma_send_ctxt *sctxt,
				    struct svc_rdma_recv_ctxt *rctxt,
				    int status);
extern void svc_rdma_wake_send_waiters(struct svcxprt_rdma *rdma, int avail);
extern int svc_rdma_sendto(struct svc_rqst *);
extern int svc_rdma_result_payload(struct svc_rqst *rqstp, unsigned int offset,
				   unsigned int length);

/* svc_rdma_transport.c */
extern struct svc_xprt_class svc_rdma_class;
#ifdef CONFIG_SUNRPC_BACKCHANNEL
extern struct svc_xprt_class svc_rdma_bc_class;
#endif

/* svc_rdma.c */
extern int svc_rdma_init(void);
extern void svc_rdma_cleanup(void);

#endif
