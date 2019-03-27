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



/*
 * File: qlnx_def.h
 * Author : David C Somayajulu, Cavium Inc., San Jose, CA 95131.
 */

#ifndef _QLNX_DEF_H_
#define _QLNX_DEF_H_

#define VER_SIZE 16

struct qlnx_ivec {
        uint32_t                rss_idx;
        void                    *ha;
        struct resource         *irq;
        void                    *handle;
        int                     irq_rid;
};

typedef struct qlnx_ivec qlnx_ivec_t;

//#define QLNX_MAX_RSS		30
#define QLNX_MAX_VF_RSS		4
#define QLNX_MAX_RSS		36
#define QLNX_DEFAULT_RSS	16
#define QLNX_MAX_TC		1

enum QLNX_STATE {
        QLNX_STATE_CLOSED,
        QLNX_STATE_OPEN,
};

#define HILO_U64(hi, lo)                ((((u64)(hi)) << 32) + (lo))

#define MAX_NUM_TC      8
#define MAX_NUM_PRI     8

#ifndef BITS_PER_BYTE
#define BITS_PER_BYTE	8
#endif /* #ifndef BITS_PER_BYTE */


/* 
 * RX ring buffer contains pointer to kmalloc() data only,
 */
struct sw_rx_data {
        void		*data;
	bus_dmamap_t	map;
	dma_addr_t	dma_addr;
};

enum qlnx_agg_state {
        QLNX_AGG_STATE_NONE  = 0,
        QLNX_AGG_STATE_START = 1,
        QLNX_AGG_STATE_ERROR = 2
};

struct qlnx_agg_info {
        /* rx_buf is a data buffer that can be placed /consumed from rx bd
         * chain. It has two purposes: We will preallocate the data buffer
         * for each aggregation when we open the interface and will place this
         * buffer on the rx-bd-ring when we receive TPA_START. We don't want
         * to be in a state where allocation fails, as we can't reuse the
         * consumer buffer in the rx-chain since FW may still be writing to it
         * (since header needs to be modified for TPA.
         * The second purpose is to keep a pointer to the bd buffer during
         * aggregation.
         */
        struct sw_rx_data       rx_buf;
        enum qlnx_agg_state     agg_state;
	uint16_t		placement_offset;
        struct mbuf             *mpf; /* first mbuf in chain */
        struct mbuf             *mpl; /* last mbuf in chain */
};

#define RX_RING_SIZE_POW        13
#define RX_RING_SIZE            (1 << RX_RING_SIZE_POW)

#define TX_RING_SIZE_POW        14
#define TX_RING_SIZE            (1 << TX_RING_SIZE_POW)

struct qlnx_rx_queue {
        volatile __le16         *hw_cons_ptr;
        struct sw_rx_data       sw_rx_ring[RX_RING_SIZE];
        uint16_t		sw_rx_cons;
        uint16_t		sw_rx_prod;
        struct ecore_chain      rx_bd_ring;
        struct ecore_chain      rx_comp_ring;
        void __iomem            *hw_rxq_prod_addr;
	void 			*handle;

        /* LRO */
        struct qlnx_agg_info    tpa_info[ETH_TPA_MAX_AGGS_NUM];

        uint32_t		rx_buf_size;

        uint16_t		num_rx_buffers;
        uint16_t		rxq_id;


#ifdef QLNX_SOFT_LRO
	struct lro_ctrl		lro;
#endif
};


union db_prod {
        struct eth_db_data	data;
        uint32_t		raw;
};

struct sw_tx_bd {
        struct mbuf		*mp;
	bus_dmamap_t		map;
        uint8_t			flags;
	int			nsegs;

/* Set on the first BD descriptor when there is a split BD */
#define QLNX_TSO_SPLIT_BD               (1<<0)
};

#define QLNX_MAX_SEGMENTS		255
struct qlnx_tx_queue {

        int                     index; /* Queue index */
        volatile __le16         *hw_cons_ptr;
        struct sw_tx_bd         sw_tx_ring[TX_RING_SIZE];
        uint16_t		sw_tx_cons;
        uint16_t		sw_tx_prod;
        struct ecore_chain	tx_pbl;
        void __iomem            *doorbell_addr;
	void 			*handle;
        union db_prod           tx_db;

