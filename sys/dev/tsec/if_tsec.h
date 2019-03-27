/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2006-2007 Semihalf, Piotr Kruszynski
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _IF_TSEC_H
#define _IF_TSEC_H

#include <dev/ofw/openfirm.h>

#define TSEC_RX_NUM_DESC	256
#define TSEC_TX_NUM_DESC	256
#define	TSEC_TX_MAX_DMA_SEGS	8

/* Interrupt Coalescing types */
#define	TSEC_IC_RX		0
#define	TSEC_IC_TX		1

/* eTSEC ID */
#define	TSEC_ETSEC_ID		0x0124

/* Frame sizes */
#define	TSEC_MIN_FRAME_SIZE	64
#define	TSEC_MAX_FRAME_SIZE	9600

struct tsec_bufmap {
	bus_dmamap_t	map;
	int		map_initialized;
	struct mbuf	*mbuf;
};

struct tsec_softc {
	/* XXX MII bus requires that struct ifnet is first!!! */
	struct ifnet	*tsec_ifp;
	
	struct mtx	transmit_lock;	/* transmitter lock */
	struct mtx	receive_lock;	/* receiver lock */

	phandle_t	node;
	device_t	dev;
	device_t	tsec_miibus;
	struct mii_data	*tsec_mii;	/* MII media control */
	int		tsec_link;

	bus_dma_tag_t	tsec_tx_dtag;	/* TX descriptors tag */
	bus_dmamap_t	tsec_tx_dmap;	/* TX descriptors map */
	bus_dma_tag_t	tsec_tx_mtag;	/* TX mbufs tag */
	uint32_t	tx_idx_head;	/* TX head descriptor/bufmap index */
	uint32_t	tx_idx_tail;	/* TX tail descriptor/bufmap index */
	struct tsec_desc *tsec_tx_vaddr;/* virtual address of TX descriptors */
	struct tsec_bufmap tx_bufmap[TSEC_TX_NUM_DESC];

	bus_dma_tag_t	tsec_rx_mtag;	/* TX mbufs tag */
	bus_dma_tag_t	tsec_rx_dtag;	/* RX descriptors tag */
	bus_dmamap_t	tsec_rx_dmap;	/* RX descriptors map */
	struct tsec_desc *tsec_rx_vaddr; /* vadress of RX descriptors */

	struct rx_data_type {
		bus_dmamap_t	map;	/* mbuf map */
		struct mbuf	*mbuf;
		uint32_t	paddr;	/* DMA address of buffer */
	} rx_data[TSEC_RX_NUM_DESC];

	uint32_t	rx_cur_desc_cnt;

	struct resource	*sc_rres;	/* register resource */
	int		sc_rrid;	/* register rid */
	struct {
		bus_space_tag_t bst;
		bus_space_handle_t bsh;
	} sc_bas;

	struct resource *sc_transmit_ires;
	void		*sc_transmit_ihand;
	int		sc_transmit_irid;
	struct resource *sc_receive_ires;
	void		*sc_receive_ihand;
	int		sc_receive_irid;
	struct resource *sc_error_ires;
	void		*sc_error_ihand;
	int		sc_error_irid;

	int		tsec_if_flags;
	int		is_etsec;

	/* Watchdog and MII tick related */
	struct callout	tsec_callout;
	int		tsec_watchdog;

	/* interrupt coalescing */
	struct mtx	ic_lock;
	uint32_t	rx_ic_time;	/* RW, valid values 0..65535 */
	uint32_t	rx_ic_count;	/* RW, valid values 0..255 */
	uint32_t	tx_ic_time;
	uint32_t	tx_ic_count;

	/* currently received frame */
	struct mbuf	*frame;

	int		phyaddr;
	bus_space_tag_t phy_bst;
	bus_space_handle_t phy_bsh;
	int		phy_regoff;

	uint32_t	tsec_rx_raddr;	/* real address of RX descriptors */
	uint32_t	tsec_tx_raddr;	/* real address of TX descriptors */
};

/* interface to get/put generic objects */
#define TSEC_CNT_INIT(cnt, wrap) ((cnt) = ((wrap) - 1))

#define TSEC_INC(count, wrap) (count = ((count) + 1) & ((wrap) - 1))

#define TSEC_GET_GENERIC(hand, tab, count, wrap) \
		((hand)->tab[TSEC_INC((hand)->count, wrap)])

