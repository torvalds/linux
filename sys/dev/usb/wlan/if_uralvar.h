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

#define RAL_TX_LIST_COUNT	8
#define RAL_TX_MINFREE		2

#define URAL_SCAN_START         1
#define URAL_SCAN_END           2
#define URAL_SET_CHANNEL        3


struct ural_rx_radiotap_header {
	struct ieee80211_radiotap_header wr_ihdr;
	uint8_t		wr_flags;
	uint8_t		wr_rate;
	uint16_t	wr_chan_freq;
	uint16_t	wr_chan_flags;
	int8_t		wr_antsignal;
	int8_t		wr_antnoise;
	uint8_t		wr_antenna;
} __packed __aligned(8);

#define RAL_RX_RADIOTAP_PRESENT						\
	((1 << IEEE80211_RADIOTAP_FLAGS) |				\
	 (1 << IEEE80211_RADIOTAP_RATE) |				\
	 (1 << IEEE80211_RADIOTAP_CHANNEL) |				\
	 (1 << IEEE80211_RADIOTAP_ANTENNA) |				\
	 (1 << IEEE80211_RADIOTAP_DBM_ANTSIGNAL) |			\
	 (1 << IEEE80211_RADIOTAP_DBM_ANTNOISE))

struct ural_tx_radiotap_header {
	struct ieee80211_radiotap_header wt_ihdr;
	uint8_t		wt_flags;
	uint8_t		wt_rate;
	uint16_t	wt_chan_freq;
	uint16_t	wt_chan_flags;
	uint8_t		wt_antenna;
} __packed;

#define RAL_TX_RADIOTAP_PRESENT						\
	((1 << IEEE80211_RADIOTAP_FLAGS) |				\
	 (1 << IEEE80211_RADIOTAP_RATE) |				\
	 (1 << IEEE80211_RADIOTAP_CHANNEL) |				\
	 (1 << IEEE80211_RADIOTAP_ANTENNA))

struct ural_softc;

struct ural_tx_data {
	STAILQ_ENTRY(ural_tx_data)	next;
	struct ural_softc		*sc;
	struct ural_tx_desc		desc;
	struct mbuf			*m;
	struct ieee80211_node		*ni;
	int				rate;
};
typedef STAILQ_HEAD(, ural_tx_data) ural_txdhead;

struct ural_vap {
	struct ieee80211vap		vap;

	struct usb_callout		ratectl_ch;
	struct task			ratectl_task;

	int				(*newstate)(struct ieee80211vap *,
					    enum ieee80211_state, int);
};
#define	URAL_VAP(vap)	((struct ural_vap *)(vap))

enum {
	URAL_BULK_WR,
	URAL_BULK_RD,
	URAL_N_TRANSFER = 2,
};

struct ural_softc {
	struct ieee80211com		sc_ic;
	struct ieee80211_ratectl_tx_stats sc_txs;
	struct mbufq			sc_snd;
	device_t			sc_dev;
	struct usb_device		*sc_udev;

	uint32_t			asic_rev;
	uint8_t				rf_rev;

	struct usb_xfer			*sc_xfer[URAL_N_TRANSFER];

	struct ural_tx_data		tx_data[RAL_TX_LIST_COUNT];
	ural_txdhead			tx_q;
	ural_txdhead			tx_free;
	int				tx_nfree;
	struct ural_rx_desc		sc_rx_desc;

	struct mtx			sc_mtx;

	uint16_t			sta[11];
	uint32_t			rf_regs[4];
	uint8_t				txpow[14];
	u_int				sc_detached:1,
					sc_running:1;

	uint8_t				sc_bssid[IEEE80211_ADDR_LEN];

	struct {
		uint8_t			val;
		uint8_t			reg;
	} __packed			bbp_prom[16];

	int				led_mode;
	int				hw_radio;
	int				rx_ant;
	int				tx_ant;
	int				nb_ant;

	struct ural_rx_radiotap_header	sc_rxtap;
	struct ural_tx_radiotap_header	sc_txtap;
};

#define RAL_LOCK(sc)		mtx_lock(&(sc)->sc_mtx)
#define RAL_UNLOCK(sc)		mtx_unlock(&(sc)->sc_mtx)
#define RAL_LOCK_ASSERT(sc, t)	mtx_assert(&(sc)->sc_mtx, t)
