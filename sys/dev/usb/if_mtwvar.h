/*	$OpenBSD: if_mtwvar.h,v 1.1 2021/12/20 13:59:02 hastings Exp $	*/
/*
 * Copyright (c) 2008,2009 Damien Bergamini <damien.bergamini@free.fr>
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

#define MTW_MAX_RXSZ			\
	4096
#if 0
	(sizeof (uint32_t) +		\
	 sizeof (struct mtw_rxwi) +	\
	 sizeof (uint16_t) +		\
	 MCLBYTES +			\
	 sizeof (struct mtw_rxd))
#endif
/* NB: "11" is the maximum number of padding bytes needed for Tx */
#define MTW_MAX_TXSZ			\
	(sizeof (struct mtw_txd) +	\
	 sizeof (struct mtw_txwi) +	\
	 MCLBYTES + 11)

#define MTW_TX_TIMEOUT	5000	/* ms */

#define MTW_RX_RING_COUNT	1
#define MTW_TX_RING_COUNT	8

#define MTW_RXQ_COUNT		2
#define MTW_TXQ_COUNT		6

#define MTW_WCID_MAX		8	
#define MTW_AID2WCID(aid)	(1 + ((aid) & 0x7))

struct mtw_rx_radiotap_header {
	struct ieee80211_radiotap_header wr_ihdr;
	uint8_t		wr_flags;
	uint8_t		wr_rate;
	uint16_t	wr_chan_freq;
	uint16_t	wr_chan_flags;
	uint8_t		wr_dbm_antsignal;
	uint8_t		wr_antenna;
	uint8_t		wr_antsignal;
} __packed;

#define MTW_RX_RADIOTAP_PRESENT				\
	(1 << IEEE80211_RADIOTAP_FLAGS |		\
	 1 << IEEE80211_RADIOTAP_RATE |			\
	 1 << IEEE80211_RADIOTAP_CHANNEL |		\
	 1 << IEEE80211_RADIOTAP_DBM_ANTSIGNAL |	\
	 1 << IEEE80211_RADIOTAP_ANTENNA |		\
	 1 << IEEE80211_RADIOTAP_DB_ANTSIGNAL)

struct mtw_tx_radiotap_header {
	struct ieee80211_radiotap_header wt_ihdr;
	uint8_t		wt_flags;
	uint8_t		wt_rate;
	uint16_t	wt_chan_freq;
	uint16_t	wt_chan_flags;
} __packed;

#define MTW_TX_RADIOTAP_PRESENT				\
	(1 << IEEE80211_RADIOTAP_FLAGS |		\
	 1 << IEEE80211_RADIOTAP_RATE |			\
	 1 << IEEE80211_RADIOTAP_CHANNEL)

struct mtw_softc;

struct mtw_tx_data {
	struct mtw_softc	*sc;
	struct usbd_xfer	*xfer;
	uint8_t			*buf;
	uint8_t			qid;
};

struct mtw_rx_data {
	struct mtw_softc	*sc;
	struct usbd_xfer	*xfer;
	uint8_t			*buf;
};

struct mtw_tx_ring {
	struct mtw_tx_data	data[MTW_TX_RING_COUNT];
	struct usbd_pipe	*pipeh;
	int			cur;
	int			queued;
	uint8_t			pipe_no;
};

struct mtw_rx_ring {
	struct mtw_rx_data	data[MTW_RX_RING_COUNT];
	struct usbd_pipe	*pipeh;
	uint8_t			pipe_no;
};

struct mtw_host_cmd {
	void	(*cb)(struct mtw_softc *, void *);
	uint8_t	data[256];
};

struct mtw_cmd_newstate {
	enum ieee80211_state	state;
	int			arg;
};

struct mtw_cmd_key {
	struct ieee80211_key	key;
	struct ieee80211_node	*ni;
};

#define MTW_HOST_CMD_RING_COUNT	32
struct mtw_host_cmd_ring {
	struct mtw_host_cmd	cmd[MTW_HOST_CMD_RING_COUNT];
	int			cur;
	int			next;
	int			queued;
};

