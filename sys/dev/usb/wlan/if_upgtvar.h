/*	$OpenBSD: if_upgtvar.h,v 1.14 2008/02/02 13:48:44 mglocker Exp $ */
/*	$FreeBSD$ */

/*
 * Copyright (c) 2007 Marcus Glocker <mglocker@openbsd.org>
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

struct upgt_softc;

/*
 * General values.
 */
enum {
	UPGT_BULK_RX,
	UPGT_BULK_TX,
	UPGT_N_XFERS = 2,
};

#define UPGT_CONFIG_INDEX		0
#define UPGT_IFACE_INDEX		0
#define UPGT_USB_TIMEOUT		1000
#define UPGT_FIRMWARE_TIMEOUT		10

#define UPGT_MEMADDR_FIRMWARE_START	0x00020000	/* 512 bytes large */
#define UPGT_MEMSIZE_FRAME_HEAD		0x0070
#define UPGT_MEMSIZE_RX			0x3500

#define	UPGT_RX_MAXCOUNT		6
#define	UPGT_TX_MAXCOUNT		128
#define	UPGT_TX_STAT_INTERVAL		5
#define	UPGT_RX_MINSZ			(sizeof(struct upgt_lmac_header) + 4)

/* device flags */
#define UPGT_DEVICE_ATTACHED		(1 << 0)

/* leds */
#define UPGT_LED_OFF			0
#define UPGT_LED_ON			1
#define UPGT_LED_BLINK			2

/*
 * Firmware.
 */
#define UPGT_FW_BLOCK_SIZE		256

#define UPGT_BRA_FWTYPE_SIZE		4
#define UPGT_BRA_FWTYPE_LM86		"LM86"
#define UPGT_BRA_FWTYPE_LM87		"LM87"
enum upgt_fw_type {
	UPGT_FWTYPE_LM86,
	UPGT_FWTYPE_LM87
};

#define UPGT_BRA_TYPE_FW		0x80000001
#define UPGT_BRA_TYPE_VERSION		0x80000002
#define UPGT_BRA_TYPE_DEPIF		0x80000003
#define UPGT_BRA_TYPE_EXPIF		0x80000004
#define UPGT_BRA_TYPE_DESCR		0x80000101
#define UPGT_BRA_TYPE_END		0xff0000ff
struct upgt_fw_bra_option {
	uint32_t			type;
	uint32_t			len;
	uint8_t				data[];
} __packed;

struct upgt_fw_bra_descr {
	uint32_t			unknown1;
	uint32_t			memaddr_space_start;
	uint32_t			memaddr_space_end;
	uint32_t			unknown2;
	uint32_t			unknown3;
	uint8_t				rates[20];
} __packed;

#define UPGT_X2_SIGNATURE_SIZE		4
#define UPGT_X2_SIGNATURE		"x2  "
struct upgt_fw_x2_header {
	uint8_t				signature[4];
	uint32_t			startaddr;
	uint32_t			len;
	uint32_t			crc;
} __packed;

/*
 * EEPROM.
 */
#define UPGT_EEPROM_SIZE		8192
#define UPGT_EEPROM_BLOCK_SIZE		1020

struct upgt_eeprom_header {
	/* 14 bytes */
	uint32_t			magic;
	uint16_t			pad1;
	uint16_t			preamble_len;
	uint32_t			pad2;
	/* data */
} __packed;

#define UPGT_EEPROM_TYPE_END		0x0000
#define UPGT_EEPROM_TYPE_NAME		0x0001
#define UPGT_EEPROM_TYPE_SERIAL		0x0003
#define UPGT_EEPROM_TYPE_MAC		0x0101
#define UPGT_EEPROM_TYPE_HWRX		0x1001
#define UPGT_EEPROM_TYPE_CHIP		0x1002
#define UPGT_EEPROM_TYPE_FREQ3		0x1903
#define UPGT_EEPROM_TYPE_FREQ4		0x1904
#define UPGT_EEPROM_TYPE_FREQ5		0x1905
#define UPGT_EEPROM_TYPE_FREQ6		0x1906
#define UPGT_EEPROM_TYPE_OFF		0xffff
struct upgt_eeprom_option {
	uint16_t			len;
	uint16_t			type;
	uint8_t				data[];
	/* data */
} __packed;

#define UPGT_EEPROM_RX_CONST		0x88
struct upgt_eeprom_option_hwrx {
	uint32_t			pad1;
	uint8_t				rxfilter;
	uint8_t				pad2[15];
} __packed;