	bus_dma_segment_t	segs[QLNX_MAX_SEGMENTS];

        uint16_t		num_tx_buffers;
};

#define BD_UNMAP_ADDR(bd)	HILO_U64(le32toh((bd)->addr.hi), \
					le32toh((bd)->addr.lo))
#define BD_UNMAP_LEN(bd)	(le16toh((bd)->nbytes))

#define BD_SET_UNMAP_ADDR_LEN(bd, maddr, len) \
        do { \
                (bd)->addr.hi = htole32(U64_HI(maddr)); \
                (bd)->addr.lo = htole32(U64_LO(maddr)); \
                (bd)->nbytes = htole16(len); \
        } while (0);


#define QLNX_FP_MAX_SEGS	24

struct qlnx_fastpath {
        void			*edev;
        uint8_t			rss_id;
        struct ecore_sb_info    *sb_info;
        struct qlnx_rx_queue    *rxq;
        struct qlnx_tx_queue    *txq[MAX_NUM_TC];
	char			name[64];

	struct mtx		tx_mtx;
	char			tx_mtx_name[32];
	struct buf_ring		*tx_br;
	uint32_t		tx_ring_full;

	struct task		fp_task;
	struct taskqueue	*fp_taskqueue;

	/* transmit statistics */
	uint64_t		tx_pkts_processed;
	uint64_t		tx_pkts_freed;
	uint64_t		tx_pkts_transmitted;
	uint64_t		tx_pkts_completed;
	uint64_t		tx_tso_pkts;
	uint64_t		tx_non_tso_pkts;

#ifdef QLNX_TRACE_PERF_DATA
	uint64_t		tx_pkts_trans_ctx;
	uint64_t		tx_pkts_compl_ctx;
	uint64_t		tx_pkts_trans_fp;
	uint64_t		tx_pkts_compl_fp;
	uint64_t		tx_pkts_compl_intr;
#endif

	uint64_t		tx_lso_wnd_min_len;
	uint64_t		tx_defrag;
	uint64_t		tx_nsegs_gt_elem_left;
	uint32_t		tx_tso_max_nsegs;
	uint32_t		tx_tso_min_nsegs;
	uint32_t		tx_tso_max_pkt_len;
	uint32_t		tx_tso_min_pkt_len;
	uint64_t		tx_pkts[QLNX_FP_MAX_SEGS];

#ifdef QLNX_TRACE_PERF_DATA
	uint64_t		tx_pkts_hist[QLNX_FP_MAX_SEGS];
	uint64_t		tx_comInt[QLNX_FP_MAX_SEGS];
	uint64_t		tx_pkts_q[QLNX_FP_MAX_SEGS];
#endif

	uint64_t		err_tx_nsegs_gt_elem_left;
        uint64_t                err_tx_dmamap_create;
        uint64_t                err_tx_defrag_dmamap_load;
        uint64_t                err_tx_non_tso_max_seg;
        uint64_t                err_tx_dmamap_load;
        uint64_t                err_tx_defrag;
        uint64_t                err_tx_free_pkt_null;
        uint64_t                err_tx_cons_idx_conflict;

        uint64_t                lro_cnt_64;
        uint64_t                lro_cnt_128;
        uint64_t                lro_cnt_256;
        uint64_t                lro_cnt_512;
        uint64_t                lro_cnt_1024;

	/* receive statistics */
	uint64_t		rx_pkts;
	uint64_t		tpa_start;
	uint64_t		tpa_cont;
	uint64_t		tpa_end;
        uint64_t                err_m_getcl;
        uint64_t                err_m_getjcl;
        uint64_t		err_rx_hw_errors;
        uint64_t		err_rx_alloc_errors;
	uint64_t		err_rx_jumbo_chain_pkts;
	uint64_t		err_rx_mp_null;
	uint64_t		err_rx_tpa_invalid_agg_num;
};