struct mtw_node {
	struct ieee80211_node		ni;
	struct ieee80211_ra_node	rn;
	uint8_t				ridx[IEEE80211_RATE_MAXSIZE];
	uint8_t				ctl_ridx[IEEE80211_RATE_MAXSIZE];
};

struct mtw_mcu_tx {
	struct mtw_softc	*sc;
	struct usbd_xfer	*xfer;
	struct usbd_pipe	*pipeh;
	uint8_t			 pipe_no;
	uint8_t			*buf;
	uint8_t			 seq;
};

#define MTW_MCU_IVB_LEN		0x40
struct mtw_ucode_hdr {
	uint32_t		ilm_len;
	uint32_t		dlm_len;
	uint16_t		build_ver;
	uint16_t		fw_ver;
	uint8_t			pad[4];
	char			build_time[16];
} __packed;

struct mtw_ucode {
	struct mtw_ucode_hdr	hdr;
	uint8_t			ivb[MTW_MCU_IVB_LEN];
	uint8_t			data[];
} __packed;

struct mtw_softc {
	struct device			sc_dev;
	struct ieee80211com		sc_ic;
	int				(*sc_newstate)(struct ieee80211com *,
					    enum ieee80211_state, int);
	int				(*sc_srom_read)(struct mtw_softc *,
					    uint16_t, uint16_t *);

	struct usbd_device		*sc_udev;
	struct usbd_interface		*sc_iface;

	uint16_t			asic_ver;
	uint16_t			asic_rev;
	uint16_t			mac_ver;
	uint16_t			mac_rev;
	uint16_t			rf_rev;
	uint8_t				freq;
	uint8_t				ntxchains;
	uint8_t				nrxchains;
	int				fixed_ridx;

	uint8_t				rfswitch;
	uint8_t				ext_2ghz_lna;
	uint8_t				ext_5ghz_lna;
	uint8_t				calib_2ghz;
	uint8_t				calib_5ghz;
	uint8_t				txmixgain_2ghz;
	uint8_t				txmixgain_5ghz;
	int8_t				txpow1[54];
	int8_t				txpow2[54];
	int8_t				txpow3[54];
	int8_t				rssi_2ghz[3];
	int8_t				rssi_5ghz[3];
	uint8_t				lna[4];

	uint8_t				leds;
	uint16_t			led[3];
	uint32_t			txpow20mhz[5];
	uint32_t			txpow40mhz_2ghz[5];
	uint32_t			txpow40mhz_5ghz[5];

	int8_t				bbp_temp;
	uint8_t				rf_freq_offset;
	uint32_t			rf_pa_mode[2];
	int				sc_rf_calibrated;
	int				sc_bw_calibrated;
	int				sc_chan_group;

	struct usb_task			sc_task;

	struct ieee80211_amrr		amrr;
	struct ieee80211_amrr_node	amn;

	struct timeout			scan_to;
	struct timeout			calib_to;

	uint8_t				cmd_seq;

	struct mtw_tx_ring		sc_mcu;
	struct mtw_rx_ring		rxq[MTW_RXQ_COUNT];
	struct mtw_tx_ring		txq[MTW_TXQ_COUNT];
	struct mtw_host_cmd_ring	cmdq;
	uint8_t				qfullmsk;
	int				sc_tx_timer;

#if NBPFILTER > 0
	caddr_t				sc_drvbpf;

	union {
		struct mtw_rx_radiotap_header th;
		uint8_t	pad[64];
	}				sc_rxtapu;
#define sc_rxtap	sc_rxtapu.th
	int				sc_rxtap_len;

	union {
		struct mtw_tx_radiotap_header th;
		uint8_t	pad[64];
	}				sc_txtapu;
#define sc_txtap	sc_txtapu.th
	int				sc_txtap_len;
#endif
	int				sc_key_tasks;
};
