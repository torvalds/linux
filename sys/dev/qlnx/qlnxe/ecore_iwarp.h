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

#ifndef __ECORE_IWARP_H__
#define __ECORE_IWARP_H__

enum ecore_iwarp_qp_state {
	ECORE_IWARP_QP_STATE_IDLE,
	ECORE_IWARP_QP_STATE_RTS,
	ECORE_IWARP_QP_STATE_TERMINATE,
	ECORE_IWARP_QP_STATE_CLOSING,
	ECORE_IWARP_QP_STATE_ERROR,
};

enum ecore_iwarp_listener_state {
	ECORE_IWARP_LISTENER_STATE_ACTIVE,
	ECORE_IWARP_LISTENER_STATE_UNPAUSE,
	ECORE_IWARP_LISTENER_STATE_PAUSE,
	ECORE_IWARP_LISTENER_STATE_DESTROYING,
};

enum ecore_iwarp_qp_state
ecore_roce2iwarp_state(enum ecore_roce_qp_state state);

#ifdef CONFIG_ECORE_IWARP

#define ECORE_IWARP_PREALLOC_CNT	ECORE_IWARP_MAX_LIS_BACKLOG

#define ECORE_IWARP_LL2_SYN_TX_SIZE	(128)
#define ECORE_IWARP_LL2_SYN_RX_SIZE	(256)
#define ECORE_IWARP_MAX_SYN_PKT_SIZE	(128)

#define ECORE_IWARP_LL2_OOO_DEF_TX_SIZE	(256)
#define ECORE_MAX_OOO			(16)
#define ECORE_IWARP_LL2_OOO_MAX_RX_SIZE	(16384)

#define ECORE_IWARP_HANDLE_INVAL	(0xff)

struct ecore_iwarp_ll2_buff {
	struct ecore_iwarp_ll2_buff	*piggy_buf;
	void 				*data;
	dma_addr_t			data_phys_addr;
	u32				buff_size;
};

struct ecore_iwarp_ll2_mpa_buf {
	osal_list_entry_t		list_entry;
	struct ecore_iwarp_ll2_buff	*ll2_buf;
	struct unaligned_opaque_data	data;
	u16				tcp_payload_len;
	u8				placement_offset;
};

/* In some cases a fpdu will arrive with only one byte of the header, in this
 * case the fpdu_length will be partial ( contain only higher byte and
 * incomplete bytes will contain the invalid value
 */
#define ECORE_IWARP_INVALID_INCOMPLETE_BYTES 0xffff

struct ecore_iwarp_fpdu {
	struct ecore_iwarp_ll2_buff 	*mpa_buf;
	dma_addr_t			pkt_hdr;
	u8				pkt_hdr_size;
	dma_addr_t			mpa_frag;
	void				*mpa_frag_virt;
	u16				mpa_frag_len;
	u16				fpdu_length;
	u16				incomplete_bytes;
};

struct ecore_iwarp_info {
	osal_list_t			listen_list; /* ecore_iwarp_listener */
	osal_list_t			ep_list;     /* ecore_iwarp_ep */
	osal_list_t			ep_free_list;/* pre-allocated ep's */
	osal_list_t			mpa_buf_list;/* list of mpa_bufs */
	osal_list_t			mpa_buf_pending_list;
	osal_spinlock_t			iw_lock;
	osal_spinlock_t			qp_lock; /* for teardown races */
	struct iwarp_rxmit_stats_drv	stats;
	u32				rcv_wnd_scale;
	u16				rcv_wnd_size;
	u16				max_mtu;
	u16				num_ooo_rx_bufs;
	u8				mac_addr[ETH_ALEN];
	u8				crc_needed;
	u8				tcp_flags;
	u8				ll2_syn_handle;
	u8				ll2_ooo_handle;
	u8				ll2_mpa_handle;
	u8				peer2peer;
	u8				_pad;
	enum mpa_negotiation_mode	mpa_rev;
	enum mpa_rtr_type		rtr_type;
	struct ecore_iwarp_fpdu		*partial_fpdus;
	struct ecore_iwarp_ll2_mpa_buf  *mpa_bufs;
	u8				*mpa_intermediate_buf;
	u16				max_num_partial_fpdus;

