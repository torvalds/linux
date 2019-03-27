/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013-2014 Qlogic Corporation
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

/*
 * File: qls_def.h
 * Author : David C Somayajulu, Qlogic Corporation, Aliso Viejo, CA 92656.
 */

#ifndef _QLS_DEF_H_
#define _QLS_DEF_H_

/*
 * structure encapsulating a DMA buffer
 */
struct qla_dma {
        bus_size_t              alignment;
        uint32_t                size;
        void                    *dma_b;
        bus_addr_t              dma_addr;
        bus_dmamap_t            dma_map;
        bus_dma_tag_t           dma_tag;
};
typedef struct qla_dma qla_dma_t;

/*
 * structure encapsulating interrupt vectors
 */
struct qla_ivec {
	uint32_t		cq_idx;
	void			*ha;
	struct resource		*irq;
	void			*handle;
	int			irq_rid;
};
typedef struct qla_ivec qla_ivec_t;

/*
 * Transmit Related Definitions
 */

#define MAX_TX_RINGS		1
#define NUM_TX_DESCRIPTORS	1024

#define QLA_MAX_SEGMENTS	64	/* maximum # of segs in a sg list */
#define QLA_OAL_BLK_SIZE	(sizeof (q81_txb_desc_t) * QLA_MAX_SEGMENTS)

#define QLA_TX_OALB_TOTAL_SIZE	(NUM_TX_DESCRIPTORS * QLA_OAL_BLK_SIZE)

#define QLA_TX_PRIVATE_BSIZE	((QLA_TX_OALB_TOTAL_SIZE + \
					PAGE_SIZE + \
					(PAGE_SIZE - 1)) & ~(PAGE_SIZE - 1))

#define QLA_MAX_MTU		9000
#define QLA_STD_FRAME_SIZE	1514
#define QLA_MAX_TSO_FRAME_SIZE	((64 * 1024 - 1) + 22)

#define QL_FRAME_HDR_SIZE	(ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN +\
                sizeof (struct ip6_hdr) + sizeof (struct tcphdr) + 16)

struct qla_tx_buf {
	struct mbuf	*m_head;
	bus_dmamap_t	map;

	/* The number of entries in the OAL is determined by QLA_MAX_SEGMENTS */
	bus_addr_t      oal_paddr;
	void		*oal_vaddr; 
};
typedef struct qla_tx_buf qla_tx_buf_t;

struct qla_tx_ring {

	volatile struct {
		uint32_t	wq_dma:1,
				privb_dma:1;
	} flags;

	qla_dma_t		privb_dma;
	qla_dma_t		wq_dma;

	qla_tx_buf_t		tx_buf[NUM_TX_DESCRIPTORS];
	uint64_t		count;

	struct resource         *wq_db_addr;
	uint32_t		wq_db_offset;

	q81_tx_cmd_t		*wq_vaddr;
	bus_addr_t		wq_paddr;

	void			*wq_icb_vaddr;
	bus_addr_t		wq_icb_paddr;

	uint32_t		*txr_cons_vaddr;
	bus_addr_t		txr_cons_paddr;
	
	volatile uint32_t	txr_free; /* # of free entries in tx ring */
	volatile uint32_t	txr_next; /* # next available tx ring entry */
	volatile uint32_t	txr_done;

	uint64_t		tx_frames;
	uint64_t		tx_tso_frames;
	uint64_t		tx_vlan_frames;
};
typedef struct qla_tx_ring qla_tx_ring_t;

/*
 * Receive Related Definitions
 */

#define MAX_RX_RINGS		MAX_TX_RINGS

#define NUM_RX_DESCRIPTORS	1024
#define NUM_CQ_ENTRIES		NUM_RX_DESCRIPTORS

#define QLA_LGB_SIZE		(12 * 1024)
#define QLA_NUM_LGB_ENTRIES	32

#define QLA_LBQ_SIZE		(QLA_NUM_LGB_ENTRIES * sizeof(q81_bq_addr_e_t)) 

#define QLA_LGBQ_AND_TABLE_SIZE	\
	((QLA_LBQ_SIZE + PAGE_SIZE + (PAGE_SIZE - 1)) & ~(PAGE_SIZE - 1))


/* Please note that Small Buffer size is determined by max mtu size */
#define QLA_NUM_SMB_ENTRIES	NUM_RX_DESCRIPTORS

#define QLA_SBQ_SIZE		(QLA_NUM_SMB_ENTRIES * sizeof(q81_bq_addr_e_t))

#define QLA_SMBQ_AND_TABLE_SIZE	\
	((QLA_SBQ_SIZE + PAGE_SIZE + (PAGE_SIZE - 1)) & ~(PAGE_SIZE - 1))

struct qla_rx_buf {
	struct mbuf	*m_head;
	bus_dmamap_t	map;
	bus_addr_t      paddr;
	void		*next;
};
typedef struct qla_rx_buf qla_rx_buf_t;

struct qla_rx_ring {
	volatile struct {
		uint32_t	cq_dma:1,
				lbq_dma:1,
				sbq_dma:1,
				lb_dma:1;
	} flags;

	qla_dma_t		cq_dma;
	qla_dma_t		lbq_dma;
	qla_dma_t		sbq_dma;
	qla_dma_t		lb_dma;

	struct lro_ctrl		lro;

	qla_rx_buf_t		rx_buf[NUM_RX_DESCRIPTORS];
	qla_rx_buf_t		*rxb_free;
	uint32_t		rx_free;
	uint32_t		rx_next;

	uint32_t		cq_db_offset;

	void			*cq_icb_vaddr;
	bus_addr_t		cq_icb_paddr;

	uint32_t		*cqi_vaddr;
	bus_addr_t		cqi_paddr;

