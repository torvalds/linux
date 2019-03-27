/*	$OpenBSD: if_uathvar.h,v 1.3 2006/09/20 19:47:17 damien Exp $	*/
/*	$FreeBSD$	*/

/*-
 * Copyright (c) 2006
 *	Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2006 Sam Leffler, Errno Consulting
 * Copyright (c) 2008-2009 Weongyo Jeong <weongyo@freebsd.org>
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
	UATH_INTR_RX,
	UATH_INTR_TX,
	UATH_BULK_RX,
	UATH_BULK_TX,
	UATH_N_XFERS = 4,
};

#define	UATH_ID_BSS		2	/* Connection ID  */

#define	UATH_RX_DATA_LIST_COUNT	128
#define	UATH_TX_DATA_LIST_COUNT	16
#define	UATH_CMD_LIST_COUNT	60

#define	UATH_DATA_TIMEOUT	10000
#define	UATH_CMD_TIMEOUT	1000

/* flags for sending firmware commands */
#define	UATH_CMD_FLAG_ASYNC	(1 << 0)
#define	UATH_CMD_FLAG_READ	(1 << 1)
#define	UATH_CMD_FLAG_MAGIC	(1 << 2)

struct uath_rx_radiotap_header {
	struct ieee80211_radiotap_header wr_ihdr;
	u_int64_t	wr_tsf;
	u_int8_t	wr_flags;
	u_int8_t	wr_rate;
	uint16_t	wr_chan_freq;
	uint16_t	wr_chan_flags;
	int8_t		wr_antsignal;
	int8_t		wr_antnoise;
	u_int8_t	wr_antenna;
} __packed __aligned(8);

#define UATH_RX_RADIOTAP_PRESENT (		\
	(1 << IEEE80211_RADIOTAP_TSFT)		| \
	(1 << IEEE80211_RADIOTAP_FLAGS)		| \
	(1 << IEEE80211_RADIOTAP_RATE)		| \
	(1 << IEEE80211_RADIOTAP_ANTENNA)	| \
	(1 << IEEE80211_RADIOTAP_CHANNEL)	| \
	(1 << IEEE80211_RADIOTAP_DBM_ANTSIGNAL)	| \
	(1 << IEEE80211_RADIOTAP_DBM_ANTNOISE)	| \
	0)

struct uath_tx_radiotap_header {
	struct ieee80211_radiotap_header wt_ihdr;
	uint8_t		wt_flags;
	uint8_t		wt_pad;
	uint16_t	wt_chan_freq;
	uint16_t	wt_chan_flags;
} __packed;

#define	UATH_TX_RADIOTAP_PRESENT					\
	((1 << IEEE80211_RADIOTAP_FLAGS) |				\
	 (1 << IEEE80211_RADIOTAP_CHANNEL))

struct uath_data {
	struct uath_softc		*sc;
	uint8_t				*buf;
	uint16_t			buflen;
	struct mbuf			*m;
	struct ieee80211_node		*ni;		/* NB: tx only */
	STAILQ_ENTRY(uath_data)		next;
};
typedef STAILQ_HEAD(, uath_data) uath_datahead;

struct uath_cmd {
	struct uath_softc		*sc;
	uint32_t			flags;
	uint32_t			msgid;
	uint8_t				*buf;
	uint16_t			buflen;
	void				*odata;		/* NB: tx only */
	int				olen;		/* space in odata */
	STAILQ_ENTRY(uath_cmd)		next;
};
typedef STAILQ_HEAD(, uath_cmd) uath_cmdhead;

struct uath_wme_settings {
	uint8_t				aifsn;
	uint8_t				logcwmin;
	uint8_t				logcwmax;
	uint16_t			txop;
	uint8_t				acm;
};

struct uath_devcap {
	uint32_t			targetVersion;
	uint32_t			targetRevision;
	uint32_t			macVersion;
	uint32_t			macRevision;
	uint32_t			phyRevision;
	uint32_t			analog5GhzRevision;
	uint32_t			analog2GhzRevision;
	uint32_t			regDomain;
	uint32_t			regCapBits;
	uint32_t			countryCode;
	uint32_t			keyCacheSize;
	uint32_t			numTxQueues;
	uint32_t			connectionIdMax;
	uint32_t			wirelessModes;
#define	UATH_WIRELESS_MODE_11A		0x01
#define	UATH_WIRELESS_MODE_TURBO	0x02
#define	UATH_WIRELESS_MODE_11B		0x04
#define	UATH_WIRELESS_MODE_11G		0x08
#define	UATH_WIRELESS_MODE_108G		0x10
	uint32_t			chanSpreadSupport;
	uint32_t			compressSupport;
	uint32_t			burstSupport;
	uint32_t			fastFramesSupport;
	uint32_t			chapTuningSupport;
	uint32_t			turboGSupport;
	uint32_t			turboPrimeSupport;
	uint32_t			deviceType;
	uint32_t			wmeSupport;
	uint32_t			low2GhzChan;
	uint32_t			high2GhzChan;
	uint32_t			low5GhzChan;
	uint32_t			high5GhzChan;
	uint32_t			supportCipherWEP;
	uint32_t			supportCipherAES_CCM;
	uint32_t			supportCipherTKIP;
	uint32_t			supportCipherMicAES_CCM;
	uint32_t			supportMicTKIP;
	uint32_t			twiceAntennaGain5G;
	uint32_t			twiceAntennaGain2G;
};

