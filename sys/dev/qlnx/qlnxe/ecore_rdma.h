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

#ifndef __ECORE_RDMA_H__
#define __ECORE_RDMA_H__

#include "ecore_status.h"
#include "ecore.h"
#include "ecore_hsi_common.h"
#include "ecore_proto_if.h"
#include "ecore_rdma_api.h"
#include "ecore_dev_api.h"
#include "ecore_roce.h"
#include "ecore_iwarp.h"

/* Constants */

/* HW/FW RoCE Limitations (internal. For external see ecore_rdma_api.h) */
#define ECORE_RDMA_MAX_FMR                    (RDMA_MAX_TIDS) /* 2^17 - 1 */
#define ECORE_RDMA_MAX_P_KEY                  (1)
#define ECORE_RDMA_MAX_WQE                    (0x7FFF) /* 2^15 -1 */
#define ECORE_RDMA_MAX_SRQ_WQE_ELEM           (0x7FFF) /* 2^15 -1 */
#define ECORE_RDMA_PAGE_SIZE_CAPS             (0xFFFFF000) /* TODO: > 4k?! */
#define ECORE_RDMA_ACK_DELAY                  (15) /* 131 milliseconds */
#define ECORE_RDMA_MAX_MR_SIZE                (0x10000000000ULL) /* 2^40 */
#define ECORE_RDMA_MAX_CQS                    (RDMA_MAX_CQS) /* 64k */
#define ECORE_RDMA_MAX_MRS                    (RDMA_MAX_TIDS) /* 2^17 - 1 */
/* Add 1 for header element */
#define ECORE_RDMA_MAX_SRQ_ELEM_PER_WQE	      (RDMA_MAX_SGE_PER_RQ_WQE + 1)
#define ECORE_RDMA_MAX_SGE_PER_SRQ_WQE	      (RDMA_MAX_SGE_PER_RQ_WQE)
#define ECORE_RDMA_SRQ_WQE_ELEM_SIZE          (16)
#define ECORE_RDMA_MAX_SRQS		      (32 * 1024) /* 32k */

/* Configurable */
/* Max CQE is derived from u16/32 size, halved and decremented by 1 to handle
 * wrap properly and then decremented by 1 again. The latter decrement comes
 * from a requirement to create a chain that is bigger than what the user
 * requested by one:
 * The CQE size is 32 bytes but the FW writes in chunks of 64
 * bytes, for performance purposes. Allocating an extra entry and telling the
 * FW we have less prevents overwriting the first entry in case of a wrap i.e.
 * when the FW writes the last entry and the application hasn't read the first
 * one.
 */
#define ECORE_RDMA_MAX_CQE_32_BIT             (0x7FFFFFFF - 1)
#define ECORE_RDMA_MAX_CQE_16_BIT             (0x7FFF - 1)

#define ECORE_RDMA_MAX_XRC_SRQS		(RDMA_MAX_XRC_SRQS)

/* Up to 2^16 XRC Domains are supported, but the actual number of supported XRC
 * SRQs is much smaller so there's no need to have that many domains.
 */
#define ECORE_RDMA_MAX_XRCDS	(OSAL_ROUNDUP_POW_OF_TWO(RDMA_MAX_XRC_SRQS))

#define IS_IWARP(_p_hwfn) (_p_hwfn->p_rdma_info->proto == PROTOCOLID_IWARP)
#define IS_ROCE(_p_hwfn) (_p_hwfn->p_rdma_info->proto == PROTOCOLID_ROCE)

enum ecore_rdma_toggle_bit {
	ECORE_RDMA_TOGGLE_BIT_CLEAR = 0,
	ECORE_RDMA_TOGGLE_BIT_SET   = 1
};

/* @@@TBD Currently we support only affilited events
   * enum ecore_rdma_unaffiliated_event_code {
   * ECORE_RDMA_PORT_ACTIVE, // Link Up
   * ECORE_RDMA_PORT_CHANGED, // SGID table has changed
   * ECORE_RDMA_LOCAL_CATASTROPHIC_ERR, // Fatal device error
   * ECORE_RDMA_PORT_ERR, // Link down
   * };
   */

#define QEDR_MAX_BMAP_NAME	(10)
struct ecore_bmap {
	u32           max_count;
	unsigned long *bitmap;
	char name[QEDR_MAX_BMAP_NAME];
};

struct ecore_rdma_info {
	osal_spinlock_t			lock;

	struct ecore_bmap		cq_map;
	struct ecore_bmap		pd_map;
	struct ecore_bmap		xrcd_map;
	struct ecore_bmap		tid_map;
	struct ecore_bmap		srq_map;
	struct ecore_bmap		xrc_srq_map;
	struct ecore_bmap		qp_map;
	struct ecore_bmap		tcp_cid_map;
	struct ecore_bmap		cid_map;
	struct ecore_bmap		dpi_map;
	struct ecore_bmap		toggle_bits;
	struct ecore_rdma_events	events;
	struct ecore_rdma_device	*dev;
	struct ecore_rdma_port		*port;
	u32				last_tid;
	u8				num_cnqs;
	struct rdma_sent_stats          rdma_sent_pstats;
	struct rdma_rcv_stats           rdma_rcv_tstats;
	u32				num_qps;
	u32				num_mrs;
	u32				num_srqs;
	u16				srq_id_offset;
	u16				queue_zone_base;
	u16				max_queue_zones;

