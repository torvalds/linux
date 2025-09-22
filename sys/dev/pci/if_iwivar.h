/*	$OpenBSD: if_iwivar.h,v 1.26 2016/09/05 08:17:48 tedu Exp $	*/

/*-
 * Copyright (c) 2004-2006
 *      Damien Bergamini <damien.bergamini@free.fr>. All rights reserved.
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
 */

struct iwi_rx_radiotap_header {
	struct ieee80211_radiotap_header wr_ihdr;
	uint8_t		wr_flags;
	uint8_t		wr_rate;
	uint16_t	wr_chan_freq;
	uint16_t	wr_chan_flags;
	uint8_t		wr_antsignal;
	uint8_t		wr_antenna;
} __packed;

#define IWI_RX_RADIOTAP_PRESENT						\
	((1 << IEEE80211_RADIOTAP_FLAGS) |				\
	 (1 << IEEE80211_RADIOTAP_RATE) |				\
	 (1 << IEEE80211_RADIOTAP_CHANNEL) |				\
	 (1 << IEEE80211_RADIOTAP_DB_ANTSIGNAL) |			\
	 (1 << IEEE80211_RADIOTAP_ANTENNA))

struct iwi_tx_radiotap_header {
	struct ieee80211_radiotap_header wt_ihdr;
	uint8_t		wt_flags;
	uint16_t	wt_chan_freq;
	uint16_t	wt_chan_flags;
} __packed;

#define IWI_TX_RADIOTAP_PRESENT						\
	((1 << IEEE80211_RADIOTAP_FLAGS) |				\
	 (1 << IEEE80211_RADIOTAP_CHANNEL))


struct iwi_cmd_ring {
	bus_dmamap_t		map;
	bus_dma_segment_t	seg;
	struct iwi_cmd_desc	*desc;
	int			queued;
	int			cur;
	int			next;
};

struct iwi_tx_data {
	bus_dmamap_t		map;
	struct mbuf		*m;
	struct ieee80211_node	*ni;
};

struct iwi_tx_ring {
	bus_dmamap_t		map;
	bus_dma_segment_t	seg;
	bus_size_t		csr_ridx;
	bus_size_t		csr_widx;
	struct iwi_tx_desc	*desc;
	struct iwi_tx_data	data[IWI_TX_RING_COUNT];
	int			queued;
	int			cur;
	int			next;
};

struct iwi_rx_data {
	bus_dmamap_t		map;
	struct mbuf		*m;
	uint32_t		reg;
};

struct iwi_rx_ring {
	struct iwi_rx_data	data[IWI_RX_RING_COUNT];
	int			cur;
};

struct iwi_softc {
	struct device		sc_dev;

	struct ieee80211com	sc_ic;
	int			(*sc_newstate)(struct ieee80211com *,
				    enum ieee80211_state, int);

	struct rwlock		sc_rwlock;

	bus_dma_tag_t		sc_dmat;

	struct iwi_cmd_ring	cmdq;
	struct iwi_tx_ring	txq[4];
	struct iwi_rx_ring	rxq;

#define IWI_MAX_NODE	32
	uint8_t			sta[IWI_MAX_NODE][IEEE80211_ADDR_LEN];
	uint8_t			nsta;

	bus_space_tag_t		sc_st;
	bus_space_handle_t	sc_sh;
	void 			*sc_ih;
	pci_chipset_tag_t	sc_pct;
	pcitag_t		sc_pcitag;
	bus_size_t		sc_sz;

	struct task		init_task;

	int			sc_tx_timer;

#if NBPFILTER > 0
	caddr_t			sc_drvbpf;

	union {
		struct iwi_rx_radiotap_header th;
		uint8_t	pad[64];
	} sc_rxtapu;
#define sc_rxtap	sc_rxtapu.th
	int			sc_rxtap_len;

	union {
		struct iwi_tx_radiotap_header th;
		uint8_t	pad[64];
	} sc_txtapu;
#define sc_txtap	sc_txtapu.th
	int			sc_txtap_len;
#endif
};