struct uath_stat {
	uint32_t			st_badchunkseqnum;
	uint32_t			st_invalidlen;
	uint32_t			st_multichunk;
	uint32_t			st_toobigrxpkt;
	uint32_t			st_stopinprogress;
	uint32_t			st_crcerr;
	uint32_t			st_phyerr;
	uint32_t			st_decrypt_crcerr;
	uint32_t			st_decrypt_micerr;
	uint32_t			st_decomperr;
	uint32_t			st_keyerr;
	uint32_t			st_err;
	/* CMD/RX/TX queues */
	uint32_t			st_cmd_active;
	uint32_t			st_cmd_inactive;
	uint32_t			st_cmd_pending;
	uint32_t			st_cmd_waiting;
	uint32_t			st_rx_active;
	uint32_t			st_rx_inactive;
	uint32_t			st_tx_active;
	uint32_t			st_tx_inactive;
	uint32_t			st_tx_pending;
};
#define	UATH_STAT_INC(sc, var)		(sc)->sc_stat.var++
#define	UATH_STAT_DEC(sc, var)		(sc)->sc_stat.var--

struct uath_vap {
	struct ieee80211vap		vap;
	int				(*newstate)(struct ieee80211vap *,
					    enum ieee80211_state, int);
};
#define	UATH_VAP(vap)			((struct uath_vap *)(vap))

struct uath_softc {
	struct ieee80211com		sc_ic;
	struct mbufq			sc_snd;
	device_t			sc_dev;
	struct usb_device		*sc_udev;
	void				*sc_cmd_dma_buf;
	void				*sc_tx_dma_buf;
	struct mtx			sc_mtx;
	uint32_t			sc_debug;

	struct uath_stat		sc_stat;
	int				(*sc_newstate)(struct ieee80211com *,
					    enum ieee80211_state, int);

	struct usb_xfer		*sc_xfer[UATH_N_XFERS];
	struct uath_cmd			sc_cmd[UATH_CMD_LIST_COUNT];
	uath_cmdhead			sc_cmd_active;
	uath_cmdhead			sc_cmd_inactive;
	uath_cmdhead			sc_cmd_pending;
	uath_cmdhead			sc_cmd_waiting;
	struct uath_data		sc_rx[UATH_RX_DATA_LIST_COUNT];
	uath_datahead			sc_rx_active;
	uath_datahead			sc_rx_inactive;
	struct uath_data		sc_tx[UATH_TX_DATA_LIST_COUNT];
	uath_datahead			sc_tx_active;
	uath_datahead			sc_tx_inactive;
	uath_datahead			sc_tx_pending;

	uint32_t			sc_msgid;
	uint32_t			sc_seqnum;
	int				sc_tx_timer;
	struct callout			watchdog_ch;
	struct callout			stat_ch;
	/* multi-chunked support  */
	struct mbuf			*sc_intrx_head;
	struct mbuf			*sc_intrx_tail;
	uint8_t				sc_intrx_nextnum;
	uint32_t			sc_intrx_len;
#define	UATH_MAX_INTRX_SIZE		3616

	struct uath_devcap		sc_devcap;
	uint8_t				sc_serial[16];

	/* unsorted  */
	uint32_t			sc_flags;
#define	UATH_FLAG_INVALID		(1 << 1)
#define	UATH_FLAG_INITDONE		(1 << 2)

	struct	uath_rx_radiotap_header	sc_rxtap;
	struct	uath_tx_radiotap_header	sc_txtap;
};

#define	UATH_LOCK(sc)			mtx_lock(&(sc)->sc_mtx)
#define	UATH_UNLOCK(sc)			mtx_unlock(&(sc)->sc_mtx)
#define	UATH_ASSERT_LOCKED(sc)		mtx_assert(&(sc)->sc_mtx, MA_OWNED)

#define	UATH_RESET_INTRX(sc) do {		\
	(sc)->sc_intrx_head = NULL;		\
	(sc)->sc_intrx_tail = NULL;		\
	(sc)->sc_intrx_nextnum = 0;		\
	(sc)->sc_intrx_len = 0;			\
} while (0)
