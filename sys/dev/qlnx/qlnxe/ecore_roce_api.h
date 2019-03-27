/*
 * Copyright (c) 2017-2018 Cavium, Inc. 
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
 *
 */

#ifndef __ECORE_RDMA_API_H__
#define __ECORE_RDMA_API_H__

#ifndef ETH_ALEN
#define ETH_ALEN 6
#endif


enum ecore_roce_ll2_tx_dest
{
	ECORE_ROCE_LL2_TX_DEST_NW /* Light L2 TX Destination to the Network */,
	ECORE_ROCE_LL2_TX_DEST_LB /* Light L2 TX Destination to the Loopback */,
	ECORE_ROCE_LL2_TX_DEST_MAX
};

/* HW/FW RoCE Limitations (external. For internal see ecore_roce.h) */
/* CNQ size Limitation
 * The CNQ size should be set as twice the amount of CQs, since for each CQ one
 * element may be inserted into the CNQ and another element is used per CQ to
 * accommodate for a possible race in the arm mechanism.
 * The FW supports a CNQ of 64k-1 and this apparently causes an issue - notice
 * that the number of QPs can reach 32k giving 64k CQs and 128k CNQ elements.
 * Luckily the FW can buffer CNQ elements avoiding an overflow, on the expense
 * of performance.
 */
#define ECORE_RDMA_MAX_CNQ_SIZE               (0xFFFF) /* 2^16 - 1 */

/* rdma interface */
enum ecore_rdma_tid_type
{
	ECORE_RDMA_TID_REGISTERED_MR,
	ECORE_RDMA_TID_FMR,
	ECORE_RDMA_TID_MW_TYPE1,
	ECORE_RDMA_TID_MW_TYPE2A
};

enum ecore_roce_qp_state {
	ECORE_ROCE_QP_STATE_RESET, /* Reset */
	ECORE_ROCE_QP_STATE_INIT,  /* Initialized */
	ECORE_ROCE_QP_STATE_RTR,   /* Ready to Receive */
	ECORE_ROCE_QP_STATE_RTS,   /* Ready to Send */
	ECORE_ROCE_QP_STATE_SQD,   /* Send Queue Draining */
	ECORE_ROCE_QP_STATE_ERR,   /* Error */
	ECORE_ROCE_QP_STATE_SQE    /* Send Queue Error */
};

typedef
void (*affiliated_event_t)(void	*context,
			   u8	fw_event_code,
			   void	*fw_handle);

typedef
void (*unaffiliated_event_t)(void *context,
			     u8   event_code);

struct ecore_rdma_events {
	void			*context;
	affiliated_event_t	affiliated_event;
	unaffiliated_event_t	unaffiliated_event;
};

struct ecore_rdma_device {
    /* Vendor specific information */
	u32	vendor_id;
	u32	vendor_part_id;
	u32	hw_ver;
	u64	fw_ver;

	u64	node_guid; /* node GUID */
	u64	sys_image_guid; /* System image GUID */

	u8	max_cnq;
	u8	max_sge; /* The maximum number of scatter/gather entries
			  * per Work Request supported
			  */
	u8	max_srq_sge; /* The maximum number of scatter/gather entries
			      * per Work Request supported for SRQ
			      */
	u16	max_inline;
	u32	max_wqe; /* The maximum number of outstanding work
			  * requests on any Work Queue supported
			  */
	u32	max_srq_wqe; /* The maximum number of outstanding work
			      * requests on any Work Queue supported for SRQ
			      */
	u8	max_qp_resp_rd_atomic_resc; /* The maximum number of RDMA Reads
					     * & atomic operation that can be
					     * outstanding per QP
					     */

	u8	max_qp_req_rd_atomic_resc; /* The maximum depth per QP for
					    * initiation of RDMA Read
					    * & atomic operations
					    */
	u64	max_dev_resp_rd_atomic_resc;
	u32	max_cq;
	u32	max_qp;
	u32	max_srq; /* Maximum number of SRQs */
	u32	max_mr; /* Maximum number of MRs supported by this device */
	u64	max_mr_size; /* Size (in bytes) of the largest contiguous memory
			      * block that can be registered by this device
			      */
	u32	max_cqe;
	u32	max_mw; /* The maximum number of memory windows supported */
	u32	max_fmr;
	u32	max_mr_mw_fmr_pbl;
	u64	max_mr_mw_fmr_size;
	u32	max_pd; /* The maximum number of protection domains supported */
	u32	max_ah;
	u8	max_pkey;
	u16	max_srq_wr; /* Maximum number of WRs per SRQ */
	u8	max_stats_queues; /* Maximum number of statistics queues */
	u32	dev_caps;

