/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997 Semen Ustimenko
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * Configuration
 */
/*#define	EPIC_DIAG	1*/
/*#define	EPIC_USEIOSPACE	1*/
/*#define	EPIC_EARLY_RX	1*/

#ifndef ETHER_MAX_LEN
#define ETHER_MAX_LEN		1518
#endif
#ifndef ETHER_MIN_LEN
#define ETHER_MIN_LEN		64
#endif
#ifndef ETHER_CRC_LEN
#define ETHER_CRC_LEN		4
#endif
#define TX_RING_SIZE		16		/* Leave this a power of 2 */
#define RX_RING_SIZE		16		/* And this too, to do not */
						/* confuse RX(TX)_RING_MASK */
#define TX_RING_MASK		(TX_RING_SIZE - 1)
#define RX_RING_MASK		(RX_RING_SIZE - 1)
#define ETHER_MAX_FRAME_LEN	(ETHER_MAX_LEN + ETHER_CRC_LEN)
#define	ETHER_ALIGN		2

/* This is driver's structure to define EPIC descriptors */
struct epic_rx_buffer {
	struct mbuf *mbuf;		/* mbuf receiving packet */
	bus_dmamap_t map;		/* DMA map */
};

struct epic_tx_buffer {
	struct mbuf *mbuf;		/* mbuf contained packet */
	bus_dmamap_t map;		/* DMA map */
};

/* PHY, known by tx driver */
#define	EPIC_UNKN_PHY		0x0000
#define	EPIC_QS6612_PHY		0x0001
#define	EPIC_AC101_PHY		0x0002
#define	EPIC_LXT970_PHY		0x0003
#define	EPIC_SERIAL		0x0004

/* Driver status structure */
typedef struct {
	struct ifnet		*ifp;
	struct resource		*res;
	struct resource		*irq;

	device_t		miibus;
	device_t		dev;
	struct callout		timer;
	struct mtx		lock;
	int			tx_timeout;

	void			*sc_ih;
	bus_dma_tag_t		mtag;
	bus_dma_tag_t		rtag;
	bus_dmamap_t		rmap;
	bus_dma_tag_t		ttag;
	bus_dmamap_t		tmap;
	bus_dma_tag_t		ftag;
	bus_dmamap_t		fmap;
	bus_dmamap_t		sparemap;

	struct epic_rx_buffer	rx_buffer[RX_RING_SIZE];
	struct epic_tx_buffer	tx_buffer[TX_RING_SIZE];

	/* Each element of array MUST be aligned on dword  */
	/* and bounded on PAGE_SIZE 			   */
	struct epic_rx_desc	*rx_desc;
	struct epic_tx_desc	*tx_desc;
	struct epic_frag_list	*tx_flist;
	u_int32_t		rx_addr;
	u_int32_t		tx_addr;
	u_int32_t		frag_addr;
	u_int32_t		flags;
	u_int32_t		tx_threshold;
	u_int32_t		txcon;
	u_int32_t		miicfg;
	u_int32_t		cur_tx;
	u_int32_t		cur_rx;
	u_int32_t		dirty_tx;
	u_int32_t		pending_txs;
	u_int16_t		cardvend;
	u_int16_t		cardid;
	struct mii_softc 	*physc;
	u_int32_t		phyid;
	int			serinst;
	void 			*pool;
} epic_softc_t;

#define	EPIC_LOCK(sc)		mtx_lock(&(sc)->lock)
#define	EPIC_UNLOCK(sc)		mtx_unlock(&(sc)->lock)
#define	EPIC_ASSERT_LOCKED(sc)	mtx_assert(&(sc)->lock, MA_OWNED)

struct epic_type {
	u_int16_t	ven_id;
	u_int16_t	dev_id;
	char		*name;
};

#define CSR_WRITE_4(sc, reg, val) 					\
	bus_write_4((sc)->res, (reg), (val))
#define CSR_WRITE_2(sc, reg, val) 					\
	bus_write_2((sc)->res, (reg), (val))
#define CSR_WRITE_1(sc, reg, val) 					\
	bus_write_1((sc)->res, (reg), (val))
#define CSR_READ_4(sc, reg) 						\
	bus_read_4((sc)->res, (reg))
#define CSR_READ_2(sc, reg) 						\
	bus_read_2((sc)->res, (reg))
#define CSR_READ_1(sc, reg) 						\
	bus_read_1((sc)->res, (reg))

#define	PHY_READ_2(sc, phy, reg)					\
	epic_read_phy_reg((sc), (phy), (reg))
#define	PHY_WRITE_2(sc, phy, reg, val)					\
	epic_write_phy_reg((sc), (phy), (reg), (val))