struct upgt_eeprom_freq3_header {
	uint8_t				flags;
	uint8_t				elements;
} __packed;

struct upgt_eeprom_freq4_header {
	uint8_t				flags;
	uint8_t				elements;
	uint8_t				settings;
	uint8_t				type;
} __packed;

struct upgt_eeprom_freq4_1 {
	uint16_t			freq;
	uint8_t				data[50];
} __packed;

struct upgt_eeprom_freq4_2 {
	uint16_t			head;
	uint8_t				subtails[4];
	uint8_t				tail;
} __packed;

/*
 * LMAC protocol.
 */
struct upgt_lmac_mem {
	uint32_t			addr;
	uint32_t			chksum;
} __packed;

#define UPGT_H1_FLAGS_TX_MGMT		0x00	/* for TX: mgmt frame */
#define UPGT_H1_FLAGS_TX_NO_CALLBACK	0x01	/* for TX: no USB callback */
#define UPGT_H1_FLAGS_TX_DATA		0x10	/* for TX: data frame */
#define UPGT_H1_TYPE_RX_DATA		0x00	/* 802.11 RX data frame */
#define UPGT_H1_TYPE_RX_DATA_MGMT	0x04	/* 802.11 RX mgmt frame */
#define UPGT_H1_TYPE_TX_DATA		0x40	/* 802.11 TX data frame */
#define UPGT_H1_TYPE_CTRL		0x80	/* control frame */
struct upgt_lmac_h1 {
	/* 4 bytes */
	uint8_t				flags;
	uint8_t				type;
	uint16_t			len;
} __packed;

#define UPGT_H2_TYPE_TX_ACK_NO		0x0000
#define UPGT_H2_TYPE_TX_ACK_YES		0x0001
#define UPGT_H2_TYPE_MACFILTER		0x0000
#define UPGT_H2_TYPE_CHANNEL		0x0001
#define UPGT_H2_TYPE_TX_DONE		0x0008
#define UPGT_H2_TYPE_STATS		0x000a
#define UPGT_H2_TYPE_EEPROM		0x000c
#define UPGT_H2_TYPE_LED		0x000d
#define UPGT_H2_FLAGS_TX_ACK_NO		0x0101
#define UPGT_H2_FLAGS_TX_ACK_YES	0x0707
struct upgt_lmac_h2 {
	/* 8 bytes */
	uint32_t			reqid;
	uint16_t			type;
	uint16_t			flags;
} __packed;

struct upgt_lmac_header {
	/* 12 bytes */
	struct upgt_lmac_h1		header1;
	struct upgt_lmac_h2		header2;
} __packed;

struct upgt_lmac_eeprom {
	/* 16 bytes */
	struct upgt_lmac_h1		header1;
	struct upgt_lmac_h2		header2;
	uint16_t			offset;
	uint16_t			len;
	/* data */
} __packed;

#define UPGT_FILTER_TYPE_NONE		0x0000
#define UPGT_FILTER_TYPE_STA		0x0001
#define UPGT_FILTER_TYPE_IBSS		0x0002
#define UPGT_FILTER_TYPE_HOSTAP		0x0004
#define UPGT_FILTER_TYPE_MONITOR	0x0010
#define UPGT_FILTER_TYPE_RESET		0x0020
#define UPGT_FILTER_UNKNOWN1		0x0002
#define UPGT_FILTER_UNKNOWN2		0x0ca8
#define UPGT_FILTER_UNKNOWN3		0xffff
#define UPGT_FILTER_MONITOR_UNKNOWN1	0x0000
#define UPGT_FILTER_MONITOR_UNKNOWN2	0x0000
#define UPGT_FILTER_MONITOR_UNKNOWN3	0x0000
struct upgt_lmac_filter {
	struct upgt_lmac_h1		header1;
	struct upgt_lmac_h2		header2;
	/* 32 bytes */
	uint16_t			type;
	uint8_t				dst[IEEE80211_ADDR_LEN];
	uint8_t				src[IEEE80211_ADDR_LEN];
	uint16_t			unknown1;
	uint32_t			rxaddr;
	uint16_t			unknown2;
	uint32_t			rxhw;
	uint16_t			unknown3;
	uint32_t			unknown4;
} __packed;

/* frequence 3 data */
struct upgt_lmac_freq3 {
	uint16_t			freq;
	uint8_t				data[6];
} __packed;

/* frequence 4 data */
struct upgt_lmac_freq4 {
	struct upgt_eeprom_freq4_2	cmd;
	uint8_t				pad;
};

