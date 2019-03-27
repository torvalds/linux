/*
 * Copyright (c) 2018-2019 Cavium, Inc.
 * All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */



#ifndef __QLNXR_CM_H__
#define __QLNXR_CM_H__


/* ECORE LL2 has a limit to the number of buffers it can handle.
 * FYI, OFED used 512 and 128 for recv and send.
 */
#define QLNXR_GSI_MAX_RECV_WR	(4096)
#define QLNXR_GSI_MAX_SEND_WR	(4096)

#define QLNXR_GSI_MAX_RECV_SGE	(1)	/* LL2 FW limitation */

/* future OFED/kernel will have these */
#define ETH_P_ROCE		(0x8915)
#define QLNXR_ROCE_V2_UDP_SPORT	(0000)

#if __FreeBSD_version >= 1102000

#define rdma_wr(_wr) rdma_wr(_wr)
#define ud_wr(_wr) ud_wr(_wr)
#define atomic_wr(_wr) atomic_wr(_wr)

#else

#define rdma_wr(_wr) (&(_wr->wr.rdma))
#define ud_wr(_wr) (&(_wr->wr.ud))
#define atomic_wr(_wr) (&(_wr->wr.atomic))

#endif /* #if __FreeBSD_version >= 1102000 */

static inline u32 qlnxr_get_ipv4_from_gid(u8 *gid)
{
	return *(u32 *)(void *)&gid[12];
}

struct ecore_roce_ll2_header {
        void *vaddr;
        dma_addr_t baddr;
        size_t len;
};

struct ecore_roce_ll2_buffer {
        dma_addr_t baddr;
        size_t len;
};

struct ecore_roce_ll2_packet {
        struct ecore_roce_ll2_header header;
        int n_seg;
        struct ecore_roce_ll2_buffer payload[RDMA_MAX_SGE_PER_SQ_WQE];
        int roce_mode;
        enum ecore_roce_ll2_tx_dest tx_dest;
};

/* RDMA CM */

extern int qlnxr_gsi_poll_cq(struct ib_cq *ibcq,
			int num_entries,
			struct ib_wc *wc);

extern int qlnxr_gsi_post_recv(struct ib_qp *ibqp,
			struct ib_recv_wr *wr,
			struct ib_recv_wr **bad_wr);

extern int qlnxr_gsi_post_send(struct ib_qp *ibqp,
			struct ib_send_wr *wr,
			struct ib_send_wr **bad_wr);

extern struct ib_qp* qlnxr_create_gsi_qp(struct qlnxr_dev *dev,
			struct ib_qp_init_attr *attrs,
			struct qlnxr_qp *qp);

extern void qlnxr_store_gsi_qp_cq(struct qlnxr_dev *dev,
			struct qlnxr_qp *qp,
			struct ib_qp_init_attr *attrs);

extern void qlnxr_inc_sw_gsi_cons(struct qlnxr_qp_hwq_info *info);

extern int qlnxr_destroy_gsi_qp(struct qlnxr_dev *dev);

#endif /* #ifndef __QLNXR_CM_H__ */