	/* Abilty to support RNR-NAK generation */

#define ECORE_RDMA_DEV_CAP_RNR_NAK_MASK				0x1
#define ECORE_RDMA_DEV_CAP_RNR_NAK_SHIFT			0
	/* Abilty to support shutdown port */
#define ECORE_RDMA_DEV_CAP_SHUTDOWN_PORT_MASK			0x1
#define ECORE_RDMA_DEV_CAP_SHUTDOWN_PORT_SHIFT			1
	/* Abilty to support port active event */
#define ECORE_RDMA_DEV_CAP_PORT_ACTIVE_EVENT_MASK		0x1
#define ECORE_RDMA_DEV_CAP_PORT_ACTIVE_EVENT_SHIFT		2
	/* Abilty to support port change event */
#define ECORE_RDMA_DEV_CAP_PORT_CHANGE_EVENT_MASK		0x1
#define ECORE_RDMA_DEV_CAP_PORT_CHANGE_EVENT_SHIFT		3
	/* Abilty to support system image GUID */
#define ECORE_RDMA_DEV_CAP_SYS_IMAGE_MASK			0x1
#define ECORE_RDMA_DEV_CAP_SYS_IMAGE_SHIFT			4
	/* Abilty to support bad P_Key counter support */
#define ECORE_RDMA_DEV_CAP_BAD_PKEY_CNT_MASK			0x1
#define ECORE_RDMA_DEV_CAP_BAD_PKEY_CNT_SHIFT			5
	/* Abilty to support atomic operations */
#define ECORE_RDMA_DEV_CAP_ATOMIC_OP_MASK			0x1
#define ECORE_RDMA_DEV_CAP_ATOMIC_OP_SHIFT			6
#define ECORE_RDMA_DEV_CAP_RESIZE_CQ_MASK			0x1
#define ECORE_RDMA_DEV_CAP_RESIZE_CQ_SHIFT			7
	/* Abilty to support modifying the maximum number of
	 * outstanding work requests per QP
	 */
#define ECORE_RDMA_DEV_CAP_RESIZE_MAX_WR_MASK			0x1
#define ECORE_RDMA_DEV_CAP_RESIZE_MAX_WR_SHIFT			8
	/* Abilty to support automatic path migration */
#define ECORE_RDMA_DEV_CAP_AUTO_PATH_MIG_MASK			0x1
#define ECORE_RDMA_DEV_CAP_AUTO_PATH_MIG_SHIFT			9
	/* Abilty to support the base memory management extensions */
#define ECORE_RDMA_DEV_CAP_BASE_MEMORY_EXT_MASK			0x1
#define ECORE_RDMA_DEV_CAP_BASE_MEMORY_EXT_SHIFT		10
#define ECORE_RDMA_DEV_CAP_BASE_QUEUE_EXT_MASK			0x1
#define ECORE_RDMA_DEV_CAP_BASE_QUEUE_EXT_SHIFT			11
	/* Abilty to support multipile page sizes per memory region */
#define ECORE_RDMA_DEV_CAP_MULTI_PAGE_PER_MR_EXT_MASK		0x1
#define ECORE_RDMA_DEV_CAP_MULTI_PAGE_PER_MR_EXT_SHIFT		12
	/* Abilty to support block list physical buffer list */
#define ECORE_RDMA_DEV_CAP_BLOCK_MODE_MASK			0x1
#define ECORE_RDMA_DEV_CAP_BLOCK_MODE_SHIFT			13
	/* Abilty to support zero based virtual addresses */
#define ECORE_RDMA_DEV_CAP_ZBVA_MASK				0x1
#define ECORE_RDMA_DEV_CAP_ZBVA_SHIFT				14
	/* Abilty to support local invalidate fencing */
#define ECORE_RDMA_DEV_CAP_LOCAL_INV_FENCE_MASK			0x1
#define ECORE_RDMA_DEV_CAP_LOCAL_INV_FENCE_SHIFT		15
	/* Abilty to support Loopback on QP */
#define ECORE_RDMA_DEV_CAP_LB_INDICATOR_MASK			0x1
#define ECORE_RDMA_DEV_CAP_LB_INDICATOR_SHIFT			16
	u64	page_size_caps;
	u8	dev_ack_delay;
	u32	reserved_lkey; /* Value of reserved L_key */
	u32	bad_pkey_counter; /* Bad P_key counter support indicator */
	struct ecore_rdma_events events;
};