struct qlnx_update_vport_params {
        uint8_t			vport_id;
        uint8_t			update_vport_active_rx_flg;
        uint8_t			vport_active_rx_flg;
        uint8_t			update_vport_active_tx_flg;
        uint8_t			vport_active_tx_flg;
        uint8_t			update_inner_vlan_removal_flg;
        uint8_t			inner_vlan_removal_flg;
        struct ecore_rss_params	*rss_params;
	struct ecore_sge_tpa_params *sge_tpa_params;
};

/*
 * link related
 */
struct qlnx_link_output {
	bool		link_up;
	uint32_t	supported_caps;
	uint32_t	advertised_caps;
	uint32_t	link_partner_caps;
	uint32_t	speed; /* In Mb/s */
	bool		autoneg;
	uint32_t	media_type;
	uint32_t	duplex;
};
typedef struct qlnx_link_output qlnx_link_output_t;

#define QLNX_LINK_DUPLEX			0x0001

#define QLNX_LINK_CAP_FIBRE			0x0001
#define QLNX_LINK_CAP_Autoneg			0x0002
#define QLNX_LINK_CAP_Pause			0x0004
#define QLNX_LINK_CAP_Asym_Pause		0x0008
#define QLNX_LINK_CAP_1000baseT_Half		0x0010
#define QLNX_LINK_CAP_1000baseT_Full		0x0020
#define QLNX_LINK_CAP_10000baseKR_Full		0x0040
#define QLNX_LINK_CAP_25000baseKR_Full		0x0080
#define QLNX_LINK_CAP_40000baseLR4_Full		0x0100
#define QLNX_LINK_CAP_50000baseKR2_Full		0x0200
#define QLNX_LINK_CAP_100000baseKR4_Full	0x0400


/* Functions definition */

#define XMIT_PLAIN              0
#define XMIT_L4_CSUM            (1 << 0)
#define XMIT_LSO                (1 << 1)

#define CQE_FLAGS_ERR   (PARSING_AND_ERR_FLAGS_IPHDRERROR_MASK <<       \
                         PARSING_AND_ERR_FLAGS_IPHDRERROR_SHIFT |       \
                         PARSING_AND_ERR_FLAGS_L4CHKSMERROR_MASK <<     \
                         PARSING_AND_ERR_FLAGS_L4CHKSMERROR_SHIFT |     \
                         PARSING_AND_ERR_FLAGS_TUNNELIPHDRERROR_MASK << \
                         PARSING_AND_ERR_FLAGS_TUNNELIPHDRERROR_SHIFT | \
                         PARSING_AND_ERR_FLAGS_TUNNELL4CHKSMERROR_MASK << \
                         PARSING_AND_ERR_FLAGS_TUNNELL4CHKSMERROR_SHIFT)

#define RX_COPY_THRESH          92
#define ETH_MAX_PACKET_SIZE     1500

#define QLNX_MFW_VERSION_LENGTH 32
#define QLNX_STORMFW_VERSION_LENGTH 32

#define QLNX_TX_ELEM_RESERVE		2
#define QLNX_TX_ELEM_THRESH		128
#define QLNX_TX_ELEM_MAX_THRESH		512
#define QLNX_TX_ELEM_MIN_THRESH		32
#define QLNX_TX_COMPL_THRESH		32


#define QLNX_TPA_MAX_AGG_BUFFERS             (20)

#define QLNX_MAX_NUM_MULTICAST_ADDRS	ECORE_MAX_MC_ADDRS
typedef struct _qlnx_mcast {
        uint16_t        rsrvd;
        uint8_t         addr[6];
} __packed qlnx_mcast_t;

typedef struct _qlnx_vf_attr {
	uint8_t		mac_addr[ETHER_ADDR_LEN];
	uint32_t	num_rings;
} qlnx_vf_attr_t;

typedef struct _qlnx_sriov_task {

	struct task		pf_task;
	struct taskqueue	*pf_taskqueue;

#define QLNX_SRIOV_TASK_FLAGS_VF_PF_MSG		0x01
#define QLNX_SRIOV_TASK_FLAGS_VF_FLR_UPDATE	0x02
#define QLNX_SRIOV_TASK_FLAGS_BULLETIN_UPDATE	0x04
	volatile uint32_t	flags;

} qlnx_sriov_task_t;


/*
 * Adapter structure contains the hardware independent information of the
 * pci function.
 */
