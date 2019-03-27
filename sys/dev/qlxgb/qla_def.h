/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011-2013 Qlogic Corporation
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
 * File: qla_def.h
 * Author : David C Somayajulu, Qlogic Corporation, Aliso Viejo, CA 92656.
 */

#ifndef _QLA_DEF_H_
#define _QLA_DEF_H_

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

struct qla_tx_buf {
	struct mbuf	*m_head;
	bus_dmamap_t	map;
};
typedef struct qla_tx_buf qla_tx_buf_t;

#define QLA_MAX_SEGMENTS	63	/* maximum # of segs in a sg list */
#define QLA_MAX_FRAME_SIZE	MJUM9BYTES
#define QLA_STD_FRAME_SIZE	1514
#define QLA_MAX_TSO_FRAME_SIZE	((64 * 1024 - 1) + 22)

/* Number of MSIX/MSI Vectors required */
#define Q8_MSI_COUNT		4

struct qla_ivec {
	struct resource		*irq;
	void			*handle;
	int			irq_rid;
	void			*ha;
	struct task		rcv_task;
	struct taskqueue	*rcv_tq;
};

typedef struct qla_ivec qla_ivec_t;

#define QLA_WATCHDOG_CALLOUT_TICKS	1

/*
 * Adapter structure contains the hardware independent information of the
 * pci function.
 */
struct qla_host {
        volatile struct {
                volatile uint32_t
			qla_watchdog_active  :1,
			qla_watchdog_exit    :1,
			qla_watchdog_pause   :1,
			lro_init	:1,
			stop_rcv	:1,
			link_up		:1,
			parent_tag	:1,
			lock_init	:1;
        } flags;

	device_t		pci_dev;

	uint8_t			pci_func;
	uint16_t		watchdog_ticks;
	uint8_t			resvd;

        /* ioctl related */
        struct cdev             *ioctl_dev;

	/* register mapping */
	struct resource		*pci_reg;
	int			reg_rid;

	/* interrupts */
	struct resource         *irq;
	int			msix_count;
	void			*intr_handle;
	qla_ivec_t		irq_vec[Q8_MSI_COUNT];
	
	/* parent dma tag */
	bus_dma_tag_t           parent_tag;

	/* interface to o.s */
	struct ifnet		*ifp;

	struct ifmedia		media;
	uint16_t		max_frame_size;
	uint16_t		rsrvd0;
	int			if_flags;

	/* hardware access lock */
	struct mtx		hw_lock;
	volatile uint32_t	hw_lock_held;

	/* transmit and receive buffers */
	qla_tx_buf_t		tx_buf[NUM_TX_DESCRIPTORS];
	bus_dma_tag_t		tx_tag;
	struct mtx		tx_lock;
	struct task		tx_task;
	struct taskqueue	*tx_tq;
	struct callout		tx_callout;

	qla_rx_buf_t		rx_buf[NUM_RX_DESCRIPTORS];
	qla_rx_buf_t		rx_jbuf[NUM_RX_JUMBO_DESCRIPTORS];
	bus_dma_tag_t		rx_tag;

	struct mtx		rx_lock;
	struct mtx		rxj_lock;

	/* stats */
	uint32_t		err_m_getcl;
	uint32_t		err_m_getjcl;
	uint32_t		err_tx_dmamap_create;
	uint32_t		err_tx_dmamap_load;
	uint32_t		err_tx_defrag;

	uint64_t		rx_frames;
	uint64_t		rx_bytes;

	uint64_t		tx_frames;
	uint64_t		tx_bytes;

        uint32_t                fw_ver_major;
        uint32_t                fw_ver_minor;
        uint32_t                fw_ver_sub;
        uint32_t                fw_ver_build;

	/* hardware specific */
	qla_hw_t		hw;

	/* debug stuff */
	volatile const char 	*qla_lock;
	volatile const char	*qla_unlock;

	uint8_t			fw_ver_str[32];
};
typedef struct qla_host qla_host_t;

/* note that align has to be a power of 2 */
#define QL_ALIGN(size, align) (((size) + ((align) - 1)) & (~((align) - 1)))
#define QL_MIN(x, y) ((x < y) ? x : y)

#define QL_RUNNING(ifp) \
		((ifp->if_drv_flags & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) == \
			IFF_DRV_RUNNING)

#endif /* #ifndef _QLA_DEF_H_ */
