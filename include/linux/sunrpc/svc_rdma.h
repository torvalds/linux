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
#include <linux/sunrpc/xdr.h>
#include <linux/sunrpc/svcsock.h>
#include <linux/sunrpc/rpc_rdma.h>
#include <rdma/ib_verbs.h>
#include <rdma/rdma_cm.h>
#define SVCRDMA_DEBUG

/* Default and maximum inline threshold sizes */
enum {
	RPCRDMA_DEF_INLINE_THRESH = 4096,
	RPCRDMA_MAX_INLINE_THRESH = 65536
};

/* RPC/RDMA parameters and stats */
extern unsigned int svcrdma_ord;
extern unsigned int svcrdma_max_requests;
extern unsigned int svcrdma_max_bc_requests;
extern unsigned int svcrdma_max_req_size;

extern atomic_t rdma_stat_recv;
extern atomic_t rdma_stat_read;
extern atomic_t rdma_stat_write;
extern atomic_t rdma_stat_sq_starve;
extern atomic_t rdma_stat_rq_starve;
extern atomic_t rdma_stat_rq_poll;
extern atomic_t rdma_stat_rq_prod;
extern atomic_t rdma_stat_sq_poll;
extern atomic_t rdma_stat_sq_prod;

/*
 * Contexts are built when an RDMA request is created and are a
 * record of the resources that can be recovered when the request
 * completes.
 */
struct svc_rdma_op_ctxt {
	struct list_head list;
	struct xdr_buf arg;
	struct ib_cqe cqe;
	u32 byte_len;
	struct svcxprt_rdma *xprt;
	enum dma_data_direction direction;
	int count;
	unsigned int mapped_sges;
	int hdr_count;
	struct ib_send_wr send_wr;
	struct ib_sge sge[1 + RPCRDMA_MAX_INLINE_THRESH / PAGE_SIZE];
	struct page *pages[RPCSVC_MAXPAGES];
};

struct svcxprt_rdma {
	struct svc_xprt      sc_xprt;		/* SVC transport structure */
	struct rdma_cm_id    *sc_cm_id;		/* RDMA connection id */
	struct list_head     sc_accept_q;	/* Conn. waiting accept */
	int		     sc_ord;		/* RDMA read limit */
	int                  sc_max_sge;
	bool		     sc_snd_w_inv;	/* OK to use Send With Invalidate */

	atomic_t             sc_sq_avail;	/* SQEs ready to be consumed */
	unsigned int	     sc_sq_depth;	/* Depth of SQ */
	__be32		     sc_fc_credits;	/* Forward credits */
	u32		     sc_max_requests;	/* Max requests */
	u32		     sc_max_bc_requests;/* Backward credits */
	int                  sc_max_req_size;	/* Size of each RQ WR buf */
	u8		     sc_port_num;

	struct ib_pd         *sc_pd;

	spinlock_t	     sc_ctxt_lock;
	struct list_head     sc_ctxts;
	int		     sc_ctxt_used;
	spinlock_t	     sc_rw_ctxt_lock;
	struct list_head     sc_rw_ctxts;

	struct list_head     sc_rq_dto_q;
	spinlock_t	     sc_rq_dto_lock;
	struct ib_qp         *sc_qp;
	struct ib_cq         *sc_rq_cq;
	struct ib_cq         *sc_sq_cq;

	spinlock_t	     sc_lock;		/* transport lock */

	wait_queue_head_t    sc_send_wait;	/* SQ exhaustion waitlist */
	unsigned long	     sc_flags;
	struct list_head     sc_read_complete_q;
	struct work_struct   sc_work;

	spinlock_t	     sc_recv_lock;
	struct list_head     sc_recv_ctxts;
};
/* sc_flags */
#define RDMAXPRT_CONN_PENDING	3

#define RPCRDMA_LISTEN_BACKLOG  10
#define RPCRDMA_MAX_REQUESTS    32

/* Typical ULP usage of BC requests is NFSv4.1 backchannel. Our
 * current NFSv4.1 implementation supports one backchannel slot.
 */
#define RPCRDMA_MAX_BC_REQUESTS	2

#define RPCSVC_MAXPAYLOAD_RDMA	RPCSVC_MAXPAYLOAD