#define TSEC_PUT_GENERIC(hand, tab, count, wrap, val)	\
		((hand)->tab[TSEC_INC((hand)->count, wrap)] = val)

#define TSEC_BACK_GENERIC(sc, count, wrap) do {			\
		if ((sc)->count > 0)				\
			(sc)->count--;				\
		else						\
			(sc)->count = (wrap) - 1;		\
} while (0)

#define TSEC_FREE_TX_DESC(sc) \
    (((sc)->tx_idx_tail - (sc)->tx_idx_head - 1) & (TSEC_TX_NUM_DESC - 1))

/* interface for manage rx tsec_desc */
#define TSEC_RX_DESC_CNT_INIT(sc) do {					\
		TSEC_CNT_INIT((sc)->rx_cur_desc_cnt, TSEC_RX_NUM_DESC);	\
} while (0)

#define TSEC_GET_CUR_RX_DESC(sc)					\
		&TSEC_GET_GENERIC(sc, tsec_rx_vaddr, rx_cur_desc_cnt,	\
		TSEC_RX_NUM_DESC)

#define TSEC_BACK_CUR_RX_DESC(sc) \
		TSEC_BACK_GENERIC(sc, rx_cur_desc_cnt, TSEC_RX_NUM_DESC)

#define TSEC_GET_CUR_RX_DESC_CNT(sc) \
		((sc)->rx_cur_desc_cnt)

/* init all counters (for init only!) */
#define TSEC_TX_RX_COUNTERS_INIT(sc) do {	\
		sc->tx_idx_head = 0;		\
		sc->tx_idx_tail = 0;		\
		TSEC_RX_DESC_CNT_INIT(sc);	\
} while (0)

/* read/write bus functions */
#define TSEC_READ(sc, reg)		\
		bus_space_read_4((sc)->sc_bas.bst, (sc)->sc_bas.bsh, (reg))
#define TSEC_WRITE(sc, reg, val)	\
		bus_space_write_4((sc)->sc_bas.bst, (sc)->sc_bas.bsh, (reg), (val))

extern struct mtx tsec_phy_mtx;
#define TSEC_PHY_LOCK(sc)	mtx_lock(&tsec_phy_mtx)
#define TSEC_PHY_UNLOCK(sc)	mtx_unlock(&tsec_phy_mtx)
#define TSEC_PHY_READ(sc, reg)		\
		bus_space_read_4((sc)->phy_bst, (sc)->phy_bsh, \
			(reg) + (sc)->phy_regoff)
#define TSEC_PHY_WRITE(sc, reg, val)	\
		bus_space_write_4((sc)->phy_bst, (sc)->phy_bsh, \
			(reg) + (sc)->phy_regoff, (val))

/* Lock for transmitter */
#define TSEC_TRANSMIT_LOCK(sc) do {					\
		mtx_assert(&(sc)->receive_lock, MA_NOTOWNED);		\
		mtx_lock(&(sc)->transmit_lock);				\
} while (0)

#define TSEC_TRANSMIT_UNLOCK(sc)	mtx_unlock(&(sc)->transmit_lock)
#define TSEC_TRANSMIT_LOCK_ASSERT(sc)	mtx_assert(&(sc)->transmit_lock, MA_OWNED)

/* Lock for receiver */
#define TSEC_RECEIVE_LOCK(sc) do {					\
		mtx_assert(&(sc)->transmit_lock, MA_NOTOWNED);		\
		mtx_lock(&(sc)->receive_lock);				\
} while (0)

#define TSEC_RECEIVE_UNLOCK(sc)		mtx_unlock(&(sc)->receive_lock)
#define TSEC_RECEIVE_LOCK_ASSERT(sc)	mtx_assert(&(sc)->receive_lock, MA_OWNED)

/* Lock for interrupts coalescing */
#define	TSEC_IC_LOCK(sc) do {						\
		mtx_assert(&(sc)->ic_lock, MA_NOTOWNED);		\
		mtx_lock(&(sc)->ic_lock);				\
} while (0)

#define	TSEC_IC_UNLOCK(sc)		mtx_unlock(&(sc)->ic_lock)
#define	TSEC_IC_LOCK_ASSERT(sc)		mtx_assert(&(sc)->ic_lock, MA_OWNED)

