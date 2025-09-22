/*	$OpenBSD: if_nfereg.h,v 1.22 2007/12/05 08:30:33 jsg Exp $	*/

/*-
 * Copyright (c) 2005 Jonathan Gray <jsg@openbsd.org>
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

#define NFE_PCI_BA		0x10

#define NFE_RX_RING_COUNT	128
#define NFE_TX_RING_COUNT	256

#define NFE_JUMBO_FRAMELEN	9018
#define NFE_JUMBO_MTU		(NFE_JUMBO_FRAMELEN - ETHER_HDR_LEN - ETHER_CRC_LEN)

#define NFE_JBYTES		(NFE_JUMBO_FRAMELEN + ETHER_ALIGN)
#define NFE_JPOOL_COUNT		(NFE_RX_RING_COUNT + 64)
#define NFE_JPOOL_SIZE		(NFE_JPOOL_COUNT * NFE_JBYTES)

#define NFE_MAX_SCATTER		(NFE_TX_RING_COUNT - 2)

#define NFE_IRQ_STATUS		0x000
#define NFE_IRQ_MASK		0x004
#define NFE_SETUP_R6		0x008
#define NFE_IMTIMER		0x00c
#define NFE_MAC_RESET		0x03c
#define NFE_MISC1		0x080
#define NFE_TX_CTL		0x084
#define NFE_TX_STATUS		0x088
#define NFE_RXFILTER		0x08c
#define NFE_RXBUFSZ		0x090
#define NFE_RX_CTL		0x094
#define NFE_RX_STATUS		0x098
#define NFE_RNDSEED		0x09c
#define NFE_SETUP_R1		0x0a0
#define NFE_SETUP_R2		0x0a4
#define NFE_MACADDR_HI		0x0a8
#define NFE_MACADDR_LO		0x0ac
#define NFE_MULTIADDR_HI	0x0b0
#define NFE_MULTIADDR_LO	0x0b4
#define NFE_MULTIMASK_HI	0x0b8
#define NFE_MULTIMASK_LO	0x0bc
#define NFE_PHY_IFACE		0x0c0
#define NFE_TX_RING_ADDR_LO	0x100
#define NFE_RX_RING_ADDR_LO	0x104
#define NFE_RING_SIZE		0x108
#define NFE_TX_UNK		0x10c
#define NFE_LINKSPEED		0x110
#define NFE_SETUP_R5		0x130
#define NFE_SETUP_R3		0x13C
#define NFE_SETUP_R7		0x140
#define NFE_RXTX_CTL		0x144
#define NFE_TX_RING_ADDR_HI	0x148
#define NFE_RX_RING_ADDR_HI	0x14c
#define NFE_PHY_STATUS		0x180
#define NFE_SETUP_R4		0x184
#define NFE_STATUS		0x188
#define NFE_PHY_SPEED		0x18c
#define NFE_PHY_CTL		0x190
#define NFE_PHY_DATA		0x194
#define NFE_WOL_CTL		0x200
#define NFE_PATTERN_CRC		0x204
#define NFE_PATTERN_MASK	0x208
#define NFE_PWR_CAP		0x268
#define NFE_PWR_STATE		0x26c
#define NFE_VTAG_CTL		0x300
#define NFE_PWR2_CTL		0x600

#define NFE_PHY_ERROR		0x00001
#define NFE_PHY_WRITE		0x00400
#define NFE_PHY_BUSY		0x08000
#define NFE_PHYADD_SHIFT	5

#define NFE_MAC_RESET_MAGIC	0x00f3

#define NFE_STATUS_MAGIC	0x140000

#define NFE_R1_MAGIC		0x16070f
#define NFE_R2_MAGIC		0x16
#define NFE_R4_MAGIC		0x08
#define NFE_R6_MAGIC		0x03
#define NFE_WOL_ENABLE		0x1111
#define NFE_RX_START		0x01
#define NFE_TX_START		0x01

#define NFE_IRQ_RXERR		0x0001
#define NFE_IRQ_RX		0x0002
#define NFE_IRQ_RX_NOBUF	0x0004
#define NFE_IRQ_TXERR		0x0008
#define NFE_IRQ_TX_DONE		0x0010
#define NFE_IRQ_TIMER		0x0020
#define NFE_IRQ_LINK		0x0040
#define NFE_IRQ_TXERR2		0x0080
#define NFE_IRQ_TX1		0x0100

#define NFE_IRQ_WANTED							\
	(NFE_IRQ_RXERR | NFE_IRQ_RX_NOBUF | NFE_IRQ_RX |		\
	 NFE_IRQ_TXERR | NFE_IRQ_TXERR2 | NFE_IRQ_TX_DONE |		\
	 NFE_IRQ_LINK)

#define NFE_RXTX_KICKTX		0x0001
#define NFE_RXTX_BIT1		0x0002
#define NFE_RXTX_BIT2		0x0004
#define NFE_RXTX_RESET		0x0010
#define NFE_RXTX_VTAG_STRIP	0x0040
#define NFE_RXTX_VTAG_INSERT	0x0080
#define NFE_RXTX_RXCSUM		0x0400
#define NFE_RXTX_V2MAGIC	0x2100
#define NFE_RXTX_V3MAGIC	0x2200
#define NFE_RXFILTER_MAGIC	0x007f0008
#define NFE_U2M			(1 << 5)
#define NFE_PROMISC		(1 << 7)

/* default interrupt moderation timer of 128us */
#define NFE_IM_DEFAULT	((128 * 100) / 1024)

