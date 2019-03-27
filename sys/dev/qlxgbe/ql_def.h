/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013-2016 Qlogic Corporation
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
 * File: ql_def.h
 * Author : David C Somayajulu, Qlogic Corporation, Aliso Viejo, CA 92656.
 */

#ifndef _QL_DEF_H_
#define _QL_DEF_H_

#define BIT_0                   (0x1 << 0)
#define BIT_1                   (0x1 << 1)
#define BIT_2                   (0x1 << 2)
#define BIT_3                   (0x1 << 3)
#define BIT_4                   (0x1 << 4)
#define BIT_5                   (0x1 << 5)
#define BIT_6                   (0x1 << 6)
#define BIT_7                   (0x1 << 7)
#define BIT_8                   (0x1 << 8)
#define BIT_9                   (0x1 << 9)
#define BIT_10                  (0x1 << 10)
#define BIT_11                  (0x1 << 11)
#define BIT_12                  (0x1 << 12)
#define BIT_13                  (0x1 << 13)
#define BIT_14                  (0x1 << 14)
#define BIT_15                  (0x1 << 15)
#define BIT_16                  (0x1 << 16)
#define BIT_17                  (0x1 << 17)
#define BIT_18                  (0x1 << 18)
#define BIT_19                  (0x1 << 19)
#define BIT_20                  (0x1 << 20)
#define BIT_21                  (0x1 << 21)
#define BIT_22                  (0x1 << 22)
#define BIT_23                  (0x1 << 23)
#define BIT_24                  (0x1 << 24)
#define BIT_25                  (0x1 << 25)
#define BIT_26                  (0x1 << 26)
#define BIT_27                  (0x1 << 27)
#define BIT_28                  (0x1 << 28)
#define BIT_29                  (0x1 << 29)
#define BIT_30                  (0x1 << 30)
#define BIT_31                  (0x1 << 31)

struct qla_rx_buf {
	struct mbuf	*m_head;
	bus_dmamap_t	map;
	bus_addr_t      paddr;
	uint32_t	handle;
	void		*next;
};
typedef struct qla_rx_buf qla_rx_buf_t;

struct qla_rx_ring {
	qla_rx_buf_t	rx_buf[NUM_RX_DESCRIPTORS];
};
typedef struct qla_rx_ring qla_rx_ring_t;

struct qla_tx_buf {
	struct mbuf	*m_head;
	bus_dmamap_t	map;
};
typedef struct qla_tx_buf qla_tx_buf_t;

#define QLA_MAX_SEGMENTS	62	/* maximum # of segs in a sg list */
#define QLA_MAX_MTU		9000
#define QLA_STD_FRAME_SIZE	1514
#define QLA_MAX_TSO_FRAME_SIZE	((64 * 1024 - 1) + 22)

/* Number of MSIX/MSI Vectors required */

struct qla_ivec {
	uint32_t		sds_idx;
	void			*ha;
	struct resource		*irq;
	void			*handle;
	int			irq_rid;
};

typedef struct qla_ivec qla_ivec_t;

#define QLA_WATCHDOG_CALLOUT_TICKS	2

typedef struct _qla_tx_ring {
	qla_tx_buf_t	tx_buf[NUM_TX_DESCRIPTORS];
	uint64_t	count;
	uint64_t	iscsi_pkt_count;
} qla_tx_ring_t;

typedef struct _qla_tx_fp {
	struct mtx		tx_mtx;
	char			tx_mtx_name[32];
	struct buf_ring		*tx_br;
	struct task		fp_task;
	struct taskqueue	*fp_taskqueue;
	void			*ha;
	uint32_t		txr_idx;
} qla_tx_fp_t;

/*
 * Adapter structure contains the hardware independant information of the
 * pci function.
 */
struct qla_host {
        volatile struct {
                volatile uint32_t
			qla_callout_init	:1,
			qla_watchdog_active	:1,
			parent_tag		:1,
			lock_init		:1;
        } flags;

	volatile uint32_t	qla_interface_up;
	volatile uint32_t	stop_rcv;
	volatile uint32_t	qla_watchdog_exit;
	volatile uint32_t	qla_watchdog_exited;
	volatile uint32_t	qla_watchdog_pause;
	volatile uint32_t	qla_watchdog_paused;
	volatile uint32_t	qla_initiate_recovery;
	volatile uint32_t	qla_detach_active;
	volatile uint32_t	offline;