	/* MPA statistics */
	u64				unalign_rx_comp;
};

enum ecore_iwarp_ep_state {
	ECORE_IWARP_EP_INIT,
	ECORE_IWARP_EP_MPA_REQ_RCVD,
	ECORE_IWARP_EP_MPA_OFFLOADED,
	ECORE_IWARP_EP_ESTABLISHED,
	ECORE_IWARP_EP_CLOSED,
	ECORE_IWARP_EP_ABORTING
};

union async_output {
	struct iwarp_eqe_data_mpa_async_completion mpa_response;
	struct iwarp_eqe_data_tcp_async_completion mpa_request;
};

#define ECORE_MAX_PRIV_DATA_LEN (512)
struct ecore_iwarp_ep_memory {
	u8			in_pdata[ECORE_MAX_PRIV_DATA_LEN];
	u8			out_pdata[ECORE_MAX_PRIV_DATA_LEN];
	union async_output	async_output;
};

/* Endpoint structure represents a TCP connection. This connection can be
 * associated with a QP or not (in which case QP==NULL)
 */
struct ecore_iwarp_ep {
	osal_list_entry_t		list_entry;
	int				sig;
	struct ecore_rdma_qp		*qp;
	enum ecore_iwarp_ep_state	state;

	/* This contains entire buffer required for ep memories. This is the
	 * only one actually allocated and freed. The rest are pointers into
	 * this buffer
	 */
	struct ecore_iwarp_ep_memory    *ep_buffer_virt;
	dma_addr_t			ep_buffer_phys;

	struct ecore_iwarp_cm_info	cm_info;
	struct ecore_iwarp_listener	*listener;
	enum tcp_connect_mode		connect_mode;
	enum mpa_rtr_type		rtr_type;
	enum mpa_negotiation_mode	mpa_rev;
	u32				tcp_cid;
	u32				cid;
	u8				remote_mac_addr[6];
	u8				local_mac_addr[6];
	u16				mss;
	bool				mpa_reply_processed;

	/* The event_cb function is called for asynchrounous events associated
	 * with the ep. It is initialized at different entry points depending
	 * on whether the ep is the tcp connection active side or passive side
	 * The cb_context is passed to the event_cb function.
	 */
	iwarp_event_handler		event_cb;
	void				*cb_context;

	/* For Passive side - syn packet related data */
	struct ecore_iwarp_ll2_buff	*syn;
	u16				syn_ip_payload_length;
	dma_addr_t			syn_phy_addr;
};

struct ecore_iwarp_listener {
	osal_list_entry_t	list_entry;

	/* The event_cb function is called for connection requests.
	 * The cb_context is passed to the event_cb function.
	 */
	iwarp_event_handler	event_cb;
	void			*cb_context;
	osal_list_t		ep_list;
	osal_spinlock_t		lock;
	u32			max_backlog;
	u8			ip_version;
	u32			ip_addr[4];
	u16			port;
	u16			vlan;
	bool			drop;
	bool			done;
	enum			ecore_iwarp_listener_state state;
};

enum _ecore_status_t
ecore_iwarp_alloc(struct ecore_hwfn *p_hwfn);

enum _ecore_status_t
ecore_iwarp_setup(struct ecore_hwfn *p_hwfn,
		  struct ecore_rdma_start_in_params *params);

void
ecore_iwarp_init_fw_ramrod(struct ecore_hwfn *p_hwfn,
			   struct iwarp_init_func_ramrod_data *p_ramrod);

enum _ecore_status_t
ecore_iwarp_stop(struct ecore_hwfn *p_hwfn);

void
ecore_iwarp_resc_free(struct ecore_hwfn *p_hwfn);

void
ecore_iwarp_init_devinfo(struct ecore_hwfn *p_hwfn);

enum _ecore_status_t
ecore_iwarp_init_hw(struct ecore_hwfn *p_hwfn, struct ecore_ptt *p_ptt);