/* frequence 6 data */
struct upgt_lmac_freq6 {
	uint16_t			freq;
	uint8_t				data[8];
} __packed;

#define UPGT_CHANNEL_UNKNOWN1		0x0001
#define UPGT_CHANNEL_UNKNOWN2		0x0000
#define UPGT_CHANNEL_UNKNOWN3		0x48
struct upgt_lmac_channel {
	struct upgt_lmac_h1		header1;
	struct upgt_lmac_h2		header2;
	/* 112 bytes */
	uint16_t			unknown1;
	uint16_t			unknown2;
	uint8_t				pad1[20];
	struct upgt_lmac_freq6		freq6;
	uint8_t				settings;
	uint8_t				unknown3;
	uint8_t				freq3_1[4];
	struct upgt_lmac_freq4		freq4[8];
	uint8_t				freq3_2[4];
	uint32_t			pad2;
} __packed;

#define UPGT_LED_MODE_SET		0x0003
#define UPGT_LED_ACTION_OFF		0x0002
#define UPGT_LED_ACTION_ON		0x0003
#define UPGT_LED_ACTION_TMP_DUR		100	/* ms */
struct upgt_lmac_led {
	struct upgt_lmac_h1		header1;
	struct upgt_lmac_h2		header2;
	uint16_t			mode;
	uint16_t			action_fix;
	uint16_t			action_tmp;
	uint16_t			action_tmp_dur;
} __packed;

struct upgt_lmac_stats {
	struct upgt_lmac_h1		header1;
	struct upgt_lmac_h2		header2;
	uint8_t				data[76];
} __packed;

struct upgt_lmac_rx_desc {
	struct upgt_lmac_h1		header1;
	/* 16 bytes */
	uint16_t			freq;
	uint8_t				unknown1;
	uint8_t				rate;
	uint8_t				rssi;
	uint8_t				pad;
	uint16_t			unknown2;
	uint32_t			timestamp;
	uint32_t			unknown3;
	uint8_t				data[];
} __packed;

#define UPGT_TX_DESC_KEY_EXISTS		0x01
struct upgt_lmac_tx_desc_wep {
	uint8_t				key_exists;
	uint8_t				key_len;
	uint8_t				key_val[16];
} __packed;

#define UPGT_TX_DESC_TYPE_BEACON	0x00000000
#define UPGT_TX_DESC_TYPE_PROBE		0x00000001
#define UPGT_TX_DESC_TYPE_MGMT		0x00000002
#define UPGT_TX_DESC_TYPE_DATA		0x00000004
#define UPGT_TX_DESC_PAD3_SIZE		2
struct upgt_lmac_tx_desc {
	struct upgt_lmac_h1		header1;
	struct upgt_lmac_h2		header2;
	uint8_t				rates[8];
	uint16_t			pad1;
	struct upgt_lmac_tx_desc_wep	wep_key;
	uint32_t			type;
	uint32_t			pad2;
	uint32_t			unknown1;
	uint32_t			unknown2;
	uint8_t				pad3[2];
	/* 802.11 frame data */
} __packed;

#define UPGT_TX_DONE_DESC_STATUS_OK	0x0001
struct upgt_lmac_tx_done_desc {
	struct upgt_lmac_h1		header1;
	struct upgt_lmac_h2		header2;
	uint16_t			status;
	uint16_t			rssi;
	uint16_t			seq;
	uint16_t			unknown;
} __packed;

/*
 * USB xfers.
 */
struct upgt_data {
	uint8_t				*buf;
	uint32_t			 buflen;
	struct ieee80211_node		*ni;
	struct mbuf			*m;
	uint32_t			 addr;
	STAILQ_ENTRY(upgt_data)		 next;
};
typedef STAILQ_HEAD(, upgt_data) upgt_datahead;

/*
 * Prism memory.
 */
struct upgt_memory_page {
	uint8_t				used;
	uint32_t			addr;
} __packed;

#define UPGT_MEMORY_MAX_PAGES		8
struct upgt_memory {
	uint8_t				pages;
	struct upgt_memory_page		page[UPGT_MEMORY_MAX_PAGES];
} __packed;

/*
 * BPF
 */
struct upgt_rx_radiotap_header {
	struct ieee80211_radiotap_header wr_ihdr;
	uint8_t		wr_flags;
	uint8_t		wr_rate;
	uint16_t	wr_chan_freq;
	uint16_t	wr_chan_flags;
	int8_t		wr_antsignal;
} __packed __aligned(8);

