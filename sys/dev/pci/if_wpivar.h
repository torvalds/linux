/*	$OpenBSD: if_wpivar.h,v 1.29 2020/10/11 07:05:29 mpi Exp $	*/

/*-
 * Copyright (c) 2006-2008
 *	Damien Bergamini <damien.bergamini@free.fr>
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

struct wpi_rx_radiotap_header {
	struct ieee80211_radiotap_header wr_ihdr;
	uint64_t	wr_tsft;
	uint8_t		wr_flags;
	uint8_t		wr_rate;
	uint16_t	wr_chan_freq;
	uint16_t	wr_chan_flags;
	int8_t		wr_dbm_antsignal;
	int8_t		wr_dbm_antnoise;
	uint8_t		wr_antenna;
} __packed;

#define WPI_RX_RADIOTAP_PRESENT						\
	((1 << IEEE80211_RADIOTAP_TSFT) |				\
	 (1 << IEEE80211_RADIOTAP_FLAGS) |				\
	 (1 << IEEE80211_RADIOTAP_RATE) |				\
	 (1 << IEEE80211_RADIOTAP_CHANNEL) |				\
	 (1 << IEEE80211_RADIOTAP_DBM_ANTSIGNAL) |			\
	 (1 << IEEE80211_RADIOTAP_DBM_ANTNOISE) |			\
	 (1 << IEEE80211_RADIOTAP_ANTENNA))

struct wpi_tx_radiotap_header {
	struct ieee80211_radiotap_header wt_ihdr;
	uint8_t		wt_flags;
	uint8_t		wt_rate;
	uint16_t	wt_chan_freq;
	uint16_t	wt_chan_flags;
} __packed;

#define WPI_TX_RADIOTAP_PRESENT						\
	((1 << IEEE80211_RADIOTAP_FLAGS) |				\
	 (1 << IEEE80211_RADIOTAP_RATE) |				\
	 (1 << IEEE80211_RADIOTAP_CHANNEL))

struct wpi_dma_info {
	bus_dma_tag_t		tag;
	bus_dmamap_t		map;
	bus_dma_segment_t	seg;
	bus_addr_t		paddr;
	caddr_t			vaddr;
	bus_size_t		size;
};

struct wpi_tx_data {
	bus_dmamap_t		map;
	bus_addr_t		cmd_paddr;
	struct mbuf		*m;
	struct ieee80211_node	*ni;
};

struct wpi_tx_ring {
	struct wpi_dma_info	desc_dma;
	struct wpi_dma_info	cmd_dma;
	struct wpi_tx_desc	*desc;
	struct wpi_tx_cmd	*cmd;
	struct wpi_tx_data	data[WPI_TX_RING_COUNT];
	int			qid;
	int			queued;
	int			cur;
};

struct wpi_softc;

struct wpi_rx_data {
	struct mbuf	*m;
	bus_dmamap_t	map;
};

struct wpi_rx_ring {
	struct wpi_dma_info	desc_dma;
	uint32_t		*desc;
	struct wpi_rx_data	data[WPI_RX_RING_COUNT];
	int			cur;
};

struct wpi_node {
	struct	ieee80211_node		ni;	/* must be the first */
	struct	ieee80211_amrr_node	amn;
	uint8_t				id;
	uint8_t				ridx[IEEE80211_RATE_MAXSIZE];
};

struct wpi_power_sample {
	uint8_t	index;
	int8_t	power;
};

struct wpi_power_group {
#define WPI_SAMPLES_COUNT	5
	struct	wpi_power_sample samples[WPI_SAMPLES_COUNT];
	uint8_t	chan;
	int8_t	maxpwr;
	int16_t	temp;
};

struct wpi_fw_part {
	const uint8_t	*text;
	uint32_t	textsz;
	const uint8_t	*data;
	uint32_t	datasz;
};

struct wpi_fw_info {
	u_char			*data;
	size_t			datalen;
	struct wpi_fw_part	init;
	struct wpi_fw_part	main;
	struct wpi_fw_part	boot;
};

struct wpi_softc {
	struct device		sc_dev;

	struct ieee80211com	sc_ic;
	int			(*sc_newstate)(struct ieee80211com *,
				    enum ieee80211_state, int);

	struct ieee80211_amrr	amrr;
	uint8_t			fixed_ridx;

	bus_dma_tag_t		sc_dmat;

	struct rwlock		sc_rwlock;
	u_int			sc_flags;
#define WPI_FLAG_HAS_5GHZ	(1 << 0)

	/* Shared area. */
	struct wpi_dma_info	shared_dma;
	struct wpi_shared	*shared;

	/* Firmware DMA transfer. */
	struct wpi_dma_info	fw_dma;

	/* TX/RX rings. */
	struct wpi_tx_ring	txq[WPI_NTXQUEUES];
	struct wpi_rx_ring	rxq;

	bus_space_tag_t		sc_st;
	bus_space_handle_t	sc_sh;
	void 			*sc_ih;
	pci_chipset_tag_t	sc_pct;
	pcitag_t		sc_pcitag;
	bus_size_t		sc_sz;
	int			sc_cap_off;	/* PCIe Capabilities. */

	struct timeout		calib_to;
	int			calib_cnt;

	struct task		init_task;

	struct wpi_fw_info	fw;
	uint32_t		errptr;

	struct wpi_rxon		rxon;
	int			temp;
	uint32_t		qfullmsk;

	uint8_t			cap;
	uint16_t		rev;
	uint8_t			type;
	struct wpi_power_group	groups[WPI_POWER_GROUPS_COUNT];
	int8_t			maxpwr[IEEE80211_CHAN_MAX];

	int			sc_tx_timer;

#if NBPFILTER > 0
	caddr_t			sc_drvbpf;

	union {
		struct wpi_rx_radiotap_header th;
		uint8_t	pad[IEEE80211_RADIOTAP_HDRLEN];
	} sc_rxtapu;
#define sc_rxtap	sc_rxtapu.th
	int			sc_rxtap_len;

	union {
		struct wpi_tx_radiotap_header th;
		uint8_t	pad[IEEE80211_RADIOTAP_HDRLEN];
	} sc_txtapu;
#define sc_txtap	sc_txtapu.th
	int			sc_txtap_len;
#endif
};
