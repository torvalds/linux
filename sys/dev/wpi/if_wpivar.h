/*	$FreeBSD$	*/

/*-
 * Copyright (c) 2006,2007
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
} __packed __aligned(8);

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
	bus_addr_t		paddr;
	caddr_t			vaddr;
	bus_size_t		size;
};

struct wpi_tx_data {
	bus_dmamap_t		map;
	bus_addr_t		cmd_paddr;
	struct mbuf		*m;
	struct ieee80211_node	*ni;
	int			hdrlen;
};

struct wpi_tx_ring {
	struct wpi_dma_info	desc_dma;
	struct wpi_dma_info	cmd_dma;
	struct wpi_tx_desc	*desc;
	struct wpi_tx_cmd	*cmd;
	struct wpi_tx_data	data[WPI_TX_RING_COUNT];
	bus_dma_tag_t		data_dmat;
	uint8_t			qid;
	uint8_t			cur;
	uint8_t			pending;
	int16_t			queued;
	int			update:1;
};

struct wpi_rx_data {
	struct mbuf	*m;
	bus_dmamap_t	map;
};

struct wpi_rx_ring {
	struct wpi_dma_info	desc_dma;
	uint32_t		*desc;
	struct wpi_rx_data	data[WPI_RX_RING_COUNT];
	bus_dma_tag_t		data_dmat;
	uint16_t		cur;
	int			update;
};

struct wpi_node {
	struct ieee80211_node	ni;	/* must be the first */
	uint8_t			id;
};
#define WPI_NODE(ni)	((struct wpi_node *)(ni))

struct wpi_power_sample {
	uint8_t	index;
	int8_t	power;
};

struct wpi_power_group {
#define WPI_SAMPLES_COUNT	5
	struct wpi_power_sample samples[WPI_SAMPLES_COUNT];
	uint8_t	chan;
	int8_t	maxpwr;
	int16_t	temp;
};

struct wpi_buf {
	uint8_t			data[56];  /* sizeof(struct wpi_cmd_beacon) */
	struct ieee80211_node	*ni;
	struct mbuf		*m;
	size_t			size;
	uint8_t			code;
	uint16_t		ac;
};

struct wpi_vap {
	struct ieee80211vap	wv_vap;

	struct wpi_buf		wv_bcbuf;
	struct mtx		wv_mtx;

	uint8_t			wv_gtk;
#define WPI_VAP_KEY(kid)	(1 << kid)

	int			(*wv_newstate)(struct ieee80211vap *,
				    enum ieee80211_state, int);
	void			(*wv_recv_mgmt)(struct ieee80211_node *,
				    struct mbuf *, int,
				    const struct ieee80211_rx_stats *,
				    int, int);
};
#define	WPI_VAP(vap)	((struct wpi_vap *)(vap))

#define WPI_VAP_LOCK_INIT(_wvp)	\
	mtx_init(&(_wvp)->wv_mtx, "lock for wv_bcbuf/wv_boff structures", \
	    NULL, MTX_DEF)
#define WPI_VAP_LOCK(_wvp)		mtx_lock(&(_wvp)->wv_mtx)
#define WPI_VAP_UNLOCK(_wvp)		mtx_unlock(&(_wvp)->wv_mtx)
#define WPI_VAP_LOCK_ASSERT(_wvp)	mtx_assert(&(_wvp)->wv_mtx, MA_OWNED)
#define WPI_VAP_LOCK_DESTROY(_wvp)	mtx_destroy(&(_wvp)->wv_mtx)

struct wpi_fw_part {
	const uint8_t	*text;
	uint32_t	textsz;
	const uint8_t	*data;
	uint32_t	datasz;
};

struct wpi_fw_info {
	const uint8_t		*data;
	size_t			size;
	struct wpi_fw_part	init;
	struct wpi_fw_part	main;
	struct wpi_fw_part	boot;
};

struct wpi_softc {
	device_t		sc_dev;
	int			sc_debug;

	int			sc_running;

	struct mtx		sc_mtx;
	struct ieee80211com	sc_ic;
	struct ieee80211_ratectl_tx_status sc_txs;

	struct mtx		tx_mtx;

	/* Shared area. */
	struct wpi_dma_info	shared_dma;
	struct wpi_shared	*shared;

	struct wpi_tx_ring	txq[WPI_DRV_NTXQUEUES];
	struct mtx		txq_mtx;
	struct mtx		txq_state_mtx;

	struct wpi_rx_ring	rxq;
	uint64_t		rx_tstamp;

	/* TX Thermal Callibration. */
	struct callout		calib_to;

	struct callout		scan_timeout;
	struct callout		tx_timeout;

	/* Watch dog timer. */
	struct callout		watchdog_rfkill;

	/* Firmware image. */
	struct wpi_fw_info	fw;
	uint32_t		errptr;

	struct resource		*irq;
	struct resource		*mem;
	bus_space_tag_t		sc_st;
	bus_space_handle_t	sc_sh;
	void			*sc_ih;
	bus_size_t		sc_sz;
	int			sc_cap_off;	/* PCIe Capabilities. */