	struct ecore_rdma_glob_cfg	glob_cfg;

	enum protocol_type		proto;
	struct ecore_roce_info		roce;
#ifdef CONFIG_ECORE_IWARP
	struct ecore_iwarp_info		iwarp;
#endif
	bool				active;
	int				ref_cnt;
};

struct cq_prod {
	u32	req;
	u32	resp;
};

struct ecore_rdma_qp {
	struct regpair qp_handle;
	struct regpair qp_handle_async;
	u32	qpid; /* iwarp: may differ from icid */
	u16	icid;
	u16	qp_idx;
	enum ecore_roce_qp_state cur_state;
	enum ecore_rdma_qp_type qp_type;
#ifdef CONFIG_ECORE_IWARP
	enum ecore_iwarp_qp_state iwarp_state;
#endif
	bool	use_srq;
	bool	signal_all;
	bool	fmr_and_reserved_lkey;

	bool	incoming_rdma_read_en;
	bool	incoming_rdma_write_en;
	bool	incoming_atomic_en;
	bool	e2e_flow_control_en;

	u16	pd;			/* Protection domain */
	u16	pkey;			/* Primary P_key index */
	u32	dest_qp;
	u16	mtu;
	u16	srq_id;
	u8	traffic_class_tos;	/* IPv6/GRH traffic class; IPv4 TOS */
	u8	hop_limit_ttl;		/* IPv6/GRH hop limit; IPv4 TTL */
	u16	dpi;
	u32	flow_label;		/* ignored in IPv4 */
	u16	vlan_id;
	u32	ack_timeout;
	u8	retry_cnt;
	u8	rnr_retry_cnt;
	u8	min_rnr_nak_timer;
	bool	sqd_async;
	union ecore_gid	sgid;		/* GRH SGID; IPv4/6 Source IP */
	union ecore_gid	dgid;		/* GRH DGID; IPv4/6 Destination IP */
	enum roce_mode roce_mode;
	u16	udp_src_port;		/* RoCEv2 only */
	u8	stats_queue;

	/* requeseter */
	u8	max_rd_atomic_req;
	u32     sq_psn;
	u16	sq_cq_id; /* The cq to be associated with the send queue*/
	u16	sq_num_pages;
	dma_addr_t sq_pbl_ptr;
	void	*orq;
	dma_addr_t orq_phys_addr;
	u8	orq_num_pages;
	bool	req_offloaded;
	bool	has_req;

	/* responder */
	u8	max_rd_atomic_resp;
	u32     rq_psn;
	u16	rq_cq_id; /* The cq to be associated with the receive queue */
	u16	rq_num_pages;
	dma_addr_t rq_pbl_ptr;
	void	*irq;
	dma_addr_t irq_phys_addr;
	u8	irq_num_pages;
	bool	resp_offloaded;
	bool	has_resp;
	struct cq_prod	cq_prod;

	u8	remote_mac_addr[6];
	u8	local_mac_addr[6];

	void	*shared_queue;
	dma_addr_t shared_queue_phys_addr;
#ifdef CONFIG_ECORE_IWARP
	struct ecore_iwarp_ep *ep;
#endif

	u16 xrcd_id;
};

static OSAL_INLINE bool ecore_rdma_is_xrc_qp(struct ecore_rdma_qp *qp)
{
	if ((qp->qp_type == ECORE_RDMA_QP_TYPE_XRC_TGT) ||
	    (qp->qp_type == ECORE_RDMA_QP_TYPE_XRC_INI))
		return 1;

	return 0;
}

enum _ecore_status_t ecore_rdma_info_alloc(struct ecore_hwfn *p_hwfn);
void ecore_rdma_info_free(struct ecore_hwfn *p_hwfn);

enum _ecore_status_t
ecore_rdma_bmap_alloc(struct ecore_hwfn *p_hwfn,
		      struct ecore_bmap *bmap,
		      u32 max_count,
		      char *name);

void
ecore_rdma_bmap_free(struct ecore_hwfn *p_hwfn,
		     struct ecore_bmap *bmap,
		     bool check);

enum _ecore_status_t
ecore_rdma_bmap_alloc_id(struct ecore_hwfn *p_hwfn,
			 struct ecore_bmap *bmap,
			 u32 *id_num);

void
ecore_bmap_set_id(struct ecore_hwfn *p_hwfn,
		  struct ecore_bmap *bmap,
		  u32 id_num);

void
ecore_bmap_release_id(struct ecore_hwfn *p_hwfn,
		      struct ecore_bmap *bmap,
		      u32 id_num);

int
ecore_bmap_test_id(struct ecore_hwfn *p_hwfn,
		   struct ecore_bmap *bmap,
		   u32 id_num);

void
ecore_rdma_set_fw_mac(u16 *p_fw_mac, u8 *p_ecore_mac);

bool
ecore_rdma_allocated_qps(struct ecore_hwfn *p_hwfn);

u16 ecore_rdma_get_fw_srq_id(struct ecore_hwfn *p_hwfn, u16 id, bool is_xrc);

#endif /*__ECORE_RDMA_H__*/