struct qlnx_host {

	/* interface to ecore */

	struct ecore_dev	cdev;

	uint32_t		state;

	/* some flags */
        volatile struct {
                volatile uint32_t
			hw_init			:1,
			callout_init		:1,
                        slowpath_start		:1,
                        parent_tag		:1,
                        lock_init		:1;
        } flags;

	/* interface to o.s */

	device_t		pci_dev;
	uint8_t			pci_func;
	uint8_t			dev_unit;
	uint16_t		device_id;

	struct ifnet		*ifp;
	int			if_flags;
	volatile int		link_up;
	struct ifmedia		media;
	uint16_t		max_frame_size;

	struct cdev		*ioctl_dev;

	/* resources */
        struct resource         *pci_reg;
        int                     reg_rid;

        struct resource         *pci_dbells;
        int                     dbells_rid;
	uint64_t		dbells_phys_addr;
	uint32_t		dbells_size;

        struct resource         *msix_bar;
        int                     msix_rid;

	int			msix_count;

	struct mtx		hw_lock;

	/* debug */

	uint32_t                dbg_level;
	uint32_t                dbg_trace_lro_cnt;
	uint32_t                dbg_trace_tso_pkt_len;
	uint32_t                dp_level;
	uint32_t                dp_module;

	/* misc */
	uint8_t 		mfw_ver[QLNX_MFW_VERSION_LENGTH];
	uint8_t 		stormfw_ver[QLNX_STORMFW_VERSION_LENGTH];
	uint32_t		flash_size;

	/* dma related */

	bus_dma_tag_t		parent_tag;
	bus_dma_tag_t		tx_tag;
	bus_dma_tag_t		rx_tag;

	
        struct ecore_sb_info    sb_array[QLNX_MAX_RSS];
        struct qlnx_rx_queue    rxq_array[QLNX_MAX_RSS];
        struct qlnx_tx_queue    txq_array[(QLNX_MAX_RSS * MAX_NUM_TC)];
        struct qlnx_fastpath    fp_array[QLNX_MAX_RSS];

	/* tx related */
	struct callout		tx_callout;
	uint32_t		txr_idx;

	/* rx related */
	uint32_t		rx_pkt_threshold;
	uint32_t		rx_jumbo_buf_eq_mtu;

	/* slow path related */
        struct resource         *sp_irq[MAX_HWFNS_PER_DEVICE];
        void                    *sp_handle[MAX_HWFNS_PER_DEVICE];
        int                     sp_irq_rid[MAX_HWFNS_PER_DEVICE];
	struct task		sp_task[MAX_HWFNS_PER_DEVICE];
	struct taskqueue	*sp_taskqueue[MAX_HWFNS_PER_DEVICE];

	struct callout          qlnx_callout;

	/* fast path related */
	int			num_rss;
	int			num_tc;

#define QLNX_MAX_TSS_CNT(ha)	((ha->num_rss) * (ha->num_tc))

	qlnx_ivec_t              irq_vec[QLNX_MAX_RSS];
	

	uint8_t			filter;
	uint32_t                nmcast;
	qlnx_mcast_t            mcast[QLNX_MAX_NUM_MULTICAST_ADDRS];
	struct ecore_filter_mcast ecore_mcast;
	uint8_t			primary_mac[ETH_ALEN];
	uint8_t			prio_to_tc[MAX_NUM_PRI];
	struct ecore_eth_stats	hw_stats;
	struct ecore_rss_params	rss_params;
        uint32_t		rx_buf_size;
        bool			rx_csum_offload;
	
	uint32_t		rx_coalesce_usecs;
	uint32_t		tx_coalesce_usecs;

	/* link related */
	qlnx_link_output_t	if_link;

	/* global counters */
	uint64_t		sp_interrupts;
	uint64_t		err_illegal_intr;
	uint64_t		err_fp_null;
	uint64_t		err_get_proto_invalid_type;
	
	/* error recovery related */
	uint32_t		error_recovery;
	struct task		err_task;
	struct taskqueue	*err_taskqueue;

