/*	$OpenBSD: if_uathreg.h,v 1.2 2006/09/18 16:34:23 damien Exp $	*/

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

#define UATH_CONFIG_NO		1
#define UATH_IFACE_INDEX	0

/* all fields are big endian */
struct uath_fwblock {
	uint32_t	flags;
#define UATH_WRITE_BLOCK	(1 << 4)

	uint32_t	len;
#define UATH_MAX_FWBLOCK_SIZE	2048

	uint32_t	total;
	uint32_t	remain;
	uint32_t	rxtotal;
	uint32_t	pad[123];
} __packed;

#define UATH_MAX_RXCMDSZ	512
#define UATH_MAX_TXCMDSZ	512

struct uath_cmd_hdr {
	uint32_t	len;
	uint32_t	code;
#define UATH_CMD_SETUP		0x01
#define UATH_CMD_02		0x02
#define UATH_CMD_READ_MAC	0x03
#define UATH_CMD_WRITE_MAC	0x04
#define UATH_CMD_READ_EEPROM	0x05
#define UATH_CMD_STATS		0x06
#define UATH_CMD_07		0x07
#define UATH_CMD_SHUTDOWN	0x08
#define UATH_CMD_0B		0x0b
#define UATH_CMD_0C		0x0c
#define UATH_CMD_0F		0x0f
#define UATH_NOTIF_STATS	0x10
#define UATH_NOTIF_READY	0x12
#define UATH_NOTIF_TX		0x13
#define UATH_CMD_15		0x15
#define UATH_CMD_SET_LED	0x17
#define UATH_CMD_SET_XLED	0x18
#define UATH_CMD_1B		0x1b
#define UATH_CMD_1E		0x1e
#define UATH_CMD_CRYPTO		0x1d
#define UATH_CMD_SET_STATE	0x20
#define UATH_CMD_SET_BSSID	0x21
#define UATH_CMD_24		0x24
#define UATH_CMD_SET_RATES	0x26
#define UATH_CMD_27		0x27
#define UATH_CMD_2E		0x2e
#define UATH_CMD_31		0x31
#define UATH_CMD_SET_FILTER	0x32
#define UATH_CMD_SET_CHAN	0x34
#define UATH_CMD_RESET		0x35
#define UATH_CMD_SET_QUEUE	0x3a
#define UATH_CMD_RESET_QUEUE	0x3b

	uint32_t	priv;	/* driver private data */
	uint32_t	magic;
	uint32_t	reserved2[4];
} __packed;

struct uath_rx_desc {
	uint32_t	len;
	uint32_t	reserved1[8];
	uint32_t	rssi;
	uint32_t	freq;
	uint32_t	reserved2[5];
} __packed;

#define UATH_MAKECTL(qid, len)	htobe32((qid) << 16 | (len))

struct uath_tx_desc {
	uint32_t	len;
	uint32_t	priv;	/* driver private data */
	uint32_t	type;
#define UATH_TX_DATA	0xe
#define UATH_TX_NULL	0xf

	uint32_t	magic;
	uint32_t	dest;
#define UATH_ID_BSS		2
#define UATH_ID_BROADCAST	0xffffffff

	uint32_t	flags;
#define UATH_TX_NOTIFY	(1 << 24)	/* f/w will send a UATH_NOTIF_TX */

	uint32_t	paylen;
} __packed;

/* structure for command UATH_CMD_SETUP */
struct uath_cmd_setup {
	uint32_t	magic1;
	uint32_t	magic2;
	uint32_t	magic3;
	uint32_t	magic4;
} __packed;

/* structure for commands UATH_CMD_READ_MAC and UATH_CMD_READ_EEPROM */
struct uath_read_mac {
	uint32_t	len;
	uint8_t		data[32];
} __packed;

/* structure for command UATH_CMD_WRITE_MAC */
struct uath_write_mac {
	uint32_t	reg;
	uint32_t	len;
	uint8_t		data[32];
} __packed;

/* structure for command UATH_CMD_0B */
struct uath_cmd_0b {
	uint32_t	code;
	uint32_t	reserved;
	uint32_t	size;
	uint8_t		data[44];
} __packed;

/* structure for command UATH_CMD_0C */
struct uath_cmd_0c {
	uint32_t	magic1;
	uint32_t	magic2;
	uint32_t	magic3;
} __packed;

/* structure for command UATH_CMD_SET_LED */
struct uath_cmd_led {
	uint32_t	which;
#define UATH_LED_LINK		0
#define UATH_LED_ACTIVITY	1

	uint32_t	state;
#define UATH_LED_OFF	0
#define UATH_LED_ON	1
} __packed;

/* structure for command UATH_CMD_SET_XLED */
struct uath_cmd_xled {
	uint32_t	which;
	uint32_t	rate;
	uint32_t	mode;
} __packed;

/* structure for command UATH_CMD_CRYPTO */
struct uath_cmd_crypto {
	uint32_t	keyidx;
#define UATH_DEFAULT_KEY	6

	uint32_t	magic1;
	uint32_t	size;
	uint32_t	reserved1;
	uint32_t	mask;
	uint8_t		addr[IEEE80211_ADDR_LEN];
	uint16_t	reserved2;
	uint32_t	flags;
	uint32_t	reserved3[2];
	uint8_t		key[68];
	uint8_t		magic2[136];
	uint8_t		magic3[136];
} __packed;

/* structure for command UATH_CMD_SET_RATES */
struct uath_cmd_rates {
	uint32_t	magic1;
	uint32_t	reserved;
	uint32_t	size;
	uint8_t		nrates;
#define UATH_MAX_NRATES	30
	uint8_t		rates[UATH_MAX_NRATES];
} __packed;

/* structure for command UATH_CMD_SET_CHAN */
struct uath_set_chan {
	uint32_t	flags;
	uint32_t	freq;
	uint32_t	magic1;
	uint32_t	magic2;
	uint32_t	reserved1;
	uint32_t	magic3;
	uint32_t	reserved2;
} __packed;

/* structure for command UATH_CMD_SET_QUEUE */
struct uath_qinfo {
	uint32_t	qid;
#define UATH_AC_TO_QID(ac)	(ac)	/* id function */

	uint32_t	size;
	uint32_t	ac;
	uint32_t	aifsn;
	uint32_t	logcwmin;
	uint32_t	logcwmax;
	uint32_t	txop;
	uint32_t	acm;
	uint32_t	magic1;
	uint32_t	magic2;
} __packed;

/* structure for command UATH_CMD_31 */
struct uath_cmd_31 {
	uint32_t	magic1;
	uint32_t	magic2;
} __packed;

/* structure for command UATH_CMD_SET_FILTER */
struct uath_cmd_filter {
	uint32_t	filter;
	uint32_t	flags;
} __packed;

/* structure for command UATH_CMD_SET_BSSID */
struct uath_cmd_bssid {
	uint32_t	reserved1;
	uint32_t	flags1;
	uint32_t	flags2;
	uint32_t	reserved2;
	uint32_t	len;
	uint8_t		bssid[IEEE80211_ADDR_LEN];
} __packed;


#define UATH_EEPROM_MACADDR	0x0b
#define UATH_EEPROM_RXBUFSZ	0x0f

#define UATH_MAX_TXBUFSZ	\
	(sizeof (uint32_t) + sizeof (struct uath_tx_desc) + IEEE80211_MAX_LEN)

#define UATH_MIN_RXBUFSZ						\
	(((sizeof (uint32_t) + sizeof (struct ieee80211_frame_min) +	\
	   sizeof (struct uath_rx_desc)) + 3) & ~3)
