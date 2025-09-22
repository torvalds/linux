/*	$OpenBSD: rt2860var.h,v 1.26 2020/10/11 07:05:28 mpi Exp $	*/

/*-
 * Copyright (c) 2007
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

#define RT2860_MAX_SCATTER	15
#define RT2860_MAX_SCATTER_TXD	(1 + (RT2860_MAX_SCATTER / 2))

#define RT2860_RX_RING_COUNT	128
#define RT2860_TX_RING_COUNT	64
#define RT2860_TX_RING_MAX	(RT2860_TX_RING_COUNT - 1)
#define RT2860_TX_RING_ONEMORE	(RT2860_TX_RING_MAX - RT2860_MAX_SCATTER_TXD)
#define RT2860_TX_POOL_COUNT	(RT2860_TX_RING_COUNT * 2)

/* HW supports up to 255 STAs */
#define RT2860_WCID_MAX		254
#define RT2860_AID2WCID(aid)	((aid) & 0xff)

struct rt2860_rx_radiotap_header {
	struct ieee80211_radiotap_header wr_ihdr;
	uint8_t		wr_flags;
	uint8_t		wr_rate;
	uint16_t	wr_chan_freq;
	uint16_t	wr_chan_flags;
	uint8_t		wr_dbm_antsignal;
	uint8_t		wr_antenna;
	uint8_t		wr_antsignal;
} __packed;

#define RT2860_RX_RADIOTAP_PRESENT			\
	(1 << IEEE80211_RADIOTAP_FLAGS |		\
	 1 << IEEE80211_RADIOTAP_RATE |			\
	 1 << IEEE80211_RADIOTAP_CHANNEL |		\
	 1 << IEEE80211_RADIOTAP_DBM_ANTSIGNAL |	\
	 1 << IEEE80211_RADIOTAP_ANTENNA |		\
	 1 << IEEE80211_RADIOTAP_DB_ANTSIGNAL)

struct rt2860_tx_radiotap_header {
	struct ieee80211_radiotap_header wt_ihdr;
	uint8_t		wt_flags;
	uint8_t		wt_rate;
	uint16_t	wt_chan_freq;
	uint16_t	wt_chan_flags;
} __packed;

#define RT2860_TX_RADIOTAP_PRESENT			\
	(1 << IEEE80211_RADIOTAP_FLAGS |		\
	 1 << IEEE80211_RADIOTAP_RATE |			\
	 1 << IEEE80211_RADIOTAP_CHANNEL)

struct rt2860_tx_data {
	struct rt2860_txwi		*txwi;
	struct mbuf			*m;
	struct ieee80211_node		*ni;
	bus_dmamap_t			map;
	bus_addr_t			paddr;
	SLIST_ENTRY(rt2860_tx_data)	next;
};

struct rt2860_tx_ring {
	struct rt2860_txd	*txd;
	bus_addr_t		paddr;
	bus_dmamap_t		map;
	bus_dma_segment_t	seg;
	struct rt2860_tx_data	*data[RT2860_TX_RING_COUNT];
	int			cur;
	int			next;
	int			queued;
};

struct rt2860_rx_data {
	struct mbuf	*m;
	bus_dmamap_t	map;
};

struct rt2860_rx_ring {
	struct rt2860_rxd	*rxd;
	bus_addr_t		paddr;
	bus_dmamap_t		map;
	bus_dma_segment_t	seg;
	unsigned int		cur;	/* must be unsigned */
	struct rt2860_rx_data	data[RT2860_RX_RING_COUNT];
};

struct rt2860_node {
	struct ieee80211_node	ni;
	uint8_t			wcid;
	uint8_t			ridx[IEEE80211_RATE_MAXSIZE];
	uint8_t			ctl_ridx[IEEE80211_RATE_MAXSIZE];
};

struct rt2860_softc {
	struct device			sc_dev;

	struct ieee80211com		sc_ic;
	int				(*sc_newstate)(struct ieee80211com *,
					    enum ieee80211_state, int);
	struct ieee80211_amrr		amrr;

	int				(*sc_enable)(struct rt2860_softc *);
	void				(*sc_disable)(struct rt2860_softc *);

	bus_dma_tag_t			sc_dmat;
	bus_space_tag_t			sc_st;
	bus_space_handle_t		sc_sh;

	uint16_t			(*sc_srom_read)(struct rt2860_softc *,
					    uint16_t);

	int				sc_flags;
#define RT2860_ENABLED		(1 << 0)
#define RT2860_ADVANCED_PS	(1 << 1)
#define RT2860_PCIE		(1 << 2)

	uint32_t			sc_ic_flags;
	int				fixed_ridx;

	u_char				*ucode;
	size_t				ucsize;

	struct rt2860_tx_ring		txq[6];
	struct rt2860_rx_ring		rxq;

	SLIST_HEAD(, rt2860_tx_data)	data_pool;
	struct rt2860_tx_data		data[RT2860_TX_POOL_COUNT];
	bus_dmamap_t			txwi_map;
	bus_dma_segment_t		txwi_seg;
	caddr_t				txwi_vaddr;

	int				sc_tx_timer;
	int				mgtqid;
	uint8_t				qfullmsk;

	uint16_t			mac_ver;
	uint16_t			mac_rev;
	uint16_t			rf_rev;
	uint8_t				freq;
	uint8_t				ntxchains;
	uint8_t				nrxchains;
	uint8_t				pslevel;
	int8_t				txpow1[54];
	int8_t				txpow2[54];
	int8_t				rssi_2ghz[3];
	int8_t				rssi_5ghz[3];
	uint8_t				lna[4];
	uint8_t				rf24_20mhz;
	uint8_t				rf24_40mhz;
	uint8_t				patch_dac;
	uint8_t				rfswitch;
	uint8_t				ext_2ghz_lna;
	uint8_t				ext_5ghz_lna;
	uint8_t				calib_2ghz;
	uint8_t				calib_5ghz;
	uint8_t				txmixgain_2ghz;
	uint8_t				txmixgain_5ghz;
	uint8_t				tssi_2ghz[9];
	uint8_t				tssi_5ghz[9];
	uint8_t				step_2ghz;
	uint8_t				step_5ghz;
	struct {
		uint8_t	reg;
		uint8_t	val;
	}				bbp[8], rf[10];
	uint8_t				leds;
	uint16_t			led[3];
	uint32_t			txpow20mhz[5];
	uint32_t			txpow40mhz_2ghz[5];
	uint32_t			txpow40mhz_5ghz[5];

	struct ieee80211_amrr_node	amn[RT2860_WCID_MAX + 1];

#if NBPFILTER > 0
	caddr_t				sc_drvbpf;
	union {
		struct rt2860_rx_radiotap_header th;
		uint8_t pad[64];
	}				sc_rxtapu;
#define sc_rxtap			sc_rxtapu.th
	int				sc_rxtap_len;

	union {
		struct rt2860_tx_radiotap_header th;
		uint8_t pad[64];
	}				sc_txtapu;
#define sc_txtap			sc_txtapu.th
	int				sc_txtap_len;
#endif
};

int	rt2860_attach(void *, int);
int	rt2860_detach(void *);
void	rt2860_suspend(void *);
void	rt2860_wakeup(void *);
int	rt2860_intr(void *);