	/* grcdump related */
	uint32_t		err_inject;
	uint32_t		grcdump_taken;
	uint32_t		grcdump_dwords[QLNX_MAX_HW_FUNCS];
	uint32_t		grcdump_size[QLNX_MAX_HW_FUNCS];
	void			*grcdump[QLNX_MAX_HW_FUNCS];

	uint32_t		idle_chk_taken;
	uint32_t		idle_chk_dwords[QLNX_MAX_HW_FUNCS];
	uint32_t		idle_chk_size[QLNX_MAX_HW_FUNCS];
	void			*idle_chk[QLNX_MAX_HW_FUNCS];

	/* storm stats related */
#define QLNX_STORM_STATS_TOTAL \
		(QLNX_MAX_HW_FUNCS * QLNX_STORM_STATS_SAMPLES_PER_HWFN)
	qlnx_storm_stats_t	storm_stats[QLNX_STORM_STATS_TOTAL];
	uint32_t		storm_stats_index;
	uint32_t		storm_stats_enable;
	uint32_t		storm_stats_gather;

	uint32_t		personality;

	uint16_t		sriov_initialized;
	uint16_t		num_vfs;
	qlnx_vf_attr_t		*vf_attr;
	qlnx_sriov_task_t	sriov_task[MAX_HWFNS_PER_DEVICE];
	uint32_t		curr_vf;

	void			*next;
	void			*qlnx_rdma;
	volatile int		qlnxr_debug;
};

typedef struct qlnx_host qlnx_host_t;

/* note that align has to be a power of 2 */
#define QL_ALIGN(size, align) (((size) + ((align) - 1)) & (~((align) - 1)));
#define QL_MIN(x, y) ((x < y) ? x : y)

#define QL_RUNNING(ifp) \
		((ifp->if_drv_flags & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) == \
			IFF_DRV_RUNNING)

#define QLNX_MAX_MTU			9000
#define QLNX_MAX_SEGMENTS_NON_TSO	(ETH_TX_MAX_BDS_PER_NON_LSO_PACKET - 1)
//#define QLNX_MAX_TSO_FRAME_SIZE		((64 * 1024 - 1) + 22)
#define QLNX_MAX_TSO_FRAME_SIZE		65536
#define QLNX_MAX_TX_MBUF_SIZE		65536    /* bytes - bd_len = 16bits */


#define QL_MAC_CMP(mac1, mac2)    \
        ((((*(uint32_t *) mac1) == (*(uint32_t *) mac2) && \
        (*(uint16_t *)(mac1 + 4)) == (*(uint16_t *)(mac2 + 4)))) ? 0 : 1)
#define for_each_rss(i) for (i = 0; i < ha->num_rss; i++)

/*
 * Debug Related
 */

#ifdef QLNX_DEBUG