	device_t		pci_dev;

	volatile uint16_t	watchdog_ticks;
	uint8_t			pci_func;

        /* ioctl related */
        struct cdev             *ioctl_dev;

	/* register mapping */
	struct resource		*pci_reg;
	int			reg_rid;
	struct resource		*pci_reg1;
	int			reg_rid1;

	/* interrupts */
	struct resource         *mbx_irq;
	void			*mbx_handle;
	int			mbx_irq_rid;

	int			msix_count;

	qla_ivec_t		irq_vec[MAX_SDS_RINGS];
	
	/* parent dma tag */
	bus_dma_tag_t           parent_tag;

	/* interface to o.s */
	struct ifnet		*ifp;

	struct ifmedia		media;
	uint16_t		max_frame_size;
	uint16_t		rsrvd0;
	int			if_flags;

	/* hardware access lock */

	struct mtx		sp_log_lock;
	struct mtx		hw_lock;
	volatile uint32_t	hw_lock_held;
	uint64_t		hw_lock_failed;

	/* transmit and receive buffers */
	uint32_t		txr_idx; /* index of the current tx ring */
	qla_tx_ring_t		tx_ring[NUM_TX_RINGS];
						
	bus_dma_tag_t		tx_tag;
	struct callout		tx_callout;

	qla_tx_fp_t		tx_fp[MAX_SDS_RINGS];

	qla_rx_ring_t		rx_ring[MAX_RDS_RINGS];
	bus_dma_tag_t		rx_tag;
	uint32_t		std_replenish;

	qla_rx_buf_t		*rxb_free;
	uint32_t		rxb_free_count;

	/* stats */
	uint32_t		err_m_getcl;
	uint32_t		err_m_getjcl;
	uint32_t		err_tx_dmamap_create;
	uint32_t		err_tx_dmamap_load;
	uint32_t		err_tx_defrag;

	uint64_t		rx_frames;
	uint64_t		rx_bytes;

	uint64_t		lro_pkt_count;
	uint64_t		lro_bytes;

	uint64_t		ipv4_lro;
	uint64_t		ipv6_lro;

	uint64_t		tx_frames;
	uint64_t		tx_bytes;
	uint64_t		tx_tso_frames;
	uint64_t		hw_vlan_tx_frames;

	struct task             stats_task;
	struct taskqueue	*stats_tq;
	
        uint32_t                fw_ver_major;
        uint32_t                fw_ver_minor;
        uint32_t                fw_ver_sub;
        uint32_t                fw_ver_build;

	/* hardware specific */
	qla_hw_t		hw;

	/* debug stuff */
	volatile const char 	*qla_lock;
	volatile const char	*qla_unlock;
	uint32_t		dbg_level;
	uint32_t		enable_minidump;
	uint32_t		enable_driverstate_dump;
	uint32_t		enable_error_recovery;
	uint32_t		ms_delay_after_init;

	uint8_t			fw_ver_str[32];

	/* Error Injection Related */
	uint32_t		err_inject;
	struct task		err_task;
	struct taskqueue	*err_tq;

	/* Async Event Related */
	uint32_t                async_event;
	struct task             async_event_task;
	struct taskqueue        *async_event_tq;

	/* Peer Device */
	device_t		peer_dev;

	volatile uint32_t	msg_from_peer;
#define QL_PEER_MSG_RESET	0x01
#define QL_PEER_MSG_ACK		0x02

};
typedef struct qla_host qla_host_t;

/* note that align has to be a power of 2 */
#define QL_ALIGN(size, align) (((size) + ((align) - 1)) & (~((align) - 1)))
#define QL_MIN(x, y) ((x < y) ? x : y)

#define QL_RUNNING(ifp) (ifp->if_drv_flags & IFF_DRV_RUNNING)

/* Return 0, if identical, else 1 */
#define QL_MAC_CMP(mac1, mac2)    \
	((((*(uint32_t *) mac1) == (*(uint32_t *) mac2) && \
	(*(uint16_t *)(mac1 + 4)) == (*(uint16_t *)(mac2 + 4)))) ? 0 : 1)

#define QL_INITIATE_RECOVERY(ha) qla_set_error_recovery(ha)

#endif /* #ifndef _QL_DEF_H_ */
