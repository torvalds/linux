/*	$FreeBSD$	*/

/*-
 * Copyright (c) 2005
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

struct rt2661_rx_radiotap_header {
	struct ieee80211_radiotap_header wr_ihdr;
	uint64_t	wr_tsf;
	uint8_t		wr_flags;
	uint8_t		wr_rate;
	uint16_t	wr_chan_freq;
	uint16_t	wr_chan_flags;
	int8_t		wr_antsignal;
	int8_t		wr_antnoise;
} __packed __aligned(8);

#define RT2661_RX_RADIOTAP_PRESENT					\
	((1 << IEEE80211_RADIOTAP_TSFT) |				\
	 (1 << IEEE80211_RADIOTAP_FLAGS) |				\
	 (1 << IEEE80211_RADIOTAP_RATE) |				\
	 (1 << IEEE80211_RADIOTAP_CHANNEL) |				\
	 (1 << IEEE80211_RADIOTAP_DBM_ANTSIGNAL) |			\
	 (1 << IEEE80211_RADIOTAP_DBM_ANTNOISE))

struct rt2661_tx_radiotap_header {
	struct ieee80211_radiotap_header wt_ihdr;
	uint8_t		wt_flags;
	uint8_t		wt_rate;
	uint16_t	wt_chan_freq;
	uint16_t	wt_chan_flags;
} __packed;

#define RT2661_TX_RADIOTAP_PRESENT					\
	((1 << IEEE80211_RADIOTAP_FLAGS) |				\
	 (1 << IEEE80211_RADIOTAP_RATE) |				\
	 (1 << IEEE80211_RADIOTAP_CHANNEL))

struct rt2661_tx_data {
	bus_dmamap_t		map;
	struct mbuf		*m;
	struct ieee80211_node	*ni;
	uint8_t			rix;
	int8_t			rssi;
};

struct rt2661_tx_ring {
	bus_dma_tag_t		desc_dmat;
	bus_dma_tag_t		data_dmat;
	bus_dmamap_t		desc_map;
	bus_addr_t		physaddr;
	struct rt2661_tx_desc	*desc;
	struct rt2661_tx_data	*data;
	int			count;
	int			queued;
	int			cur;
	int			next;
	int			stat;
};

struct rt2661_rx_data {
	bus_dmamap_t	map;
	struct mbuf	*m;
};

struct rt2661_rx_ring {
	bus_dma_tag_t		desc_dmat;
	bus_dma_tag_t		data_dmat;
	bus_dmamap_t		desc_map;
	bus_addr_t		physaddr;
	struct rt2661_rx_desc	*desc;
	struct rt2661_rx_data	*data;
	int			count;
	int			cur;
	int			next;
};

struct rt2661_vap {
	struct ieee80211vap	ral_vap;

	int			(*ral_newstate)(struct ieee80211vap *,
				    enum ieee80211_state, int);
};
#define	RT2661_VAP(vap)		((struct rt2661_vap *)(vap))

struct rt2661_softc {
	struct ieee80211com		sc_ic;
	struct ieee80211_ratectl_tx_status sc_txs;
	struct mtx			sc_mtx;
	struct mbufq			sc_snd;
	device_t			sc_dev;
	bus_space_tag_t			sc_st;
	bus_space_handle_t		sc_sh;

	struct callout			watchdog_ch;

	int				sc_tx_timer;
	int                             sc_invalid;
	int				sc_debug;
/*
 * The same in both up to here
 * ------------------------------------------------
 */
	
	int                             sc_flags;
#define	RAL_FW_LOADED		0x1
#define	RAL_INPUT_RUNNING	0x2
#define	RAL_RUNNING		0x4
	int				sc_id;
	struct ieee80211_channel	*sc_curchan;

	uint8_t				rf_rev;

	uint8_t				rfprog;
	uint8_t				rffreq;

	struct rt2661_tx_ring		txq[4];
	struct rt2661_tx_ring		mgtq;
	struct rt2661_rx_ring		rxq;

	uint32_t			rf_regs[4];
	int8_t				txpow[38];

	struct {
		uint8_t	reg;
		uint8_t	val;
	}				bbp_prom[16];

	int				hw_radio;
	int				rx_ant;
	int				tx_ant;
	int				nb_ant;
	int				ext_2ghz_lna;
	int				ext_5ghz_lna;
	int				rssi_2ghz_corr;
	int				rssi_5ghz_corr;

	uint8_t				bbp18;
	uint8_t				bbp21;
	uint8_t				bbp22;
	uint8_t				bbp16;
	uint8_t				bbp17;
	uint8_t				bbp64;

	int				dwelltime;

	struct rt2661_rx_radiotap_header sc_rxtap;
	struct rt2661_tx_radiotap_header sc_txtap;
};

int	rt2661_attach(device_t, int);
int	rt2661_detach(void *);
void	rt2661_shutdown(void *);
void	rt2661_suspend(void *);
void	rt2661_resume(void *);
void	rt2661_intr(void *);

#define RAL_LOCK(sc)		mtx_lock(&(sc)->sc_mtx)
#define RAL_LOCK_ASSERT(sc)	mtx_assert(&(sc)->sc_mtx, MA_OWNED)
#define RAL_UNLOCK(sc)		mtx_unlock(&(sc)->sc_mtx)