struct svc_rdma_recv_ctxt {
	struct list_head	rc_list;
	struct ib_recv_wr	rc_recv_wr;
	struct ib_cqe		rc_cqe;
	struct ib_sge		rc_recv_sge;
	void			*rc_recv_buf;
	struct xdr_buf		rc_arg;
	u32			rc_byte_len;
	unsigned int		rc_page_count;
	unsigned int		rc_hdr_count;
	struct page		*rc_pages[RPCSVC_MAXPAGES];
};

/* Track DMA maps for this transport and context */
static inline void svc_rdma_count_mappings(struct svcxprt_rdma *rdma,
					   struct svc_rdma_op_ctxt *ctxt)
{
	ctxt->mapped_sges++;
}

/* svc_rdma_backchannel.c */
extern int svc_rdma_handle_bc_reply(struct rpc_xprt *xprt,
				    __be32 *rdma_resp,
				    struct xdr_buf *rcvbuf);

/* svc_rdma_recvfrom.c */
extern void svc_rdma_recv_ctxts_destroy(struct svcxprt_rdma *rdma);
extern bool svc_rdma_post_recvs(struct svcxprt_rdma *rdma);
extern void svc_rdma_recv_ctxt_put(struct svcxprt_rdma *rdma,
				   struct svc_rdma_recv_ctxt *ctxt);
extern void svc_rdma_flush_recv_queues(struct svcxprt_rdma *rdma);
extern int svc_rdma_recvfrom(struct svc_rqst *);

/* svc_rdma_rw.c */
extern void svc_rdma_destroy_rw_ctxts(struct svcxprt_rdma *rdma);
extern int svc_rdma_recv_read_chunk(struct svcxprt_rdma *rdma,
				    struct svc_rqst *rqstp,
				    struct svc_rdma_recv_ctxt *head, __be32 *p);
extern int svc_rdma_send_write_chunk(struct svcxprt_rdma *rdma,
				     __be32 *wr_ch, struct xdr_buf *xdr);
extern int svc_rdma_send_reply_chunk(struct svcxprt_rdma *rdma,
				     __be32 *rp_ch, bool writelist,
				     struct xdr_buf *xdr);

/* svc_rdma_sendto.c */
extern int svc_rdma_map_reply_hdr(struct svcxprt_rdma *rdma,
				  struct svc_rdma_op_ctxt *ctxt,
				  __be32 *rdma_resp, unsigned int len);
extern int svc_rdma_post_send_wr(struct svcxprt_rdma *rdma,
				 struct svc_rdma_op_ctxt *ctxt,
				 int num_sge, u32 inv_rkey);
extern int svc_rdma_sendto(struct svc_rqst *);

/* svc_rdma_transport.c */
extern void svc_rdma_wc_send(struct ib_cq *, struct ib_wc *);
extern void svc_rdma_wc_reg(struct ib_cq *, struct ib_wc *);
extern void svc_rdma_wc_read(struct ib_cq *, struct ib_wc *);
extern void svc_rdma_wc_inv(struct ib_cq *, struct ib_wc *);
extern int svc_rdma_send(struct svcxprt_rdma *, struct ib_send_wr *);
extern int svc_rdma_create_listen(struct svc_serv *, int, struct sockaddr *);
extern struct svc_rdma_op_ctxt *svc_rdma_get_context(struct svcxprt_rdma *);
extern void svc_rdma_put_context(struct svc_rdma_op_ctxt *, int);
extern void svc_rdma_unmap_dma(struct svc_rdma_op_ctxt *ctxt);
extern void svc_sq_reap(struct svcxprt_rdma *);
extern void svc_rq_reap(struct svcxprt_rdma *);
extern void svc_rdma_prep_reply_hdr(struct svc_rqst *);

extern struct svc_xprt_class svc_rdma_class;
#ifdef CONFIG_SUNRPC_BACKCHANNEL
extern struct svc_xprt_class svc_rdma_bc_class;
#endif

/* svc_rdma.c */
extern struct workqueue_struct *svc_rdma_wq;
extern int svc_rdma_init(void);
extern void svc_rdma_cleanup(void);

#endif
