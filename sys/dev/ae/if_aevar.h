/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Stanislav Sedov <stas@FreeBSD.org>.
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
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef IF_AEVAR_H
#define IF_AEVAR_H

/*
 * Supported chips identifiers.
*/
#define	VENDORID_ATTANSIC	0x1969
#define	DEVICEID_ATTANSIC_L2	0x2048

/* How much to wait for reset to complete (10 microsecond units). */
#define	AE_RESET_TIMEOUT	100

/* How much to wait for device to enter idle state (100 microsecond units). */
#define	AE_IDLE_TIMEOUT		100

/* How much to wait for MDIO to do the work (2 microsecond units). */
#define	AE_MDIO_TIMEOUT		10

/* How much to wait for VPD reading operation to complete (2 ms units). */
#define AE_VPD_TIMEOUT		10

/* How much to wait for send operation to complete (HZ units). */
#define	AE_TX_TIMEOUT		5

/* Default PHY address. */
#define	AE_PHYADDR_DEFAULT	0

/* Tx packet descriptor header format. */
typedef struct ae_txd {
	uint16_t	len;
	uint16_t	vlan;
} __packed ae_txd_t;

/* Tx status descriptor format. */
typedef struct ae_txs {
	uint16_t	len;
	uint16_t	flags;
} __packed ae_txs_t;

/* Rx packet descriptor format. */
typedef struct ae_rxd {
	uint16_t	len;
	uint16_t	flags;
	uint16_t	vlan;
	uint16_t	__pad;
	uint8_t		data[1528];
} __packed ae_rxd_t;

/* Statistics. */
typedef struct ae_stats {
	uint32_t	rx_bcast;
	uint32_t	rx_mcast;
	uint32_t	rx_pause;
	uint32_t	rx_ctrl;
	uint32_t	rx_crcerr;
	uint32_t	rx_codeerr;
	uint32_t	rx_runt;
	uint32_t	rx_frag;
	uint32_t	rx_trunc;
	uint32_t	rx_align;
	uint32_t	tx_bcast;
	uint32_t	tx_mcast;
	uint32_t	tx_pause;
	uint32_t	tx_ctrl;
	uint32_t	tx_defer;
	uint32_t	tx_excdefer;
	uint32_t	tx_singlecol;
	uint32_t	tx_multicol;
	uint32_t	tx_latecol;
	uint32_t	tx_abortcol;
	uint32_t	tx_underrun;
} ae_stats_t;

/* Software state structure. */
typedef struct ae_softc	{
	struct ifnet		*ifp;
	device_t		dev;
	device_t		miibus;
	struct resource		*mem[1];
	struct resource_spec	*spec_mem;
	struct resource		*irq[1];
	struct resource_spec	*spec_irq;
	void			*intrhand;

	struct mtx		mtx;

	uint8_t			eaddr[ETHER_ADDR_LEN];
	uint8_t			flags;
	int			if_flags;

	struct callout		tick_ch;

	/* Tasks. */
	struct task		int_task;
	struct task		link_task;
	struct taskqueue	*tq;
	
	/* DMA tags. */
	bus_dma_tag_t		dma_parent_tag;
	bus_dma_tag_t		dma_rxd_tag;
	bus_dma_tag_t		dma_txd_tag;
	bus_dma_tag_t		dma_txs_tag;
	bus_dmamap_t		dma_rxd_map;
	bus_dmamap_t		dma_txd_map;
	bus_dmamap_t		dma_txs_map;

	bus_addr_t		dma_rxd_busaddr;
	bus_addr_t		dma_txd_busaddr;
	bus_addr_t		dma_txs_busaddr;
	
	char			*rxd_base_dma;	/* Start of allocated area. */
	ae_rxd_t		*rxd_base;	/* Start of RxD ring. */
	char			*txd_base;	/* Start of TxD ring. */
	ae_txs_t		*txs_base;	/* Start of TxS ring. */

	/* Ring pointers. */
	unsigned int		rxd_cur;
	unsigned int		txd_cur;
	unsigned int		txs_cur;
	unsigned int		txs_ack;
	unsigned int		txd_ack;

	int			tx_inproc;	/* Active Tx frames in ring. */
	int			wd_timer;

	ae_stats_t		stats;
} ae_softc_t;

#define	AE_LOCK(_sc)		mtx_lock(&(_sc)->mtx)
#define	AE_UNLOCK(_sc)		mtx_unlock(&(_sc)->mtx)
#define	AE_LOCK_ASSERT(_sc)	mtx_assert(&(_sc)->mtx, MA_OWNED)

#define	BUS_ADDR_LO(x)		((uint64_t) (x) & 0xFFFFFFFF)
#define	BUS_ADDR_HI(x)		((uint64_t) (x) >> 32)

#define	AE_FLAG_LINK		0x01	/* Has link. */
#define	AE_FLAG_DETACH		0x02	/* Is detaching. */
#define	AE_FLAG_TXAVAIL		0x04	/* Tx'es available. */
#define	AE_FLAG_MSI		0x08	/* Using MSI. */
#define	AE_FLAG_PMG		0x10	/* Supports PCI power management. */

#endif	/* IF_AEVAR_H */