#define NFE_VTAG_ENABLE		(1 << 13)

#define NFE_PWR_VALID		(1 << 8)
#define NFE_PWR_WAKEUP		(1 << 15)
#define NFE_PWR2_WAKEUP_MASK	0x0f11

#define NFE_MEDIA_SET		0x10000
#define	NFE_MEDIA_1000T		0x00032
#define NFE_MEDIA_100TX		0x00064
#define NFE_MEDIA_10T		0x003e8

#define NFE_PHY_100TX		(1 << 0)
#define NFE_PHY_1000T		(1 << 1)
#define NFE_PHY_HDX		(1 << 8)

#define NFE_MISC1_MAGIC		0x003b0f3c
#define NFE_MISC1_HDX		(1 << 1)

#define NFE_SEED_MASK		0x0003ff00
#define NFE_SEED_10T		0x00007f00
#define NFE_SEED_100TX		0x00002d00
#define NFE_SEED_1000T		0x00007400

/* Rx/Tx descriptor */
struct nfe_desc32 {
	uint32_t	physaddr;
	uint16_t	length;
	uint16_t	flags;
#define NFE_RX_FIXME_V1		0x6004
#define NFE_RX_VALID_V1		(1 << 0)
#define NFE_TX_ERROR_V1		0x7808
#define NFE_TX_LASTFRAG_V1	(1 << 0)
} __packed;

#define NFE_V1_TXERR	"\020"	\
	"\14TXERROR\13UNDERFLOW\12LATECOLLISION\11LOSTCARRIER\10DEFERRED" \
	"\08FORCEDINT\03RETRY\00LASTPACKET"

/* V2 Rx/Tx descriptor */
struct nfe_desc64 {
	uint32_t	physaddr[2];
	uint32_t	vtag;
#define NFE_RX_VTAG		(1 << 16)
#define NFE_TX_VTAG		(1 << 18)
	uint16_t	length;
	uint16_t	flags;
#define NFE_RX_FIXME_V2		0x4300
#define NFE_RX_VALID_V2		(1 << 13)
#define NFE_TX_ERROR_V2		0x5c04
#define NFE_TX_LASTFRAG_V2	(1 << 13)
} __packed;

#define NFE_V2_TXERR	"\020"	\
	"\14FORCEDINT\13LASTPACKET\12UNDERFLOW\10LOSTCARRIER\09DEFERRED\02RETRY"

/* flags common to V1/V2 descriptors */
#define NFE_RX_UDP_CSUMOK	(1 << 10)
#define NFE_RX_TCP_CSUMOK	(1 << 11)
#define NFE_RX_IP_CSUMOK	(1 << 12)
#define NFE_RX_ERROR		(1 << 14)
#define NFE_RX_READY		(1 << 15)
#define NFE_TX_TCP_UDP_CSUM	(1 << 10)
#define NFE_TX_IP_CSUM		(1 << 11)
#define NFE_TX_VALID		(1 << 15)

#define NFE_READ(sc, reg) \
	bus_space_read_4((sc)->sc_memt, (sc)->sc_memh, (reg))

#define NFE_WRITE(sc, reg, val) \
	bus_space_write_4((sc)->sc_memt, (sc)->sc_memh, (reg), (val))
