/*-
 * Copyright (c) 2010 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2016 Andriy Voskoboinyk <avos@FreeBSD.org>
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
 *
 * $OpenBSD: if_urtwnreg.h,v 1.3 2010/11/16 18:02:59 damien Exp $
 * $FreeBSD$
 */

#ifndef IF_RTWNREG_H
#define IF_RTWNREG_H

#define R92C_MIN_TX_PWR		0x00
#define R92C_MAX_TX_PWR		0x3f

#define R92C_H2C_NBOX		4


/* Common part of Tx descriptor (named only!). */
struct rtwn_tx_desc_common {
	uint16_t	pktlen;
	uint8_t		offset;
	uint8_t		flags0;
#define RTWN_FLAGS0_OWN	0x80

	uint32_t	txdw1;
/* NB: qsel is shared too; however, it looks better at the lower level */
#define RTWN_TXDW1_CIPHER_M	0x00c00000
#define RTWN_TXDW1_CIPHER_S	22
#define RTWN_TXDW1_CIPHER_NONE	0
#define RTWN_TXDW1_CIPHER_RC4	1
#define RTWN_TXDW1_CIPHER_SM4	2
#define RTWN_TXDW1_CIPHER_AES	3

	uint32_t	reserved[5];

	union txdw7_shared {
		uint16_t	usb_checksum;
		uint16_t	pci_txbufsize;
	} txdw7;
} __packed __attribute__((aligned(4)));

/* Common part of Rx descriptor. */
struct rtwn_rx_stat_common {
	uint32_t	rxdw0;
#define RTWN_RXDW0_PKTLEN_M	0x00003fff
#define RTWN_RXDW0_PKTLEN_S	0
#define RTWN_RXDW0_CRCERR	0x00004000
#define RTWN_RXDW0_ICVERR	0x00008000
#define RTWN_RXDW0_INFOSZ_M	0x000f0000
#define RTWN_RXDW0_INFOSZ_S	16
#define RTWN_RXDW0_CIPHER_M	0x00700000
#define RTWN_RXDW0_CIPHER_S	20
#define RTWN_RXDW0_QOS		0x00800000
#define RTWN_RXDW0_SHIFT_M	0x03000000
#define RTWN_RXDW0_SHIFT_S	24
#define RTWN_RXDW0_PHYST	0x04000000
#define RTWN_RXDW0_SWDEC	0x08000000
#define RTWN_RXDW0_LS		0x10000000
#define RTWN_RXDW0_FS		0x20000000
#define RTWN_RXDW0_EOR		0x40000000
#define RTWN_RXDW0_OWN		0x80000000

	uint32_t	rxdw1;
#define RTWN_RXDW1_AMSDU	0x00002000
#define RTWN_RXDW1_MC		0x40000000
#define RTWN_RXDW1_BC		0x80000000

	uint32_t	rxdw2;
	uint32_t	rxdw3;
#define RTWN_RXDW3_HTC		0x00000400
#define RTWN_RXDW3_BSSID01_FIT_M 0x00003000
#define RTWN_RXDW3_BSSID01_FIT_S 12

	uint32_t	rxdw4;
	uint32_t	tsf_low;
} __packed __attribute__((aligned(4)));

/* Rx descriptor for PCIe devices. */
struct rtwn_rx_stat_pci {
	uint32_t	rxdw0;
	uint32_t	rxdw1;
	uint32_t	rxdw2;
	uint32_t	rxdw3;
	uint32_t	rxdw4;
	uint32_t	tsf_low;

	uint32_t	rxbufaddr;
	uint32_t	rxbufaddr64;
} __packed __attribute__((aligned(4)));

/*
 * Macros to access subfields in registers.
 */
/* Mask and Shift (getter). */
#define MS(val, field)							\
	(((val) & field##_M) >> field##_S)

/* Shift and Mask (setter). */
#define SM(field, val)							\
	(((val) << field##_S) & field##_M)

/* Rewrite. */
#define RW(var, field, val)						\
	(((var) & ~field##_M) | SM(field, val))


#define RTWN_MAX_CONDITIONS	3

/*
 * Structure for MAC initialization values.
 */
struct rtwn_mac_prog {
	uint16_t	reg;
	uint8_t		val;
};

/*
 * Structure for baseband initialization values.
 */
struct rtwn_bb_prog {
	int		count;
	const uint16_t	*reg;
	const uint32_t	*val;
	const uint8_t	cond[RTWN_MAX_CONDITIONS];
	const struct rtwn_bb_prog *next;
};

struct rtwn_agc_prog {
	int		count;
	const uint32_t	*val;
	const uint8_t	cond[RTWN_MAX_CONDITIONS];
	const struct rtwn_agc_prog *next;
};

/*
 * Structure for RF initialization values.
 */
struct rtwn_rf_prog {
	int		count;
	const uint8_t	*reg;
	const uint32_t	*val;
	const uint8_t	cond[RTWN_MAX_CONDITIONS];
	const struct rtwn_rf_prog *next;
};


/* XXX move to net80211. */
static __inline int
rtwn_chan2centieee(const struct ieee80211_channel *c)
{
	int chan;

	chan = c->ic_ieee;
	if (c->ic_extieee != 0)
		chan = (chan + c->ic_extieee) / 2;

	return (chan);
}

#endif	/* IF_RTWNREG_H */