enum ecore_port_state {
	ECORE_RDMA_PORT_UP,
	ECORE_RDMA_PORT_DOWN,
};

enum ecore_roce_capability {
	ECORE_ROCE_V1	= 1 << 0,
	ECORE_ROCE_V2	= 1 << 1,
};

struct ecore_rdma_port {
	enum ecore_port_state port_state;
	int	link_speed;
	u64	max_msg_size;
	u8	source_gid_table_len;
	void	*source_gid_table_ptr;
	u8	pkey_table_len;
	void	*pkey_table_ptr;
	u32	pkey_bad_counter;
	enum ecore_roce_capability capability;
};

struct ecore_rdma_cnq_params
{
	u8  num_pbl_pages; /* Number of pages in the PBL allocated
				   * for this queue
				   */
	u64 pbl_ptr; /* Address to the first entry of the queue PBL */
};

/* The CQ Mode affects the CQ doorbell transaction size.
 * 64/32 bit machines should configure to 32/16 bits respectively.
 */
enum ecore_rdma_cq_mode {
	ECORE_RDMA_CQ_MODE_16_BITS,
	ECORE_RDMA_CQ_MODE_32_BITS,
};

struct ecore_roce_dcqcn_params {
	u8	notification_point;
	u8	reaction_point;

	/* fields for notification point */
	u32	cnp_send_timeout;

	/* fields for reaction point */
	u32	rl_bc_rate;  /* Byte Counter Limit. */
	u16	rl_max_rate; /* Maximum rate in 1.6 Mbps resolution */
	u16	rl_r_ai;     /* Active increase rate */
	u16	rl_r_hai;    /* Hyper active increase rate */
	u16	dcqcn_g;     /* Alpha update gain in 1/64K resolution */
	u32	dcqcn_k_us;  /* Alpha update interval */
	u32	dcqcn_timeout_us;
};

#ifdef CONFIG_ECORE_IWARP

#define ECORE_MPA_RTR_TYPE_NONE		0 /* No RTR type */
#define ECORE_MPA_RTR_TYPE_ZERO_SEND	(1 << 0)
#define ECORE_MPA_RTR_TYPE_ZERO_WRITE	(1 << 1)
#define ECORE_MPA_RTR_TYPE_ZERO_READ	(1 << 2)

enum ecore_mpa_rev {
	ECORE_MPA_REV1,
	ECORE_MPA_REV2,
};

struct ecore_iwarp_params {
	u32				rcv_wnd_size;
	u16				ooo_num_rx_bufs;
#define ECORE_IWARP_TS_EN (1 << 0)
#define ECORE_IWARP_DA_EN (1 << 1)
	u8				flags;
	u8				crc_needed;
	enum ecore_mpa_rev		mpa_rev;
	u8				mpa_rtr;
	u8				mpa_peer2peer;
};

#endif

struct ecore_roce_params {
	enum ecore_rdma_cq_mode		cq_mode;
	struct ecore_roce_dcqcn_params	dcqcn_params;
	u8				ll2_handle; /* required for UD QPs */
};

struct ecore_rdma_start_in_params {
	struct ecore_rdma_events	*events;
	struct ecore_rdma_cnq_params	cnq_pbl_list[128];
	u8				desired_cnq;
	u16				max_mtu;
	u8				mac_addr[ETH_ALEN];
#ifdef CONFIG_ECORE_IWARP
	struct ecore_iwarp_params	iwarp;
#endif
	struct ecore_roce_params	roce;
};

struct ecore_rdma_add_user_out_params {
	/* output variables (given to miniport) */
	u16	dpi;
	u64	dpi_addr;
	u64	dpi_phys_addr;
	u32	dpi_size;
	u16	wid_count;
};

