/*
 * Copyright (c) 2017 Stormshield.
 * Copyright (c) 2017 Semihalf.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */

#ifndef _IF_MVNETAVAR_H_
#define	_IF_MVNETAVAR_H_
#include <net/if.h>

#define	MVNETA_HWHEADER_SIZE	2	/* Marvell Header */
#define	MVNETA_ETHER_SIZE	22	/* Maximum ether size */
#define	MVNETA_MAX_CSUM_MTU	1600	/* Port1,2 hw limit */

/*
 * Limit support for frame up to hw csum limit
 * until jumbo frame support is added.
 */
#define	MVNETA_MAX_FRAME		(MVNETA_MAX_CSUM_MTU + MVNETA_ETHER_SIZE)

/*
 * Default limit of queue length
 *
 * queue 0 is lowest priority and queue 7 is highest priority.
 * IP packet is received on queue 7 by default.
 */
#define	MVNETA_TX_RING_CNT	512
#define	MVNETA_RX_RING_CNT	256

#define	MVNETA_BUFRING_SIZE	1024

#define	MVNETA_PACKET_OFFSET	64
#define	MVNETA_PACKET_SIZE	MCLBYTES

#define	MVNETA_RXTH_COUNT	128
#define	MVNETA_RX_REFILL_COUNT	8
#define	MVNETA_TX_RECLAIM_COUNT	32

/*
 * Device Register access
 */
#define	MVNETA_READ(sc, reg) \
	bus_read_4((sc)->res[0], (reg))
#define	MVNETA_WRITE(sc, reg, val) \
	bus_write_4((sc)->res[0], (reg), (val))

#define	MVNETA_READ_REGION(sc, reg, val, c) \
	bus_read_region_4((sc)->res[0], (reg), (val), (c))
#define	MVNETA_WRITE_REGION(sc, reg, val, c) \
	bus_write_region_4((sc)->res[0], (reg), (val), (c))

#define	MVNETA_READ_MIB_4(sc, reg) \
	bus_read_4((sc)->res[0], MVNETA_PORTMIB_BASE + (reg))
#define	MVNETA_READ_MIB_8(sc, reg) \
	bus_read_8((sc)->res[0], MVNETA_PORTMIB_BASE + (reg))

#define	MVNETA_IS_LINKUP(sc) \
	(MVNETA_READ((sc), MVNETA_PSR) & MVNETA_PSR_LINKUP)

#define	MVNETA_IS_QUEUE_SET(queues, q) \
	((((queues) >> (q)) & 0x1))

/*
 * EEE: Lower Power Idle config
 * Default timer is duration of MTU sized frame transmission.
 * The timer can be negotiated by LLDP protocol, but we have no
 * support.
 */
#define	MVNETA_LPI_TS		(ETHERMTU * 8 / 1000) /* [us] */
#define	MVNETA_LPI_TW		(ETHERMTU * 8 / 1000) /* [us] */
#define	MVNETA_LPI_LI		(ETHERMTU * 8 / 1000) /* [us] */

/*
 * DMA Descriptor
 *
 * the ethernet device has 8 rx/tx DMA queues. each of queue has its own
 * decriptor list. descriptors are simply index by counter inside the device.
 */
#define	MVNETA_TX_SEGLIMIT	32

#define	MVNETA_QUEUE_IDLE	1
#define	MVNETA_QUEUE_WORKING	2
#define	MVNETA_QUEUE_DISABLED	3

struct mvneta_buf {
	struct mbuf *	m;	/* pointer to related mbuf */
	bus_dmamap_t	dmap;
};

struct mvneta_rx_ring {
	int				queue_status;
	/* Real descriptors array. shared by RxDMA */
	struct mvneta_rx_desc		*desc;
	bus_dmamap_t			desc_map;
	bus_addr_t			desc_pa;

	/* Virtual address of the RX buffer */
	void 				*rxbuf_virt_addr[MVNETA_RX_RING_CNT];

	/* Managment entries for each of descritors */
	struct mvneta_buf		rxbuf[MVNETA_RX_RING_CNT];

	/* locks */
	struct mtx			ring_mtx;

	/* Index */
	int				dma;
	int				cpu;

	/* Limit */
	int				queue_th_received;
	int				queue_th_time; /* [Tclk] */

	/* LRO */
	struct lro_ctrl			lro;
	boolean_t			lro_enabled;
	/* Is this queue out of mbuf */
	boolean_t			needs_refill;
} __aligned(CACHE_LINE_SIZE);

struct mvneta_tx_ring {
	/* Index of this queue */
	int				qidx;
	/* IFNET pointer */
	struct ifnet			*ifp;
	/* Ring buffer for IFNET */
	struct buf_ring			*br;
	/* Real descriptors array. shared by TxDMA */
	struct mvneta_tx_desc		*desc;
	bus_dmamap_t			desc_map;
	bus_addr_t			desc_pa;

	/* Managment entries for each of descritors */
	struct mvneta_buf		txbuf[MVNETA_TX_RING_CNT];

	/* locks */
	struct mtx			ring_mtx;

	/* Index */
	int				used;
	int				dma;
	int				cpu;