enum _ecore_status_t
ecore_iwarp_create_qp(struct ecore_hwfn *p_hwfn,
		      struct ecore_rdma_qp *qp,
		      struct ecore_rdma_create_qp_out_params *out_params);

enum _ecore_status_t
ecore_iwarp_modify_qp(struct ecore_hwfn *p_hwfn,
		      struct ecore_rdma_qp *qp,
		      enum ecore_iwarp_qp_state new_state,
		      bool internal);

enum _ecore_status_t
ecore_iwarp_destroy_qp(struct ecore_hwfn *p_hwfn,
		       struct ecore_rdma_qp *qp);

enum _ecore_status_t
ecore_iwarp_fw_destroy(struct ecore_hwfn *p_hwfn,
		       struct ecore_rdma_qp *qp);

enum _ecore_status_t
ecore_iwarp_query_qp(struct ecore_rdma_qp *qp,
		     struct ecore_rdma_query_qp_out_params *out_params);

#else

static OSAL_INLINE enum _ecore_status_t
ecore_iwarp_alloc(struct ecore_hwfn OSAL_UNUSED *p_hwfn)
{
	return ECORE_SUCCESS;
}

static OSAL_INLINE enum _ecore_status_t
ecore_iwarp_setup(struct ecore_hwfn OSAL_UNUSED *p_hwfn,
		  struct ecore_rdma_start_in_params OSAL_UNUSED *params)
{
	return ECORE_SUCCESS;
}

static OSAL_INLINE void
ecore_iwarp_init_fw_ramrod(struct ecore_hwfn OSAL_UNUSED *p_hwfn,
			   struct iwarp_init_func_ramrod_data OSAL_UNUSED *p_ramrod)
{
}

static OSAL_INLINE enum _ecore_status_t
ecore_iwarp_stop(struct ecore_hwfn OSAL_UNUSED *p_hwfn)
{
	return ECORE_SUCCESS;
}

static OSAL_INLINE void
ecore_iwarp_resc_free(struct ecore_hwfn OSAL_UNUSED *p_hwfn)
{
}

static OSAL_INLINE void
ecore_iwarp_init_devinfo(struct ecore_hwfn OSAL_UNUSED *p_hwfn)
{
}

static OSAL_INLINE enum _ecore_status_t
ecore_iwarp_init_hw(struct ecore_hwfn OSAL_UNUSED *p_hwfn,
		    struct ecore_ptt OSAL_UNUSED *p_ptt)
{
	return ECORE_SUCCESS;
}

static OSAL_INLINE enum _ecore_status_t
ecore_iwarp_create_qp(struct ecore_hwfn OSAL_UNUSED *p_hwfn,
		      struct ecore_rdma_qp OSAL_UNUSED *qp,
		      struct ecore_rdma_create_qp_out_params OSAL_UNUSED *out_params)
{
	return ECORE_SUCCESS;
}

static OSAL_INLINE enum _ecore_status_t
ecore_iwarp_modify_qp(struct ecore_hwfn OSAL_UNUSED *p_hwfn,
		      struct ecore_rdma_qp OSAL_UNUSED *qp,
		      enum ecore_iwarp_qp_state OSAL_UNUSED new_state,
		      bool OSAL_UNUSED internal)
{
	return ECORE_SUCCESS;
}

static OSAL_INLINE enum _ecore_status_t
ecore_iwarp_destroy_qp(struct ecore_hwfn OSAL_UNUSED *p_hwfn,
		       struct ecore_rdma_qp OSAL_UNUSED *qp)
{
	return ECORE_SUCCESS;
}

static OSAL_INLINE enum _ecore_status_t
ecore_iwarp_fw_destroy(struct ecore_hwfn OSAL_UNUSED *p_hwfn,
		       struct ecore_rdma_qp OSAL_UNUSED *qp)
{
	return ECORE_SUCCESS;
}

static OSAL_INLINE enum _ecore_status_t
ecore_iwarp_query_qp(struct ecore_rdma_qp OSAL_UNUSED *qp,
		     struct ecore_rdma_query_qp_out_params OSAL_UNUSED *out_params)
{
	return ECORE_SUCCESS;
}

#endif
#endif