	struct wpi_rxon		rxon;
	struct mtx		rxon_mtx;

	int			temp;

	uint32_t		nodesmsk;
	struct mtx		nt_mtx;

	void			(*sc_node_free)(struct ieee80211_node *);
	void			(*sc_update_rx_ring)(struct wpi_softc *);
	void			(*sc_update_tx_ring)(struct wpi_softc *,
				    struct wpi_tx_ring *);

	struct wpi_rx_radiotap_header	sc_rxtap;
	struct wpi_tx_radiotap_header	sc_txtap;

	/* Firmware image. */
	const struct firmware	*fw_fp;

	/* Firmware DMA transfer. */
	struct wpi_dma_info	fw_dma;

	/* Tasks used by the driver. */
	struct task		sc_radiooff_task;
	struct task		sc_radioon_task;

	/* Eeprom info. */
	uint8_t			cap;
	uint16_t		rev;
	uint8_t			type;
	struct wpi_eeprom_chan
	    eeprom_channels[WPI_CHAN_BANDS_COUNT][WPI_MAX_CHAN_PER_BAND];
	struct wpi_power_group	groups[WPI_POWER_GROUPS_COUNT];
	int8_t			maxpwr[IEEE80211_CHAN_MAX];
	char			domain[4];	/* Regulatory domain. */
};

/*
 * Locking order:
 * 1. WPI_LOCK;
 * 2. WPI_RXON_LOCK;
 * 3. WPI_TX_LOCK;
 * 4. WPI_NT_LOCK / WPI_VAP_LOCK;
 * 5. WPI_TXQ_LOCK;
 * 6. WPI_TXQ_STATE_LOCK;
 */

#define WPI_LOCK_INIT(_sc) \
	mtx_init(&(_sc)->sc_mtx, device_get_nameunit((_sc)->sc_dev), \
	    MTX_NETWORK_LOCK, MTX_DEF)
#define WPI_LOCK(_sc)		mtx_lock(&(_sc)->sc_mtx)
#define WPI_UNLOCK(_sc)		mtx_unlock(&(_sc)->sc_mtx)
#define WPI_LOCK_ASSERT(_sc)	mtx_assert(&(_sc)->sc_mtx, MA_OWNED)
#define WPI_LOCK_DESTROY(_sc)	mtx_destroy(&(_sc)->sc_mtx)

#define WPI_RXON_LOCK_INIT(_sc) \
	mtx_init(&(_sc)->rxon_mtx, "lock for wpi_rxon structure", NULL, MTX_DEF)
#define WPI_RXON_LOCK(_sc)		mtx_lock(&(_sc)->rxon_mtx)
#define WPI_RXON_UNLOCK(_sc)		mtx_unlock(&(_sc)->rxon_mtx)
#define WPI_RXON_LOCK_ASSERT(_sc)	mtx_assert(&(_sc)->rxon_mtx, MA_OWNED)
#define WPI_RXON_LOCK_DESTROY(_sc)	mtx_destroy(&(_sc)->rxon_mtx)

#define WPI_TX_LOCK_INIT(_sc) \
	mtx_init(&(_sc)->tx_mtx, "tx path lock", NULL, MTX_DEF)
#define WPI_TX_LOCK(_sc)		mtx_lock(&(_sc)->tx_mtx)
#define WPI_TX_UNLOCK(_sc)		mtx_unlock(&(_sc)->tx_mtx)
#define WPI_TX_LOCK_DESTROY(_sc)	mtx_destroy(&(_sc)->tx_mtx)

#define WPI_NT_LOCK_INIT(_sc) \
	mtx_init(&(_sc)->nt_mtx, "node table lock", NULL, MTX_DEF)
#define WPI_NT_LOCK(_sc)		mtx_lock(&(_sc)->nt_mtx)
#define WPI_NT_UNLOCK(_sc)		mtx_unlock(&(_sc)->nt_mtx)
#define WPI_NT_LOCK_DESTROY(_sc)	mtx_destroy(&(_sc)->nt_mtx)

#define WPI_TXQ_LOCK_INIT(_sc) \
	mtx_init(&(_sc)->txq_mtx, "txq/cmdq lock", NULL, MTX_DEF)
#define WPI_TXQ_LOCK(_sc)		mtx_lock(&(_sc)->txq_mtx)
#define WPI_TXQ_UNLOCK(_sc)		mtx_unlock(&(_sc)->txq_mtx)
#define WPI_TXQ_LOCK_DESTROY(_sc)	mtx_destroy(&(_sc)->txq_mtx)

#define WPI_TXQ_STATE_LOCK_INIT(_sc) \
	mtx_init(&(_sc)->txq_state_mtx, "txq state lock", NULL, MTX_DEF)
#define WPI_TXQ_STATE_LOCK(_sc)		mtx_lock(&(_sc)->txq_state_mtx)
#define WPI_TXQ_STATE_UNLOCK(_sc)	mtx_unlock(&(_sc)->txq_state_mtx)
#define WPI_TXQ_STATE_LOCK_DESTROY(_sc)	mtx_destroy(&(_sc)->txq_state_mtx)
