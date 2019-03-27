/*	$FreeBSD$	*/

/*-
 * Copyright (c) 2008 Weongyo Jeong <weongyo@FreeBSD.org>
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

enum {
	URTW_8187B_BULK_RX,
	URTW_8187B_BULK_TX_STATUS,
	URTW_8187B_BULK_TX_BE,
	URTW_8187B_BULK_TX_BK,
	URTW_8187B_BULK_TX_VI,
	URTW_8187B_BULK_TX_VO,
	URTW_8187B_BULK_TX_EP12,
	URTW_8187B_N_XFERS = 7
};

enum {
	URTW_8187L_BULK_RX,
	URTW_8187L_BULK_TX_LOW,
	URTW_8187L_BULK_TX_NORMAL,
	URTW_8187L_N_XFERS = 3
};

/* XXX no definition at net80211?  */
#define URTW_MAX_CHANNELS		15

struct urtw_data {
	struct urtw_softc	*sc;
	uint8_t			*buf;
	uint16_t		buflen;
	struct mbuf		*m;
	struct ieee80211_node	*ni;		/* NB: tx only */
	STAILQ_ENTRY(urtw_data)	next;
};
typedef STAILQ_HEAD(, urtw_data) urtw_datahead;

#define URTW_RX_DATA_LIST_COUNT		4
#define URTW_TX_DATA_LIST_COUNT		16
#define URTW_RX_MAXSIZE			0x9c4
#define URTW_TX_MAXSIZE			0x9c4
#define	URTW_TX_MAXRETRY		11

struct urtw_rx_radiotap_header {
	struct ieee80211_radiotap_header wr_ihdr;
	uint8_t		wr_flags;
	uint8_t		wr_pad;
	uint16_t	wr_chan_freq;
	uint16_t	wr_chan_flags;
	int8_t		wr_dbm_antsignal;
} __packed __aligned(8);

#define URTW_RX_RADIOTAP_PRESENT					\
	((1 << IEEE80211_RADIOTAP_FLAGS) |				\
	 (1 << IEEE80211_RADIOTAP_CHANNEL) |				\
	 (1 << IEEE80211_RADIOTAP_DBM_ANTSIGNAL))

struct urtw_tx_radiotap_header {
	struct ieee80211_radiotap_header wt_ihdr;
	uint8_t		wt_flags;
	uint8_t		wt_pad;
	uint16_t	wt_chan_freq;
	uint16_t	wt_chan_flags;
} __packed;

#define URTW_TX_RADIOTAP_PRESENT					\
	((1 << IEEE80211_RADIOTAP_FLAGS) |				\
	 (1 << IEEE80211_RADIOTAP_CHANNEL))

struct urtw_stats {
	unsigned int			txrates[12];
};

struct urtw_vap {
	struct ieee80211vap		vap;
	int				(*newstate)(struct ieee80211vap *,
					    enum ieee80211_state, int);
};
#define	URTW_VAP(vap)	((struct urtw_vap *)(vap))

struct urtw_softc {
	struct ieee80211com		sc_ic;
	struct mbufq			sc_snd;
	device_t			sc_dev;
	struct usb_device		*sc_udev;
	struct mtx			sc_mtx;
	void				*sc_tx_dma_buf;

	int				sc_debug;
	int				sc_flags;
#define	URTW_INIT_ONCE			(1 << 1)
#define	URTW_RTL8187B			(1 << 2)
#define	URTW_RTL8187B_REV_B		(1 << 3)
#define	URTW_RTL8187B_REV_D		(1 << 4)
#define	URTW_RTL8187B_REV_E		(1 << 5)
#define	URTW_DETACHED			(1 << 6)
#define	URTW_RUNNING			(1 << 7)
	enum ieee80211_state		sc_state;

	int				sc_epromtype;
#define URTW_EEPROM_93C46		0
#define URTW_EEPROM_93C56		1
	uint8_t				sc_crcmon;

	struct ieee80211_channel	*sc_curchan;

	/* for RF  */
	usb_error_t			(*sc_rf_init)(struct urtw_softc *);
	usb_error_t			(*sc_rf_set_chan)(struct urtw_softc *,
					    int);
	usb_error_t			(*sc_rf_set_sens)(struct urtw_softc *,
					    int);
	usb_error_t			(*sc_rf_stop)(struct urtw_softc *);
	uint8_t				sc_rfchip;
	uint32_t			sc_max_sens;
	uint32_t			sc_sens;
	/* for LED  */
	struct usb_callout		sc_led_ch;
	struct task			sc_led_task;
	uint8_t				sc_psr;
	uint8_t				sc_strategy;
#define	URTW_LED_GPIO			1
	uint8_t				sc_gpio_ledon;
	uint8_t				sc_gpio_ledinprogress;
	uint8_t				sc_gpio_ledstate;
	uint8_t				sc_gpio_ledpin;
	uint8_t				sc_gpio_blinktime;
	uint8_t				sc_gpio_blinkstate;
	/* RX/TX */
	struct usb_xfer		*sc_xfer[URTW_8187B_N_XFERS];
#define	URTW_PRIORITY_LOW		0
#define	URTW_PRIORITY_NORMAL		1
#define URTW_DATA_TIMEOUT		10000		/* 10 sec  */
#define	URTW_8187B_TXPIPE_BE		0x6	/* best effort */
#define	URTW_8187B_TXPIPE_BK		0x7	/* background */
#define	URTW_8187B_TXPIPE_VI		0x5	/* video */
#define	URTW_8187B_TXPIPE_VO		0x4	/* voice */
#define	URTW_8187B_TXPIPE_MAX		4
	struct urtw_data		sc_rx[URTW_RX_DATA_LIST_COUNT];
	urtw_datahead			sc_rx_active;
	urtw_datahead			sc_rx_inactive;
	struct urtw_data		sc_tx[URTW_TX_DATA_LIST_COUNT];
	urtw_datahead			sc_tx_active;
	urtw_datahead			sc_tx_inactive;
	urtw_datahead			sc_tx_pending;
	uint8_t				sc_rts_retry;
	uint8_t				sc_tx_retry;
	uint8_t				sc_preamble_mode;
#define	URTW_PREAMBLE_MODE_SHORT	1
#define	URTW_PREAMBLE_MODE_LONG		2
	struct callout			sc_watchdog_ch;
	int				sc_txtimer;
	int				sc_currate;
	/* TX power  */
	uint8_t				sc_txpwr_cck[URTW_MAX_CHANNELS];
	uint8_t				sc_txpwr_cck_base;
	uint8_t				sc_txpwr_ofdm[URTW_MAX_CHANNELS];
	uint8_t				sc_txpwr_ofdm_base;

	uint8_t				sc_acmctl;
	uint64_t			sc_txstatus;	/* only for 8187B */
	struct task			sc_updateslot_task;

	struct urtw_stats		sc_stats;

	struct	urtw_rx_radiotap_header	sc_rxtap;
	struct	urtw_tx_radiotap_header	sc_txtap;
};

#define URTW_LOCK(sc)			mtx_lock(&(sc)->sc_mtx)
#define URTW_UNLOCK(sc)			mtx_unlock(&(sc)->sc_mtx)
#define URTW_ASSERT_LOCKED(sc)		mtx_assert(&(sc)->sc_mtx, MA_OWNED)
