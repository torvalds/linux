/*	$OpenBSD: if_uathvar.h,v 1.7 2013/04/15 09:23:01 mglocker Exp $	*/

/*-
 * Copyright (c) 2006
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

#define UATH_TX_DATA_LIST_COUNT	8	/* 16 */
#define UATH_TX_CMD_LIST_COUNT	8	/* 30 */

/* XXX ehci will panic on abort_pipe if set to anything > 1 */
#define UATH_RX_DATA_LIST_COUNT	1	/* 128 */
#define UATH_RX_CMD_LIST_COUNT	1	/* 30 */

#define UATH_DATA_TIMEOUT	10000
#define UATH_CMD_TIMEOUT	1000

struct uath_rx_radiotap_header {
	struct ieee80211_radiotap_header wr_ihdr;
	uint8_t		wr_flags;
	uint16_t	wr_chan_freq;
	uint16_t	wr_chan_flags;
	int8_t		wr_dbm_antsignal;
} __packed;

#define UATH_RX_RADIOTAP_PRESENT					\
	((1 << IEEE80211_RADIOTAP_FLAGS) |				\
	 (1 << IEEE80211_RADIOTAP_CHANNEL) |				\
	 (1 << IEEE80211_RADIOTAP_DBM_ANTSIGNAL))

struct uath_tx_radiotap_header {
	struct ieee80211_radiotap_header wt_ihdr;
	uint8_t		wt_flags;
	uint16_t	wt_chan_freq;
	uint16_t	wt_chan_flags;
} __packed;

#define UATH_TX_RADIOTAP_PRESENT					\
	((1 << IEEE80211_RADIOTAP_FLAGS) |				\
	 (1 << IEEE80211_RADIOTAP_CHANNEL))

struct uath_tx_data {
	struct uath_softc	*sc;
	struct usbd_xfer	*xfer;
	uint8_t			*buf;
	struct ieee80211_node	*ni;
};

struct uath_rx_data {
	struct uath_softc	*sc;
	struct usbd_xfer	*xfer;
	uint8_t			*buf;
	struct mbuf		*m;
};

struct uath_tx_cmd {
	struct uath_softc	*sc;
	struct usbd_xfer	*xfer;
	uint8_t			*buf;
	void			*odata;
};

struct uath_rx_cmd {
	struct uath_softc	*sc;
	struct usbd_xfer	*xfer;
	uint8_t			*buf;
};

struct uath_wme_settings {
	uint8_t		aifsn;
	uint8_t		logcwmin;
	uint8_t		logcwmax;
	uint16_t	txop;
#define UATH_TXOP_TO_US(txop)	((txop) << 5)

	uint8_t		acm;
};

/* condvars */
#define UATH_COND_INIT(sc)	((caddr_t)sc + 1)

/* flags for sending firmware commands */
#define UATH_CMD_FLAG_ASYNC	(1 << 0)
#define UATH_CMD_FLAG_READ	(1 << 1)
#define UATH_CMD_FLAG_MAGIC	(1 << 2)

struct uath_softc {
	struct device			sc_dev;
	struct ieee80211com		sc_ic;
	int				(*sc_newstate)(struct ieee80211com *,
					    enum ieee80211_state, int);

	struct uath_tx_data		tx_data[UATH_TX_DATA_LIST_COUNT];
	struct uath_rx_data		rx_data[UATH_RX_DATA_LIST_COUNT];

	struct uath_tx_cmd		tx_cmd[UATH_TX_CMD_LIST_COUNT];
	struct uath_rx_cmd		rx_cmd[UATH_RX_CMD_LIST_COUNT];

	int				sc_flags;

	int				data_idx;
	int				cmd_idx;
	int				tx_queued;

	struct usbd_device		*sc_udev;
	struct usbd_device		*sc_uhub;
	int				sc_port;

	struct usbd_interface		*sc_iface;

	struct usbd_pipe		*data_tx_pipe;
	struct usbd_pipe		*data_rx_pipe;
	struct usbd_pipe		*cmd_tx_pipe;
	struct usbd_pipe		*cmd_rx_pipe;

	enum ieee80211_state		sc_state;
	int				sc_arg;
	struct usb_task			sc_task;

	struct timeout			scan_to;
	struct timeout			stat_to;

	int				sc_tx_timer;

	int				rxbufsz;

#if NBPFILTER > 0
	caddr_t				sc_drvbpf;

	union {
		struct	uath_rx_radiotap_header th;
		uint8_t	pad[64];
	}				sc_rxtapu;
#define sc_rxtap			sc_rxtapu.th
	int				sc_rxtap_len;

	union {
		struct	uath_tx_radiotap_header th;
		uint8_t	pad[64];
	}				sc_txtapu;
#define sc_txtap			sc_txtapu.th
	int				sc_txtap_len;
#endif
};
