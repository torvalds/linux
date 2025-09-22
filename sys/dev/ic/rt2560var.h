/*	$OpenBSD: rt2560var.h,v 1.11 2013/12/06 21:03:03 deraadt Exp $  */

/*-
 * Copyright (c) 2005, 2006
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

struct rt2560_rx_radiotap_header {
	struct ieee80211_radiotap_header wr_ihdr;
	uint64_t	wr_tsf;
	uint8_t		wr_flags;
	uint8_t		wr_rate;
	uint16_t	wr_chan_freq;
	uint16_t	wr_chan_flags;
	uint8_t		wr_antenna;
	uint8_t		wr_antsignal;
} __packed;

#define RT2560_RX_RADIOTAP_PRESENT					\
	((1 << IEEE80211_RADIOTAP_TSFT) |				\
	 (1 << IEEE80211_RADIOTAP_FLAGS) |				\
	 (1 << IEEE80211_RADIOTAP_RATE) |				\
	 (1 << IEEE80211_RADIOTAP_CHANNEL) |				\
	 (1 << IEEE80211_RADIOTAP_ANTENNA) |				\
	 (1 << IEEE80211_RADIOTAP_DB_ANTSIGNAL))

struct rt2560_tx_radiotap_header {
	struct ieee80211_radiotap_header wt_ihdr;
	uint8_t		wt_flags;
	uint8_t		wt_rate;
	uint16_t	wt_chan_freq;
	uint16_t	wt_chan_flags;
	uint8_t		wt_antenna;
} __packed;

#define RT2560_TX_RADIOTAP_PRESENT					\
	((1 << IEEE80211_RADIOTAP_FLAGS) |				\
	 (1 << IEEE80211_RADIOTAP_RATE) |				\
	 (1 << IEEE80211_RADIOTAP_CHANNEL) |				\
	 (1 << IEEE80211_RADIOTAP_ANTENNA))

struct rt2560_tx_data {
	bus_dmamap_t			map;
	struct mbuf			*m;
	struct ieee80211_node		*ni;
};

struct rt2560_tx_ring {
	bus_dmamap_t		map;
	bus_dma_segment_t	seg;
	bus_addr_t		physaddr;
	struct rt2560_tx_desc	*desc;
	struct rt2560_tx_data	*data;
	int			count;
	int			queued;
	int			cur;
	int			next;
	int			cur_encrypt;
	int			next_encrypt;
};

struct rt2560_rx_data {
	bus_dmamap_t	map;
	struct mbuf	*m;
	int		drop;
};

struct rt2560_rx_ring {
	bus_dmamap_t		map;
	bus_dma_segment_t	seg;
	bus_addr_t		physaddr;
	struct rt2560_rx_desc	*desc;
	struct rt2560_rx_data	*data;
	int			count;
	int			cur;
	int			next;
	int			cur_decrypt;
};

struct rt2560_node {
	struct ieee80211_node		ni;
	struct ieee80211_amrr_node	amn;
};

struct rt2560_softc {
	struct device		sc_dev;

	struct ieee80211com	sc_ic;
	int			(*sc_newstate)(struct ieee80211com *,
				    enum ieee80211_state, int);
	struct ieee80211_amrr	amrr;

	int			(*sc_enable)(struct rt2560_softc *);
	void			(*sc_disable)(struct rt2560_softc *);

	bus_dma_tag_t		sc_dmat;
	bus_space_tag_t		sc_st;
	bus_space_handle_t	sc_sh;

	struct timeout		scan_to;
	struct timeout		amrr_to;

	int			sc_flags;
#define RT2560_ENABLED		(1 << 0)
#define RT2560_UPDATE_SLOT	(1 << 1)
#define RT2560_SET_SLOTTIME	(1 << 2)
#define RT2560_PRIO_OACTIVE	(1 << 3)
#define RT2560_DATA_OACTIVE	(1 << 4)

	int			sc_tx_timer;

	uint32_t		asic_rev;
	uint8_t			rf_rev;

	struct rt2560_tx_ring	txq;
	struct rt2560_tx_ring	prioq;
	struct rt2560_tx_ring	atimq;
	struct rt2560_tx_ring	bcnq;
	struct rt2560_rx_ring	rxq;

	uint32_t		rf_regs[4];
	uint8_t			txpow[14];

	struct {
		uint8_t	reg;
		uint8_t	val;
	}			bbp_prom[16];

	int			led_mode;
	int			hw_radio;
	int			rx_ant;
	int			tx_ant;
	int			nb_ant;

	uint8_t			*erp;

#if NBPFILTER > 0
	caddr_t			sc_drvbpf;

	union {
		struct rt2560_rx_radiotap_header th;
		uint8_t	pad[64];
	}			sc_rxtapu;
#define sc_rxtap		sc_rxtapu.th
	int			sc_rxtap_len;

	union {
		struct rt2560_tx_radiotap_header th;
		uint8_t	pad[64];
	}			sc_txtapu;
#define sc_txtap		sc_txtapu.th
	int			sc_txtap_len;
#endif
};

int	rt2560_attach(void *, int);
int	rt2560_detach(void *);
void	rt2560_suspend(void *);
void	rt2560_wakeup(void *);
int	rt2560_intr(void *);