#define QL_DPRINT1(ha, x, ...) 					\
	do { 							\
		if ((ha)->dbg_level & 0x0001) {			\
			device_printf ((ha)->pci_dev,		\
				"[%s:%d]" x,			\
				__func__, __LINE__,		\
				## __VA_ARGS__);		\
		}						\
	} while (0)

#define QL_DPRINT2(ha, x, ...)					\
	do { 							\
		if ((ha)->dbg_level & 0x0002) {			\
			device_printf ((ha)->pci_dev,		\
				"[%s:%d]" x,			\
				__func__, __LINE__,		\
				## __VA_ARGS__);		\
		}						\
	} while (0)

#define QL_DPRINT3(ha, x, ...)					\
	do { 							\
		if ((ha)->dbg_level & 0x0004) {			\
			device_printf ((ha)->pci_dev,		\
				"[%s:%d]" x,			\
				__func__, __LINE__,		\
				## __VA_ARGS__);		\
		}						\
	} while (0)

#define QL_DPRINT4(ha, x, ...)					\
	do { 							\
		if ((ha)->dbg_level & 0x0008) {			\
			device_printf ((ha)->pci_dev,		\
				"[%s:%d]" x,			\
				__func__, __LINE__,		\
				## __VA_ARGS__);		\
		}						\
	} while (0)

#define QL_DPRINT5(ha, x, ...)					\
	do { 							\
		if ((ha)->dbg_level & 0x0010) {			\
			device_printf ((ha)->pci_dev,		\
				"[%s:%d]" x,			\
				__func__, __LINE__,		\
				## __VA_ARGS__);		\
		}						\
	} while (0)

#define QL_DPRINT6(ha, x, ...)					\
	do { 							\
		if ((ha)->dbg_level & 0x0020) {			\
			device_printf ((ha)->pci_dev,		\
				"[%s:%d]" x,			\
				__func__, __LINE__,		\
				## __VA_ARGS__);		\
		}						\
	} while (0)

#define QL_DPRINT7(ha, x, ...)					\
	do { 							\
		if ((ha)->dbg_level & 0x0040) {			\
			device_printf ((ha)->pci_dev,		\
				"[%s:%d]" x,			\
				__func__, __LINE__,		\
				## __VA_ARGS__);		\
		}						\
	} while (0)

#define QL_DPRINT8(ha, x, ...)					\
	do { 							\
		if ((ha)->dbg_level & 0x0080) {			\
			device_printf ((ha)->pci_dev,		\
				"[%s:%d]" x,			\
				__func__, __LINE__,		\
				## __VA_ARGS__);		\
		}						\
	} while (0)

#define QL_DPRINT9(ha, x, ...)					\
	do { 							\
		if ((ha)->dbg_level & 0x0100) {			\
			device_printf ((ha)->pci_dev,		\
				"[%s:%d]" x,			\
				__func__, __LINE__,		\
				## __VA_ARGS__);		\
		}						\
	} while (0)

#define QL_DPRINT11(ha, x, ...)					\
	do { 							\
		if ((ha)->dbg_level & 0x0400) {			\
			device_printf ((ha)->pci_dev,		\
				"[%s:%d]" x,			\
				__func__, __LINE__,		\
				## __VA_ARGS__);		\
		}						\
	} while (0)

#define QL_DPRINT12(ha, x, ...)					\
	do { 							\
		if ((ha)->dbg_level & 0x0800) {			\
			device_printf ((ha)->pci_dev,		\
				"[%s:%d]" x,			\
				__func__, __LINE__,		\
				## __VA_ARGS__);		\
		}						\
	} while (0)

#define QL_DPRINT13(ha, x, ...)					\
	do { 							\
		if ((ha)->dbg_level & 0x1000) {			\
			device_printf ((ha)->pci_dev,		\
				"[%s:%d]" x,			\
				__func__, __LINE__,		\
				## __VA_ARGS__);		\
		}						\
	} while (0)


#else

#define QL_DPRINT1(ha, x, ...)
#define QL_DPRINT2(ha, x, ...)
#define QL_DPRINT3(ha, x, ...)
#define QL_DPRINT4(ha, x, ...)
#define QL_DPRINT5(ha, x, ...)
#define QL_DPRINT6(ha, x, ...)
#define QL_DPRINT7(ha, x, ...)
#define QL_DPRINT8(ha, x, ...)
#define QL_DPRINT9(ha, x, ...)
#define QL_DPRINT11(ha, x, ...)
#define QL_DPRINT12(ha, x, ...)
#define QL_DPRINT13(ha, x, ...)

#endif /* #ifdef QLNX_DEBUG */

#define QL_ASSERT(ha, x, y)     if (!x) panic y

#define QL_ERR_INJECT(ha, val)		(ha->err_inject == val)
#define QL_RESET_ERR_INJECT(ha, val)	{if (ha->err_inject == val) ha->err_inject = 0;}
#define QL_ERR_INJCT_TX_INT_DIFF	0x0001
#define QL_ERR_INJCT_TX_INT_MBUF_NULL	0x0002


/*
 * exported functions
 */
extern int qlnx_make_cdev(qlnx_host_t *ha);
extern void qlnx_del_cdev(qlnx_host_t *ha);
extern int qlnx_grc_dump(qlnx_host_t *ha, uint32_t *num_dumped_dwords,
		int hwfn_index);
extern int qlnx_idle_chk(qlnx_host_t *ha, uint32_t *num_dumped_dwords,
		int hwfn_index);
extern uint8_t *qlnx_get_mac_addr(qlnx_host_t *ha);
extern void qlnx_fill_link(qlnx_host_t *ha, struct ecore_hwfn *hwfn,
                          struct qlnx_link_output *if_link);
extern int qlnx_set_lldp_tlvx(qlnx_host_t *ha, qlnx_lldp_sys_tlvs_t *lldp_tlvs);
extern int qlnx_vf_device(qlnx_host_t *ha);
extern void qlnx_free_mem_sb(qlnx_host_t *ha, struct ecore_sb_info *sb_info);
extern int qlnx_alloc_mem_sb(qlnx_host_t *ha, struct ecore_sb_info *sb_info,
		u16 sb_id);


/*
 * Some OS specific stuff
 */

#if (defined IFM_100G_SR4)
#define QLNX_IFM_100G_SR4 IFM_100G_SR4
#define QLNX_IFM_100G_LR4 IFM_100G_LR4
#define QLNX_IFM_100G_CR4 IFM_100G_CR4
#else
#define QLNX_IFM_100G_SR4 IFM_UNKNOWN
#define QLNX_IFM_100G_LR4 IFM_UNKNOWN
#endif /* #if (defined IFM_100G_SR4) */

#if (defined IFM_25G_SR)
#define QLNX_IFM_25G_SR IFM_25G_SR
#define QLNX_IFM_25G_CR IFM_25G_CR
#else
#define QLNX_IFM_25G_SR IFM_UNKNOWN
#define QLNX_IFM_25G_CR IFM_UNKNOWN
#endif /* #if (defined IFM_25G_SR) */


#if __FreeBSD_version < 1100000

#define QLNX_INC_IERRORS(ifp)		ifp->if_ierrors++
#define QLNX_INC_IQDROPS(ifp)		ifp->if_iqdrops++
#define QLNX_INC_IPACKETS(ifp)		ifp->if_ipackets++
#define QLNX_INC_OPACKETS(ifp)		ifp->if_opackets++
#define QLNX_INC_OBYTES(ifp, len)	ifp->if_obytes += len
#define QLNX_INC_IBYTES(ifp, len)	ifp->if_ibytes += len

#else

#define QLNX_INC_IERRORS(ifp)	if_inc_counter(ifp, IFCOUNTER_IERRORS, 1)
#define QLNX_INC_IQDROPS(ifp)	if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1)
#define QLNX_INC_IPACKETS(ifp)	if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1)
#define QLNX_INC_OPACKETS(ifp)	if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1)

#define QLNX_INC_OBYTES(ifp, len)	\
			if_inc_counter(ifp, IFCOUNTER_OBYTES, len)
#define QLNX_INC_IBYTES(ifp, len)	\
			if_inc_counter(ha->ifp, IFCOUNTER_IBYTES, len)

#endif /* #if __FreeBSD_version < 1100000 */

#define CQE_L3_PACKET(flags)    \
        ((((flags) & PARSING_AND_ERR_FLAGS_L3TYPE_MASK) == e_l3_type_ipv4) || \
        (((flags) & PARSING_AND_ERR_FLAGS_L3TYPE_MASK) == e_l3_type_ipv6))

#define CQE_IP_HDR_ERR(flags) \
        ((flags) & (PARSING_AND_ERR_FLAGS_IPHDRERROR_MASK \
                << PARSING_AND_ERR_FLAGS_IPHDRERROR_SHIFT))

#define CQE_L4_HAS_CSUM(flags) \
        ((flags) & (PARSING_AND_ERR_FLAGS_L4CHKSMWASCALCULATED_MASK \
                << PARSING_AND_ERR_FLAGS_L4CHKSMWASCALCULATED_SHIFT))

#define CQE_HAS_VLAN(flags) \
        ((flags) & (PARSING_AND_ERR_FLAGS_TAG8021QEXIST_MASK \
                << PARSING_AND_ERR_FLAGS_TAG8021QEXIST_SHIFT))

#ifndef QLNX_RDMA
#if defined(__i386__) || defined(__amd64__)

static __inline
void prefetch(void *x)
{
        __asm volatile("prefetcht0 %0" :: "m" (*(unsigned long *)x));
}

#else
#define prefetch(x)
#endif
#endif


#endif /* #ifndef _QLNX_DEF_H_ */