/*Returns the CQ CID or zero in case of failure */
struct ecore_rdma_create_cq_in_params {
	/* input variables (given by miniport) */
	u32	cq_handle_lo; /* CQ handle to be written in CNQ */
	u32	cq_handle_hi;
	u32	cq_size;
	u16	dpi;
	bool	pbl_two_level;
	u64	pbl_ptr;
	u16	pbl_num_pages;
	u8	pbl_page_size_log; /* for the pages that contain the
			   * pointers to the CQ pages
			   */
	u8	cnq_id;
	u16	int_timeout;
};


struct ecore_rdma_resize_cq_in_params {
	/* input variables (given by miniport) */

	u16	icid;
	u32	cq_size;
	bool	pbl_two_level;
	u64	pbl_ptr;
	u16	pbl_num_pages;
	u8	pbl_page_size_log; /* for the pages that contain the
		       * pointers to the CQ pages
		       */
};


enum roce_mode
{
	ROCE_V1,
	ROCE_V2_IPV4,
	ROCE_V2_IPV6,
	MAX_ROCE_MODE
};

struct ecore_rdma_create_qp_in_params {
	/* input variables (given by miniport) */
	u32	qp_handle_lo; /* QP handle to be written in CQE */
	u32	qp_handle_hi;
	u32	qp_handle_async_lo; /* QP handle to be written in async event */
	u32	qp_handle_async_hi;
	bool	use_srq;
	bool	signal_all;
	bool	fmr_and_reserved_lkey;
	u16	pd;
	u16	dpi;
	u16	sq_cq_id;
	u16	sq_num_pages;
	u64	sq_pbl_ptr;	/* Not relevant for iWARP */
	u8	max_sq_sges;
	u16	rq_cq_id;
	u16	rq_num_pages;
	u64	rq_pbl_ptr;	/* Not relevant for iWARP */
	u16	srq_id;
	u8	stats_queue;
};

struct ecore_rdma_create_qp_out_params {
	/* output variables (given to miniport) */
	u32		qp_id;
	u16		icid;
	void		*rq_pbl_virt;
	dma_addr_t	rq_pbl_phys;
	void		*sq_pbl_virt;
	dma_addr_t	sq_pbl_phys;
};

struct ecore_rdma_destroy_cq_in_params {
	/* input variables (given by miniport) */
	u16 icid;
};

struct ecore_rdma_destroy_cq_out_params {
	/* output variables, provided to the upper layer */

	/* Sequence number of completion notification sent for the CQ on
	 * the associated CNQ
	 */
	u16	num_cq_notif;
};

/* ECORE GID can be used as IPv4/6 address in RoCE v2 */
union ecore_gid {
	u8 bytes[16];
	u16 words[8];
	u32 dwords[4];
	u64 qwords[2];
	u32 ipv4_addr;
};