/* Global tsec lock (with all locks) */
#define TSEC_GLOBAL_LOCK(sc) do {					\
		if ((mtx_owned(&(sc)->transmit_lock) ? 1 : 0) !=	\
			(mtx_owned(&(sc)->receive_lock) ? 1 : 0)) {	\
			panic("tsec deadlock possibility detection!");	\
		}							\
		mtx_lock(&(sc)->transmit_lock);				\
		mtx_lock(&(sc)->receive_lock);				\
} while (0)

#define TSEC_GLOBAL_UNLOCK(sc) do {		\
		TSEC_RECEIVE_UNLOCK(sc);	\
		TSEC_TRANSMIT_UNLOCK(sc);	\
} while (0)

#define TSEC_GLOBAL_LOCK_ASSERT(sc) do {	\
		TSEC_TRANSMIT_LOCK_ASSERT(sc);	\
		TSEC_RECEIVE_LOCK_ASSERT(sc);	\
} while (0)

/* From global to {transmit,receive} */
#define TSEC_GLOBAL_TO_TRANSMIT_LOCK(sc) do {	\
		mtx_unlock(&(sc)->receive_lock);\
} while (0)

#define TSEC_GLOBAL_TO_RECEIVE_LOCK(sc) do {	\
		mtx_unlock(&(sc)->transmit_lock);\
} while (0)

struct tsec_desc {
	volatile uint16_t	flags;	/* descriptor flags */
	volatile uint16_t	length;	/* buffer length */
	volatile uint32_t	bufptr;	/* buffer pointer */
};

#define TSEC_READ_RETRY	10000
#define TSEC_READ_DELAY	100

/* Structures and defines for TCP/IP Off-load */
struct tsec_tx_fcb {
	volatile uint16_t	flags;
	volatile uint8_t	l4_offset;
	volatile uint8_t	l3_offset;
	volatile uint16_t	ph_chsum;
	volatile uint16_t	vlan;
};

struct tsec_rx_fcb {
	volatile uint16_t	flags;
	volatile uint8_t	rq_index;
	volatile uint8_t	protocol;
	volatile uint16_t	unused;
	volatile uint16_t	vlan;
};

#define	TSEC_CHECKSUM_FEATURES	(CSUM_IP | CSUM_TCP | CSUM_UDP)

#define	TSEC_TX_FCB_IP4		TSEC_TX_FCB_L3_IS_IP
#define	TSEC_TX_FCB_IP6		(TSEC_TX_FCB_L3_IS_IP | TSEC_TX_FCB_L3_IS_IP6)

#define	TSEC_TX_FCB_TCP		TSEC_TX_FCB_L4_IS_TCP_UDP
#define	TSEC_TX_FCB_UDP		(TSEC_TX_FCB_L4_IS_TCP_UDP | TSEC_TX_FCB_L4_IS_UDP)

#define	TSEC_RX_FCB_IP_CSUM_CHECKED(flags)					\
		((flags & (TSEC_RX_FCB_IP_FOUND | TSEC_RX_FCB_IP6_FOUND |	\
		TSEC_RX_FCB_IP_CSUM | TSEC_RX_FCB_PARSE_ERROR))			\
		 == (TSEC_RX_FCB_IP_FOUND | TSEC_RX_FCB_IP_CSUM))

#define TSEC_RX_FCB_TCP_UDP_CSUM_CHECKED(flags)					\
		((flags & (TSEC_RX_FCB_TCP_UDP_FOUND | TSEC_RX_FCB_TCP_UDP_CSUM	\
		| TSEC_RX_FCB_PARSE_ERROR))					\
		== (TSEC_RX_FCB_TCP_UDP_FOUND | TSEC_RX_FCB_TCP_UDP_CSUM))

/* Prototypes */
extern devclass_t tsec_devclass;

int	tsec_attach(struct tsec_softc *sc);
int	tsec_detach(struct tsec_softc *sc);

void	tsec_error_intr(void *arg);
void	tsec_receive_intr(void *arg);
void	tsec_transmit_intr(void *arg);

int	tsec_miibus_readreg(device_t dev, int phy, int reg);
int	tsec_miibus_writereg(device_t dev, int phy, int reg, int value);
void	tsec_miibus_statchg(device_t dev);
int	tsec_resume(device_t dev); /* XXX */
int	tsec_shutdown(device_t dev);
int	tsec_suspend(device_t dev); /* XXX */

void	tsec_get_hwaddr(struct tsec_softc *sc, uint8_t *addr);

#endif /* _IF_TSEC_H */