	/* watchdog */
#define	MVNETA_WATCHDOG_TXCOMP	(hz / 10) /* 100ms */
#define	MVNETA_WATCHDOG	(10 * hz) /* 10s */
	int				watchdog_time;
	int				queue_status;
	boolean_t			queue_hung;

	/* Task */
	struct task			task;
	struct taskqueue		*taskq;

	/* Stats */
	uint32_t			drv_error;
} __aligned(CACHE_LINE_SIZE);

static __inline int
tx_counter_adv(int ctr, int n)
{

	ctr += n;
	while (__predict_false(ctr >= MVNETA_TX_RING_CNT))
		ctr -= MVNETA_TX_RING_CNT;

	return (ctr);
}

static __inline int
rx_counter_adv(int ctr, int n)
{

	ctr += n;
	while (__predict_false(ctr >= MVNETA_RX_RING_CNT))
		ctr -= MVNETA_RX_RING_CNT;

	return (ctr);
}

/*
 * Timeout control
 */
#define	MVNETA_PHY_TIMEOUT	10000	/* msec */
#define	RX_DISABLE_TIMEOUT	0x1000000 /* times */
#define	TX_DISABLE_TIMEOUT	0x1000000 /* times */
#define	TX_FIFO_EMPTY_TIMEOUT	0x1000000 /* times */

/*
 * Debug
 */
#define	KASSERT_SC_MTX(sc) \
    KASSERT(mtx_owned(&(sc)->mtx), ("SC mutex not owned"))
#define	KASSERT_BM_MTX(sc) \
    KASSERT(mtx_owned(&(sc)->bm.bm_mtx), ("BM mutex not owned"))
#define	KASSERT_RX_MTX(sc, q) \
    KASSERT(mtx_owned(&(sc)->rx_ring[(q)].ring_mtx),\
        ("RX mutex not owned"))
#define	KASSERT_TX_MTX(sc, q) \
    KASSERT(mtx_owned(&(sc)->tx_ring[(q)].ring_mtx),\
        ("TX mutex not owned"))

/*
 * sysctl(9) parameters
 */
struct mvneta_sysctl_queue {
	struct mvneta_softc	*sc;
	int			rxtx;
	int			queue;
};
#define	MVNETA_SYSCTL_RX		0
#define	MVNETA_SYSCTL_TX		1

struct mvneta_sysctl_mib {
	struct mvneta_softc	*sc;
	int			index;
	uint64_t		counter;
};

enum mvneta_phy_mode {
	MVNETA_PHY_QSGMII,
	MVNETA_PHY_SGMII,
	MVNETA_PHY_RGMII,
	MVNETA_PHY_RGMII_ID
};

/*
 * Ethernet Device main context
 */
DECLARE_CLASS(mvneta_driver);

struct mvneta_softc {
	device_t	dev;
	uint32_t	version;
	/*
	 * mtx must be held by interface functions to/from
	 * other frameworks. interrupt hander, sysctl hander,
	 * ioctl hander, and so on.
	 */
	struct mtx	mtx;
	struct resource *res[2];
	void            *ih_cookie[1];

	struct ifnet	*ifp;
	uint32_t        mvneta_if_flags;
	uint32_t        mvneta_media;

	int			phy_attached;
	enum mvneta_phy_mode	phy_mode;
	int			phy_addr;
	int			phy_speed;	/* PHY speed */
	boolean_t		phy_fdx;	/* Full duplex mode */
	boolean_t		autoneg;	/* Autonegotiation status */
	boolean_t		use_inband_status;	/* In-band link status */

	/*
	 * Link State control
	 */
	boolean_t	linkup;
        device_t        miibus;
	struct mii_data *mii;
	uint8_t		enaddr[ETHER_ADDR_LEN];
	struct ifmedia	mvneta_ifmedia;

	bus_dma_tag_t	rx_dtag;
	bus_dma_tag_t	rxbuf_dtag;
	bus_dma_tag_t	tx_dtag;
	bus_dma_tag_t	txmbuf_dtag;
	struct mvneta_rx_ring		rx_ring[MVNETA_RX_QNUM_MAX];
	struct mvneta_tx_ring		tx_ring[MVNETA_TX_QNUM_MAX];

	/*
	 * Maintance clock
	 */
	struct callout		tick_ch;

	int cf_lpi;
	int cf_fc;
	int debug;

	/*
	 * Sysctl interfaces
	 */
	struct mvneta_sysctl_queue sysctl_rx_queue[MVNETA_RX_QNUM_MAX];
	struct mvneta_sysctl_queue sysctl_tx_queue[MVNETA_TX_QNUM_MAX];

	/*
	 * MIB counter
	 */
	struct mvneta_sysctl_mib sysctl_mib[MVNETA_PORTMIB_NOCOUNTER];
	uint64_t counter_pdfc;
	uint64_t counter_pofc;
	uint32_t counter_watchdog;		/* manual reset when clearing mib */
	uint32_t counter_watchdog_mib;	/* reset after each mib update */
};
#define	MVNETA_RX_RING(sc, q) \
    (&(sc)->rx_ring[(q)])
#define	MVNETA_TX_RING(sc, q) \
    (&(sc)->tx_ring[(q)])

int mvneta_attach(device_t);

#ifdef FDT
int mvneta_fdt_mac_address(struct mvneta_softc *, uint8_t *);
#endif

#endif /* _IF_MVNETAVAR_H_ */