struct ecore_rdma_modify_qp_in_params {
	/* input variables (given by miniport) */
	u32		modify_flags;
#define ECORE_RDMA_MODIFY_QP_VALID_NEW_STATE_MASK               0x1
#define ECORE_RDMA_MODIFY_QP_VALID_NEW_STATE_SHIFT              0
#define ECORE_ROCE_MODIFY_QP_VALID_PKEY_MASK                    0x1
#define ECORE_ROCE_MODIFY_QP_VALID_PKEY_SHIFT                   1
#define ECORE_RDMA_MODIFY_QP_VALID_RDMA_OPS_EN_MASK             0x1
#define ECORE_RDMA_MODIFY_QP_VALID_RDMA_OPS_EN_SHIFT            2
#define ECORE_ROCE_MODIFY_QP_VALID_DEST_QP_MASK                 0x1
#define ECORE_ROCE_MODIFY_QP_VALID_DEST_QP_SHIFT                3
#define ECORE_ROCE_MODIFY_QP_VALID_ADDRESS_VECTOR_MASK          0x1
#define ECORE_ROCE_MODIFY_QP_VALID_ADDRESS_VECTOR_SHIFT         4
#define ECORE_ROCE_MODIFY_QP_VALID_RQ_PSN_MASK                  0x1
#define ECORE_ROCE_MODIFY_QP_VALID_RQ_PSN_SHIFT                 5
#define ECORE_ROCE_MODIFY_QP_VALID_SQ_PSN_MASK                  0x1
#define ECORE_ROCE_MODIFY_QP_VALID_SQ_PSN_SHIFT                 6
#define ECORE_RDMA_MODIFY_QP_VALID_MAX_RD_ATOMIC_REQ_MASK       0x1
#define ECORE_RDMA_MODIFY_QP_VALID_MAX_RD_ATOMIC_REQ_SHIFT      7
#define ECORE_RDMA_MODIFY_QP_VALID_MAX_RD_ATOMIC_RESP_MASK      0x1
#define ECORE_RDMA_MODIFY_QP_VALID_MAX_RD_ATOMIC_RESP_SHIFT     8
#define ECORE_ROCE_MODIFY_QP_VALID_ACK_TIMEOUT_MASK             0x1
#define ECORE_ROCE_MODIFY_QP_VALID_ACK_TIMEOUT_SHIFT            9
#define ECORE_ROCE_MODIFY_QP_VALID_RETRY_CNT_MASK               0x1
#define ECORE_ROCE_MODIFY_QP_VALID_RETRY_CNT_SHIFT              10
#define ECORE_ROCE_MODIFY_QP_VALID_RNR_RETRY_CNT_MASK           0x1
#define ECORE_ROCE_MODIFY_QP_VALID_RNR_RETRY_CNT_SHIFT          11
#define ECORE_ROCE_MODIFY_QP_VALID_MIN_RNR_NAK_TIMER_MASK       0x1
#define ECORE_ROCE_MODIFY_QP_VALID_MIN_RNR_NAK_TIMER_SHIFT      12
#define ECORE_ROCE_MODIFY_QP_VALID_E2E_FLOW_CONTROL_EN_MASK     0x1
#define ECORE_ROCE_MODIFY_QP_VALID_E2E_FLOW_CONTROL_EN_SHIFT    13
#define ECORE_ROCE_MODIFY_QP_VALID_ROCE_MODE_MASK               0x1
#define ECORE_ROCE_MODIFY_QP_VALID_ROCE_MODE_SHIFT              14

	enum ecore_roce_qp_state	new_state;
	u16		pkey;
	bool		incoming_rdma_read_en;
	bool		incoming_rdma_write_en;
	bool		incoming_atomic_en;
	bool		e2e_flow_control_en;
	u32		dest_qp;
	u16		mtu;
	u8		traffic_class_tos; /* IPv6/GRH tc; IPv4 TOS */
	u8		hop_limit_ttl; /* IPv6/GRH hop limit; IPv4 TTL */
	u32		flow_label; /* ignored in IPv4 */
	union ecore_gid	sgid; /* GRH SGID; IPv4/6 Source IP */
	union ecore_gid	dgid; /* GRH DGID; IPv4/6 Destination IP */
	u16		udp_src_port; /* RoCEv2 only */

	u16		vlan_id;

	u32		rq_psn;
	u32		sq_psn;
	u8		max_rd_atomic_resp;
	u8		max_rd_atomic_req;
	u32		ack_timeout;
	u8		retry_cnt;
	u8		rnr_retry_cnt;
	u8		min_rnr_nak_timer;
	bool		sqd_async;
	u8		remote_mac_addr[6];
	u8		local_mac_addr[6];
	bool		use_local_mac;
	enum roce_mode	roce_mode;
};

struct ecore_rdma_query_qp_out_params {
	/* output variables (given to miniport) */
	enum ecore_roce_qp_state	state;
	u32		rq_psn; /* responder */
	u32		sq_psn; /* requester */
	bool		draining; /* send queue is draining */
	u16		mtu;
	u32		dest_qp;
	bool		incoming_rdma_read_en;
	bool		incoming_rdma_write_en;
	bool		incoming_atomic_en;
	bool		e2e_flow_control_en;
	union ecore_gid sgid; /* GRH SGID; IPv4/6 Source IP */
	union ecore_gid dgid; /* GRH DGID; IPv4/6 Destination IP */
	u32		flow_label; /* ignored in IPv4 */
	u8		hop_limit_ttl; /* IPv6/GRH hop limit; IPv4 TTL */
	u8		traffic_class_tos; /* IPv6/GRH tc; IPv4 TOS */
	u32		timeout;
	u8		rnr_retry;
	u8		retry_cnt;
	u8		min_rnr_nak_timer;
	u16		pkey_index;
	u8		max_rd_atomic;
	u8		max_dest_rd_atomic;
	bool		sqd_async;
};