#define UPGT_RX_RADIOTAP_PRESENT					\
	((1 << IEEE80211_RADIOTAP_FLAGS) |				\
	 (1 << IEEE80211_RADIOTAP_RATE) |				\
	 (1 << IEEE80211_RADIOTAP_CHANNEL) |				\
	 (1 << IEEE80211_RADIOTAP_DB_ANTSIGNAL))

struct upgt_tx_radiotap_header {
	struct ieee80211_radiotap_header wt_ihdr;
	uint8_t		wt_flags;
	uint8_t		wt_rate;
	uint16_t	wt_chan_freq;
	uint16_t	wt_chan_flags;
} __packed;

#define UPGT_TX_RADIOTAP_PRESENT					\
	((1 << IEEE80211_RADIOTAP_FLAGS) |				\
	 (1 << IEEE80211_RADIOTAP_RATE) |				\
	 (1 << IEEE80211_RADIOTAP_CHANNEL))

struct upgt_stat {
	uint32_t		st_tx_active;
	uint32_t		st_tx_inactive;
	uint32_t		st_tx_pending;
};

#define	UPGT_STAT_INC(sc, var)	(sc)->sc_stat.var++
#define	UPGT_STAT_DEC(sc, var)	(sc)->sc_stat.var--

struct upgt_vap {
	struct ieee80211vap	vap;
	int			(*newstate)(struct ieee80211vap *,
				    enum ieee80211_state, int);
};
#define	UPGT_VAP(vap)	((struct upgt_vap *)(vap))

struct upgt_softc {
	struct ieee80211com	 sc_ic;
	struct mbufq		 sc_snd;
	device_t		 sc_dev;
	struct usb_device	*sc_udev;
	void			*sc_rx_dma_buf;
	void			*sc_tx_dma_buf;
	struct mtx		 sc_mtx;
	struct upgt_stat	 sc_stat;
	int			 sc_flags;
#define	UPGT_FLAG_FWLOADED	 (1 << 0)
#define	UPGT_FLAG_INITDONE	 (1 << 1)
#define	UPGT_FLAG_DETACHED	 (1 << 2)
	int			 sc_debug;

	enum ieee80211_state	 sc_state;
	int			 sc_arg;
	int			 sc_led_blink;
	struct callout		 sc_led_ch;
	uint8_t			 sc_cur_rateset[8];

	/* watchdog  */
	int			 sc_tx_timer;
	struct callout		 sc_watchdog_ch;

	/* Firmware.  */
	int			 sc_fw_type;
	/* memory addresses on device */
	uint32_t		 sc_memaddr_frame_start;
	uint32_t		 sc_memaddr_frame_end;
	uint32_t		 sc_memaddr_rx_start;
	struct upgt_memory	 sc_memory;

	/* data which we found in the EEPROM */
	uint8_t			 sc_eeprom[2 * UPGT_EEPROM_SIZE] __aligned(4);
	uint16_t		 sc_eeprom_hwrx;
	struct upgt_lmac_freq3	 sc_eeprom_freq3[IEEE80211_CHAN_MAX];
	struct upgt_lmac_freq4	 sc_eeprom_freq4[IEEE80211_CHAN_MAX][8];
	struct upgt_lmac_freq6	 sc_eeprom_freq6[IEEE80211_CHAN_MAX];
	uint8_t			 sc_eeprom_freq6_settings;

	/* RX/TX  */
	struct usb_xfer	*sc_xfer[UPGT_N_XFERS];
	int			 sc_rx_no;
	int			 sc_tx_no;
	struct upgt_data	 sc_rx_data[UPGT_RX_MAXCOUNT];
	upgt_datahead		 sc_rx_active;
	upgt_datahead		 sc_rx_inactive;
	struct upgt_data	 sc_tx_data[UPGT_TX_MAXCOUNT];
	upgt_datahead		 sc_tx_active;
	upgt_datahead		 sc_tx_inactive;
	upgt_datahead		 sc_tx_pending;

	/* BPF  */
	struct upgt_rx_radiotap_header	sc_rxtap;
	struct upgt_tx_radiotap_header	sc_txtap;
};

#define UPGT_LOCK(sc)		mtx_lock(&(sc)->sc_mtx)
#define UPGT_UNLOCK(sc)		mtx_unlock(&(sc)->sc_mtx)
#define	UPGT_ASSERT_LOCKED(sc)	mtx_assert(&(sc)->sc_mtx, MA_OWNED)
