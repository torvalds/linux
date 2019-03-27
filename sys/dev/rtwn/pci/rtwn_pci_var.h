/*	$OpenBSD: if_rtwnreg.h,v 1.3 2015/06/14 08:02:47 stsp Exp $	*/

/*-
 * Copyright (c) 2010 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2015 Stefan Sperling <stsp@openbsd.org>
 * Copyright (c) 2016 Andriy Voskoboinyk <avos@FreeBSD.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 * 
 * $FreeBSD$
 */

#ifndef RTWN_PCI_VAR_H
#define RTWN_PCI_VAR_H

#define RTWN_PCI_RX_LIST_COUNT		256
#define RTWN_PCI_TX_LIST_COUNT		256

/* sizeof(struct rtwn_rx_stat_common) + R88E_INTR_MSG_LEN */
#define	RTWN_PCI_RX_TMP_BUF_SIZE	84

struct rtwn_rx_data {
	bus_dmamap_t		map;
	struct mbuf		*m;
	bus_addr_t		paddr;
};

struct rtwn_rx_ring {
	struct rtwn_rx_stat_pci	*desc;
	bus_addr_t		paddr;
	bus_dma_tag_t		desc_dmat;
	bus_dmamap_t		desc_map;
	bus_dma_tag_t		data_dmat;
	bus_dma_segment_t	seg;
	struct rtwn_rx_data	rx_data[RTWN_PCI_RX_LIST_COUNT];
	int			cur;
};

struct rtwn_tx_data {
	bus_dmamap_t		map;
	struct mbuf		*m;
	struct ieee80211_node	*ni;
};

struct rtwn_tx_ring {
	bus_addr_t		paddr;
	bus_dma_tag_t		desc_dmat;
	bus_dmamap_t		desc_map;
	bus_dma_tag_t		data_dmat;
	bus_dma_segment_t	seg;
	void			*desc;
	struct rtwn_tx_data	tx_data[RTWN_PCI_TX_LIST_COUNT];
	int			queued;
	int			cur;
	int			last;
};

/*
 * TX queue indices.
 */
enum {
	RTWN_PCI_BK_QUEUE,
	RTWN_PCI_BE_QUEUE,
	RTWN_PCI_VI_QUEUE,
	RTWN_PCI_VO_QUEUE,
	RTWN_PCI_BEACON_QUEUE,
	RTWN_PCI_TXCMD_QUEUE,
	RTWN_PCI_MGNT_QUEUE,
	RTWN_PCI_HIGH_QUEUE,
	RTWN_PCI_HCCA_QUEUE,
	RTWN_PCI_NTXQUEUES
};

/*
 * Interrupt events.
 */
enum {
	RTWN_PCI_INTR_RX_ERROR		= 0x00000001,
	RTWN_PCI_INTR_RX_OVERFLOW	= 0x00000002,
	RTWN_PCI_INTR_RX_DESC_UNAVAIL	= 0x00000004,
	RTWN_PCI_INTR_RX_DONE		= 0x00000008,
	RTWN_PCI_INTR_TX_ERROR		= 0x00000010,
	RTWN_PCI_INTR_TX_OVERFLOW	= 0x00000020,
	RTWN_PCI_INTR_TX_REPORT		= 0x00000040,
	RTWN_PCI_INTR_PS_TIMEOUT	= 0x00000080
};

/* Shortcuts */
/* Vendor driver treats RX errors like ROK... */
#define RTWN_PCI_INTR_RX \
	(RTWN_PCI_INTR_RX_ERROR | RTWN_PCI_INTR_RX_OVERFLOW | \
	 RTWN_PCI_INTR_RX_DESC_UNAVAIL | RTWN_PCI_INTR_RX_DONE)


struct rtwn_pci_softc {
	struct rtwn_softc	pc_sc;		/* must be the first */

	struct resource		*irq;
	struct resource		*mem;
	bus_space_tag_t		pc_st;
	bus_space_handle_t	pc_sh;
	void			*pc_ih;
	bus_size_t		pc_mapsize;

	uint8_t			pc_rx_buf[RTWN_PCI_RX_TMP_BUF_SIZE];
	struct rtwn_rx_ring	rx_ring;
	struct rtwn_tx_ring	tx_ring[RTWN_PCI_NTXQUEUES];

	/* must be set by the driver. */
	uint16_t		pc_qmap;
	uint32_t		tcr;

	void			(*pc_setup_tx_desc)(struct rtwn_pci_softc *,
				    void *, uint32_t);
	void			(*pc_tx_postsetup)(struct rtwn_pci_softc *,
				    void *, bus_dma_segment_t *);
	void			(*pc_copy_tx_desc)(void *, const void *);
	void			(*pc_enable_intr)(struct rtwn_pci_softc *);
	int			(*pc_get_intr_status)(struct rtwn_pci_softc *,
				    int *);
};
#define RTWN_PCI_SOFTC(sc)	((struct rtwn_pci_softc *)(sc))

#define rtwn_pci_setup_tx_desc(_pc, _desc, _addr) \
	(((_pc)->pc_setup_tx_desc)((_pc), (_desc), (_addr)))
#define rtwn_pci_tx_postsetup(_pc, _txd, _segs) \
	(((_pc)->pc_tx_postsetup)((_pc), (_txd), (_segs)))
#define rtwn_pci_copy_tx_desc(_pc, _dest, _src) \
	(((_pc)->pc_copy_tx_desc)((_dest), (_src)))
#define rtwn_pci_enable_intr(_pc) \
	(((_pc)->pc_enable_intr)((_pc)))
#define rtwn_pci_get_intr_status(_pc, _tx_rings) \
	(((_pc)->pc_get_intr_status)((_pc), (_tx_rings)))

#endif	/* RTWN_PCI_VAR_H */