struct ecore_rdma_register_tid_in_params {
	/* input variables (given by miniport) */
	u32	itid; /* index only, 18 bit long, lkey = itid << 8 | key */
	enum ecore_rdma_tid_type tid_type;
	u8	key;
	u16	pd;
	bool	local_read;
	bool	local_write;
	bool	remote_read;
	bool	remote_write;
	bool	remote_atomic;
	bool	mw_bind;
	u64	pbl_ptr;
	bool	pbl_two_level;
	u8	pbl_page_size_log; /* for the pages that contain the pointers
		       * to the MR pages
		       */
	u8	page_size_log; /* for the MR pages */
	u32	fbo;
	u64	length; /* only lower 40 bits are valid */
	u64	vaddr;
	bool	zbva;
	bool	phy_mr;
	bool	dma_mr;

	/* DIF related fields */
	bool	dif_enabled;
	u64	dif_error_addr;
	u64	dif_runt_addr;
};

struct ecore_rdma_create_srq_in_params	{
	u64 pbl_base_addr;
	u64 prod_pair_addr;
	u16 num_pages;
	u16 pd_id;
	u16 page_size;
};

struct ecore_rdma_create_srq_out_params {
	u16 srq_id;
};

struct ecore_rdma_destroy_srq_in_params {
	u16 srq_id;
};

struct ecore_rdma_modify_srq_in_params {
	u32 wqe_limit;
	u16 srq_id;
};

struct ecore_rdma_resize_cq_out_params {
	/* output variables, provided to the upper layer */
	u32 prod; /* CQ producer value on old PBL */
	u32 cons; /* CQ consumer value on old PBL */
};

struct ecore_rdma_resize_cnq_in_params {
	/* input variables (given by miniport) */
	u32	cnq_id;
	u32	pbl_page_size_log; /* for the pages that contain the
			* pointers to the cnq pages
			*/
	u64	pbl_ptr;
};

struct ecore_rdma_stats_out_params {
	u64	sent_bytes;
	u64	sent_pkts;
	u64	rcv_bytes;
	u64	rcv_pkts;

	/* RoCE only */
	u64	icrc_errors;		/* wraps at 32 bits */
	u64	retransmit_events;	/* wraps at 32 bits */
	u64	silent_drops;		/* wraps at 16 bits */
	u64	rnr_nacks_sent;		/* wraps at 16 bits */

	/* iWARP only */
	u64	iwarp_tx_fast_rxmit_cnt;
	u64	iwarp_tx_slow_start_cnt;
	u64	unalign_rx_comp;
};

struct ecore_rdma_counters_out_params {
	u64	pd_count;
	u64	max_pd;
	u64	dpi_count;
	u64	max_dpi;
	u64	cq_count;
	u64	max_cq;
	u64	qp_count;
	u64	max_qp;
	u64	tid_count;
	u64	max_tid;
};

enum _ecore_status_t
ecore_rdma_add_user(void *rdma_cxt,
		    struct ecore_rdma_add_user_out_params *out_params);

enum _ecore_status_t
ecore_rdma_alloc_pd(void *rdma_cxt,
		    u16	*pd);

enum _ecore_status_t
ecore_rdma_alloc_tid(void *rdma_cxt,
		     u32 *tid);

enum _ecore_status_t
ecore_rdma_create_cq(void *rdma_cxt,
		     struct ecore_rdma_create_cq_in_params *params,
		     u16 *icid);

/* Returns a pointer to the responders' CID, which is also a pointer to the
 * ecore_qp_params struct. Returns NULL in case of failure.
 */
struct ecore_rdma_qp*
ecore_rdma_create_qp(void *rdma_cxt,
		     struct ecore_rdma_create_qp_in_params  *in_params,
		     struct ecore_rdma_create_qp_out_params *out_params);

enum _ecore_status_t
ecore_roce_create_ud_qp(void *rdma_cxt,
			struct ecore_rdma_create_qp_out_params *out_params);

enum _ecore_status_t
ecore_rdma_deregister_tid(void *rdma_cxt,
			  u32		tid);

enum _ecore_status_t
ecore_rdma_destroy_cq(void *rdma_cxt,
		      struct ecore_rdma_destroy_cq_in_params  *in_params,
		      struct ecore_rdma_destroy_cq_out_params *out_params);

enum _ecore_status_t
ecore_rdma_destroy_qp(void *rdma_cxt,
		      struct ecore_rdma_qp *qp);