	void			*cq_base_vaddr;
	bus_addr_t		cq_base_paddr;
	uint32_t		cq_next; /* next cq entry to process */

	void			*lbq_addr_tbl_vaddr;
	bus_addr_t		lbq_addr_tbl_paddr;

	void			*lbq_vaddr;
	bus_addr_t		lbq_paddr;
	uint32_t		lbq_next; /* next entry in LBQ to process */
	uint32_t		lbq_free;/* # of entries in LBQ to arm */
	uint32_t		lbq_in; /* next entry in LBQ to arm */

	void			*lb_vaddr;
	bus_addr_t		lb_paddr;

	void			*sbq_addr_tbl_vaddr;
	bus_addr_t		sbq_addr_tbl_paddr;

	void			*sbq_vaddr;
	bus_addr_t		sbq_paddr;
	uint32_t		sbq_next; /* next entry in SBQ to process */
	uint32_t		sbq_free;/* # of entries in SBQ to arm */
	uint32_t		sbq_in; /* next entry in SBQ to arm */

	uint64_t		rx_int;
	uint64_t		rss_int;
};
typedef struct qla_rx_ring qla_rx_ring_t;


#define QLA_WATCHDOG_CALLOUT_TICKS	1

/*
 * Multicast Definitions
 */
typedef struct _qla_mcast {
	uint16_t	rsrvd;
	uint8_t		addr[6];
} __packed qla_mcast_t;

/*
 * Misc. definitions
 */
#define QLA_PAGE_SIZE		4096

/*
 * Adapter structure contains the hardware independent information of the
 * pci function.
 */
struct qla_host {
        volatile struct {
                volatile uint32_t
			mpi_dma			:1,
			rss_dma			:1,
			intr_enable		:1,
			qla_callout_init	:1,
			qla_watchdog_active	:1,
			qla_watchdog_exit	:1,
			qla_watchdog_pause	:1,
			lro_init		:1,
			parent_tag		:1,
			lock_init		:1;
        } flags;

	volatile uint32_t	hw_init;

	volatile uint32_t	qla_watchdog_exited;
	volatile uint32_t	qla_watchdog_paused;
	volatile uint32_t	qla_initiate_recovery;

	device_t		pci_dev;

	uint8_t			pci_func;
	uint16_t		watchdog_ticks;
	uint8_t			resvd;

        /* ioctl related */
        struct cdev             *ioctl_dev;

	/* register mapping */
	struct resource		*pci_reg;
	int			reg_rid;

	struct resource		*pci_reg1;
	int			reg_rid1;

	int			msix_count;
	qla_ivec_t              irq_vec[MAX_RX_RINGS];

	/* parent dma tag */
	bus_dma_tag_t           parent_tag;

	/* interface to o.s */
	struct ifnet		*ifp;

	struct ifmedia		media;
	uint16_t		max_frame_size;
	uint16_t		rsrvd0;
	uint32_t		msize;
	int			if_flags;

	/* hardware access lock */
	struct mtx		hw_lock;
	volatile uint32_t	hw_lock_held;

	uint32_t		vm_pgsize;
	/* transmit related */
	uint32_t		num_tx_rings;
	qla_tx_ring_t		tx_ring[MAX_TX_RINGS];
						
	bus_dma_tag_t		tx_tag;
	struct task		tx_task;
	struct taskqueue	*tx_tq;
	struct callout		tx_callout;
	struct mtx		tx_lock;

	/* receive related */
	uint32_t		num_rx_rings;
	qla_rx_ring_t		rx_ring[MAX_RX_RINGS];
	bus_dma_tag_t		rx_tag;

	/* stats */
	uint32_t		err_m_getcl;
	uint32_t		err_m_getjcl;
	uint32_t		err_tx_dmamap_create;
	uint32_t		err_tx_dmamap_load;
	uint32_t		err_tx_defrag;

	/* mac address related */
	uint8_t			mac_rcv_mode;
	uint8_t			mac_addr[ETHER_ADDR_LEN];
	uint32_t		nmcast;
	qla_mcast_t		mcast[Q8_MAX_NUM_MULTICAST_ADDRS];
	
	/* Link Related */
        uint8_t			link_up;
	uint32_t		link_status;
	uint32_t		link_down_info;
	uint32_t		link_hw_info;
	uint32_t		link_dcbx_counters;
	uint32_t		link_change_counters;

	/* Flash Related */
	q81_flash_t		flash;

	/* debug stuff */
	volatile const char 	*qla_lock;
	volatile const char	*qla_unlock;

	/* Error Recovery Related */
	uint32_t		err_inject;
	struct task		err_task;
	struct taskqueue	*err_tq;

	/* Chip related */
	uint32_t		rev_id;

	/* mailbox completions */
	uint32_t		aen[Q81_NUM_AEN_REGISTERS];
	uint32_t		mbox[Q81_NUM_MBX_REGISTERS];
	volatile uint32_t       mbx_done;

	/* mpi dump related */
	qla_dma_t		mpi_dma;
	qla_dma_t		rss_dma;
	
};
typedef struct qla_host qla_host_t;

/* note that align has to be a power of 2 */
#define QL_ALIGN(size, align) (((size) + ((align) - 1)) & (~((align) - 1)))
#define QL_MIN(x, y) ((x < y) ? x : y)

#define QL_RUNNING(ifp) \
		((ifp->if_drv_flags & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) == \
			IFF_DRV_RUNNING)

/* Return 0, if identical, else 1 */

#define QL_MAC_CMP(mac1, mac2)    \
	((((*(uint32_t *) mac1) == (*(uint32_t *) mac2) && \
	(*(uint16_t *)(mac1 + 4)) == (*(uint16_t *)(mac2 + 4)))) ? 0 : 1)

#endif /* #ifndef _QLS_DEF_H_ */