enum _ecore_status_t
ecore_roce_destroy_ud_qp(void *rdma_cxt, u16 cid);

void
ecore_rdma_free_pd(void *rdma_cxt,
		   u16	pd);

void
ecore_rdma_free_tid(void *rdma_cxt,
		    u32	tid);

enum _ecore_status_t
ecore_rdma_modify_qp(void *rdma_cxt,
		     struct ecore_rdma_qp *qp,
		     struct ecore_rdma_modify_qp_in_params *params);

struct ecore_rdma_device*
ecore_rdma_query_device(void *rdma_cxt);

struct ecore_rdma_port*
ecore_rdma_query_port(void *rdma_cxt);

enum _ecore_status_t
ecore_rdma_query_qp(void *rdma_cxt,
		    struct ecore_rdma_qp		  *qp,
		    struct ecore_rdma_query_qp_out_params *out_params);

enum _ecore_status_t
ecore_rdma_register_tid(void *rdma_cxt,
			struct ecore_rdma_register_tid_in_params *params);

void ecore_rdma_remove_user(void *rdma_cxt,
			    u16		dpi);

enum _ecore_status_t
ecore_rdma_resize_cnq(void *rdma_cxt,
		      struct ecore_rdma_resize_cnq_in_params *in_params);

/*Returns the CQ CID or zero in case of failure */
enum _ecore_status_t
ecore_rdma_resize_cq(void *rdma_cxt,
		     struct ecore_rdma_resize_cq_in_params  *in_params,
		     struct ecore_rdma_resize_cq_out_params *out_params);

/* Before calling rdma_start upper layer (VBD/qed) should fill the
 * page-size and mtu in hwfn context
 */
enum _ecore_status_t
ecore_rdma_start(void *p_hwfn,
		 struct ecore_rdma_start_in_params *params);

enum _ecore_status_t
ecore_rdma_stop(void *rdma_cxt);

enum _ecore_status_t
ecore_rdma_query_stats(void *rdma_cxt, u8 stats_queue,
		       struct ecore_rdma_stats_out_params *out_parms);

enum _ecore_status_t
ecore_rdma_query_counters(void *rdma_cxt,
			  struct ecore_rdma_counters_out_params *out_parms);

u32 ecore_rdma_get_sb_id(void *p_hwfn, u32 rel_sb_id);

u32 ecore_rdma_query_cau_timer_res(void *p_hwfn);

void ecore_rdma_cnq_prod_update(void *rdma_cxt, u8 cnq_index, u16 prod);

void ecore_rdma_resc_free(struct ecore_hwfn *p_hwfn);

#ifdef CONFIG_ECORE_IWARP

/* iWARP API */


enum ecore_iwarp_event_type {
	ECORE_IWARP_EVENT_MPA_REQUEST, /* Passive side request received */
	ECORE_IWARP_EVENT_PASSIVE_COMPLETE, /* Passive side established
					     * ( ack on mpa response )
					     */
	ECORE_IWARP_EVENT_ACTIVE_COMPLETE, /* Active side reply received */
	ECORE_IWARP_EVENT_DISCONNECT,
	ECORE_IWARP_EVENT_CLOSE,
	ECORE_IWARP_EVENT_IRQ_FULL,
	ECORE_IWARP_EVENT_RQ_EMPTY,
	ECORE_IWARP_EVENT_LLP_TIMEOUT,
	ECORE_IWARP_EVENT_REMOTE_PROTECTION_ERROR,
	ECORE_IWARP_EVENT_CQ_OVERFLOW,
	ECORE_IWARP_EVENT_QP_CATASTROPHIC,
	ECORE_IWARP_EVENT_ACTIVE_MPA_REPLY,
	ECORE_IWARP_EVENT_LOCAL_ACCESS_ERROR,
	ECORE_IWARP_EVENT_REMOTE_OPERATION_ERROR,
	ECORE_IWARP_EVENT_TERMINATE_RECEIVED
};

enum ecore_tcp_ip_version
{
	ECORE_TCP_IPV4,
	ECORE_TCP_IPV6,
};

struct ecore_iwarp_cm_info {
	enum ecore_tcp_ip_version ip_version;
	u32 remote_ip[4];
	u32 local_ip[4];
	u16 remote_port;
	u16 local_port;
	u16 vlan;
	const void *private_data;
	u16 private_data_len;
	u8 ord;
	u8 ird;
};

struct ecore_iwarp_cm_event_params {
	enum ecore_iwarp_event_type event;
	const struct ecore_iwarp_cm_info *cm_info;
	void *ep_context; /* To be passed to accept call */
	int status;
};

typedef int (*iwarp_event_handler)(void *context,
				   struct ecore_iwarp_cm_event_params *event);

/* Active Side Connect Flow:
 * upper layer driver calls ecore_iwarp_connect
 * Function is blocking: i.e. returns after tcp connection is established
 * After MPA connection is established ECORE_IWARP_EVENT_ACTIVE_COMPLETE event
 * will be passed to upperlayer driver using the event_cb passed in
 * ecore_iwarp_connect_in. Information of the established connection will be
 * initialized in event data.
 */
struct ecore_iwarp_connect_in {
	iwarp_event_handler event_cb;
	void *cb_context;
	struct ecore_rdma_qp *qp;
	struct ecore_iwarp_cm_info cm_info;
	u16 mss;
	u8 remote_mac_addr[6];
	u8 local_mac_addr[6];
};

struct ecore_iwarp_connect_out {
	void *ep_context;
};

/* Passive side connect flow:
 * upper layer driver calls ecore_iwarp_create_listen
 * once Syn packet that matches a ip/port that is listened on arrives, ecore
 * will offload the tcp connection. After MPA Request is received on the
 * offload connection, the event ECORE_IWARP_EVENT_MPA_REQUEST will be sent
 * to upper layer driver using the event_cb passed below. The event data
 * will be placed in event parameter. After upper layer driver processes the
 * event, ecore_iwarp_accept or ecore_iwarp_reject should be called to continue
 * MPA negotiation. Once negotiation is complete the event
 * ECORE_IWARP_EVENT_PASSIVE_COMPLETE will be passed to the event_cb passed
 * originally in ecore_iwarp_listen_in structure.
 */
struct ecore_iwarp_listen_in {
	iwarp_event_handler event_cb; /* Callback func for delivering events */
	void *cb_context; /* passed to event_cb */
	u32 max_backlog; /* Max num of pending incoming connection requests */
	enum ecore_tcp_ip_version ip_version;
	u32 ip_addr[4];
	u16 port;
	u16 vlan;
};

struct ecore_iwarp_listen_out {
	void *handle; /* to be sent to destroy */
};

struct ecore_iwarp_accept_in {
	void *ep_context; /* From event data of ECORE_IWARP_EVENT_MPA_REQUEST */
	void *cb_context; /* context to be passed to event_cb */
	struct ecore_rdma_qp *qp;
	const void *private_data;
	u16 private_data_len;
	u8 ord;
	u8 ird;
};

struct ecore_iwarp_reject_in {
	void *ep_context; /* From event data of ECORE_IWARP_EVENT_MPA_REQUEST */
	void *cb_context; /* context to be passed to event_cb */
	const void *private_data;
	u16 private_data_len;
};

struct ecore_iwarp_send_rtr_in {
	void *ep_context;
};

struct ecore_iwarp_tcp_abort_in {
	void *ep_context;
};


enum _ecore_status_t
ecore_iwarp_connect(void *rdma_cxt,
		    struct ecore_iwarp_connect_in *iparams,
		    struct ecore_iwarp_connect_out *oparams);

enum _ecore_status_t
ecore_iwarp_create_listen(void *rdma_cxt,
			  struct ecore_iwarp_listen_in *iparams,
			  struct ecore_iwarp_listen_out *oparams);

enum _ecore_status_t
ecore_iwarp_accept(void *rdma_cxt,
		   struct ecore_iwarp_accept_in *iparams);

enum _ecore_status_t
ecore_iwarp_reject(void *rdma_cxt,
		   struct ecore_iwarp_reject_in *iparams);

enum _ecore_status_t
ecore_iwarp_destroy_listen(void *rdma_cxt, void *handle);

enum _ecore_status_t
ecore_iwarp_send_rtr(void *rdma_cxt, struct ecore_iwarp_send_rtr_in *iparams);

enum _ecore_status_t
ecore_iwarp_tcp_abort(void *rdma_cxt, struct ecore_iwarp_tcp_abort_in *iparams);

#endif /* CONFIG_ECORE_IWARP */

#endif
