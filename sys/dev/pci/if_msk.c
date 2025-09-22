/*	$OpenBSD: if_msk.c,v 1.145 2024/08/31 16:23:09 deraadt Exp $	*/

/*
 * Copyright (c) 1997, 1998, 1999, 2000
 *	Bill Paul <wpaul@ctr.columbia.edu>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: /c/ncvs/src/sys/pci/if_sk.c,v 1.20 2000/04/22 02:16:37 wpaul Exp $
 */

/*
 * Copyright (c) 2003 Nathan L. Binkert <binkertn@umich.edu>
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

/*
 * SysKonnect SK-NET gigabit ethernet driver for FreeBSD. Supports
 * the SK-984x series adapters, both single port and dual port.
 * References:
 * 	The XaQti XMAC II datasheet,
 * http://www.freebsd.org/~wpaul/SysKonnect/xmacii_datasheet_rev_c_9-29.pdf
 *	The SysKonnect GEnesis manual, http://www.syskonnect.com
 *
 * Note: XaQti has been acquired by Vitesse, and Vitesse does not have the
 * XMAC II datasheet online. I have put my copy at people.freebsd.org as a
 * convenience to others until Vitesse corrects this problem:
 *
 * http://people.freebsd.org/~wpaul/SysKonnect/xmacii_datasheet_rev_c_9-29.pdf
 *
 * Written by Bill Paul <wpaul@ee.columbia.edu>
 * Department of Electrical Engineering
 * Columbia University, New York City
 */

/*
 * The SysKonnect gigabit ethernet adapters consist of two main
 * components: the SysKonnect GEnesis controller chip and the XaQti Corp.
 * XMAC II gigabit ethernet MAC. The XMAC provides all of the MAC
 * components and a PHY while the GEnesis controller provides a PCI
 * interface with DMA support. Each card may have between 512K and
 * 2MB of SRAM on board depending on the configuration.
 *
 * The SysKonnect GEnesis controller can have either one or two XMAC
 * chips connected to it, allowing single or dual port NIC configurations.
 * SysKonnect has the distinction of being the only vendor on the market
 * with a dual port gigabit ethernet NIC. The GEnesis provides dual FIFOs,
 * dual DMA queues, packet/MAC/transmit arbiters and direct access to the
 * XMAC registers. This driver takes advantage of these features to allow
 * both XMACs to operate as independent interfaces.
 */
 
#include "bpfilter.h"
#include "kstat.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/timeout.h>
#include <sys/device.h>
#include <sys/queue.h>

#include <net/if.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net/if_media.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#if NKSTAT > 0
#include <sys/kstat.h>
#endif

#include <dev/mii/miivar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/pci/if_skreg.h>
#include <dev/pci/if_mskvar.h>

#define MSK_STATUS_OWN_SHIFT		63
#define MSK_STATUS_OWN_MASK		0x1
#define MSK_STATUS_OPCODE_SHIFT		56
#define MSK_STATUS_OPCODE_MASK		0x7f

#define MSK_STATUS_OWN(_d) \
    (((_d) >> MSK_STATUS_OWN_SHIFT) & MSK_STATUS_OWN_MASK)
#define MSK_STATUS_OPCODE(_d) \
    (((_d) >> MSK_STATUS_OPCODE_SHIFT) & MSK_STATUS_OPCODE_MASK)

#define MSK_STATUS_OPCODE_RXSTAT	0x60
#define MSK_STATUS_OPCODE_RXTIMESTAMP	0x61
#define MSK_STATUS_OPCODE_RXVLAN	0x62
#define MSK_STATUS_OPCODE_RXCKSUM	0x64
#define MSK_STATUS_OPCODE_RXCKSUMVLAN	\
    (MSK_STATUS_OPCODE_RXVLAN | MSK_STATUS_OPCODE_RXCKSUM)
#define MSK_STATUS_OPCODE_RXTIMEVLAN	\
    (MSK_STATUS_OPCODE_RXVLAN | MSK_STATUS_OPCODE_RXTIMESTAMP)
#define MSK_STATUS_OPCODE_RSS_HASH	0x65
#define MSK_STATUS_OPCODE_TXIDX		0x68
#define MSK_STATUS_OPCODE_MACSEC	0x6c
#define MSK_STATUS_OPCODE_PUTIDX	0x70

#define MSK_STATUS_RXSTAT_PORT_SHIFT	48
#define MSK_STATUS_RXSTAT_PORT_MASK	0x1
#define MSK_STATUS_RXSTAT_LEN_SHIFT	32
#define MSK_STATUS_RXSTAT_LEN_MASK	0xffff
#define MSK_STATUS_RXSTAT_STATUS_SHIFT	0
#define MSK_STATUS_RXSTAT_STATUS_MASK	0xffffffff

#define MSK_STATUS_RXSTAT_PORT(_d) \
    (((_d) >> MSK_STATUS_RXSTAT_PORT_SHIFT) & MSK_STATUS_RXSTAT_PORT_MASK)
#define MSK_STATUS_RXSTAT_LEN(_d) \
    (((_d) >> MSK_STATUS_RXSTAT_LEN_SHIFT) & MSK_STATUS_RXSTAT_LEN_MASK)
#define MSK_STATUS_RXSTAT_STATUS(_d) \
    (((_d) >> MSK_STATUS_RXSTAT_STATUS_SHIFT) & MSK_STATUS_RXSTAT_STATUS_MASK)

#define MSK_STATUS_TXIDX_PORTA_SHIFT	0
#define MSK_STATUS_TXIDX_PORTA_MASK	0xfff
#define MSK_STATUS_TXIDX_PORTB_SHIFT	24
#define MSK_STATUS_TXIDX_PORTB_MASK	0xfff

#define MSK_STATUS_TXIDX_PORTA(_d) \
    (((_d) >> MSK_STATUS_TXIDX_PORTA_SHIFT) & MSK_STATUS_TXIDX_PORTA_MASK)
#define MSK_STATUS_TXIDX_PORTB(_d) \
    (((_d) >> MSK_STATUS_TXIDX_PORTB_SHIFT) & MSK_STATUS_TXIDX_PORTB_MASK)

int mskc_probe(struct device *, void *, void *);
void mskc_attach(struct device *, struct device *self, void *aux);
int mskc_detach(struct device *, int);
int mskc_activate(struct device *, int);
void mskc_reset(struct sk_softc *);
int msk_probe(struct device *, void *, void *);
void msk_attach(struct device *, struct device *self, void *aux);
int msk_detach(struct device *, int);
int msk_activate(struct device *, int);
void msk_reset(struct sk_if_softc *);
int mskcprint(void *, const char *);
int msk_intr(void *);
void msk_intr_yukon(struct sk_if_softc *);
static inline int msk_rxvalid(struct sk_softc *, u_int32_t, u_int32_t);
void msk_rxeof(struct sk_if_softc *, struct mbuf_list *, uint16_t, uint32_t);
void msk_txeof(struct sk_if_softc *, unsigned int);
static unsigned int msk_encap(struct sk_if_softc *, struct mbuf *, uint32_t);
void msk_start(struct ifnet *);
int msk_ioctl(struct ifnet *, u_long, caddr_t);
void msk_init(void *);
void msk_init_yukon(struct sk_if_softc *);
void msk_stop(struct sk_if_softc *, int);
void msk_watchdog(struct ifnet *);
int msk_ifmedia_upd(struct ifnet *);
void msk_ifmedia_sts(struct ifnet *, struct ifmediareq *);
static int msk_newbuf(struct sk_if_softc *);
int msk_init_rx_ring(struct sk_if_softc *);
int msk_init_tx_ring(struct sk_if_softc *);
void msk_fill_rx_ring(struct sk_if_softc *);

int msk_miibus_readreg(struct device *, int, int);
void msk_miibus_writereg(struct device *, int, int, int);
void msk_miibus_statchg(struct device *);

void msk_iff(struct sk_if_softc *);
void msk_tick(void *);
void msk_fill_rx_tick(void *);

#ifdef MSK_DEBUG
#define DPRINTF(x)	if (mskdebug) printf x
#define DPRINTFN(n,x)	if (mskdebug >= (n)) printf x
int	mskdebug = 0;

void msk_dump_txdesc(struct msk_tx_desc *, int);
void msk_dump_mbuf(struct mbuf *);
void msk_dump_bytes(const char *, int);
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

#if NKSTAT > 0
struct msk_mib {
	const char		*name;
	uint32_t		 reg;
	enum kstat_kv_type	 type;
	enum kstat_kv_unit	 unit;
};

#define C32	KSTAT_KV_T_COUNTER32
#define C64	KSTAT_KV_T_COUNTER64

#define PKTS	KSTAT_KV_U_PACKETS
#define BYTES	KSTAT_KV_U_BYTES
#define NONE	KSTAT_KV_U_NONE

static const struct msk_mib msk_mib[] = {
	{ "InUnicasts",		0x100,	C32,	PKTS },
	{ "InBroadcasts",	0x108,	C32,	PKTS },
	{ "InPause",		0x110,	C32,	PKTS },
	{ "InMulticasts",	0x118,	C32,	PKTS },
	{ "InFCSErr",		0x120,	C32,	PKTS },
	{ "InGoodOctets",	0x130,	C64,	BYTES },
	{ "InBadOctets",	0x140,	C64,	BYTES },
	{ "Undersize",		0x150,	C32,	PKTS },
	{ "Fragments",		0x158,	C32,	PKTS },
	{ "In64Octets",		0x160,	C32,	PKTS },
	{ "In127Octets",	0x168,	C32,	PKTS },
	{ "In255Octets",	0x170,	C32,	PKTS },
	{ "In511Octets",	0x178,	C32,	PKTS },
	{ "In1023Octets",	0x180,	C32,	PKTS },
	{ "In1518Octets",	0x188,	C32,	PKTS },
	{ "InMaxOctets",	0x190,	C32,	PKTS },
	{ "OverSize",		0x198,	C32,	PKTS },
	{ "Jabber",		0x1a8,	C32,	PKTS },
	{ "Overflow",		0x1b0,	C32,	PKTS },

	{ "OutUnicasts",	0x1c0,	C32,	PKTS },
	{ "OutBroadcasts",	0x1c8,	C32,	PKTS },
	{ "OutPause",		0x1d0,	C32,	PKTS },
	{ "OutMulticasts", 	0x1d8,	C32,	PKTS },
	{ "OutOctets",		0x1e0,	C64,	BYTES },
	{ "Out64Octets",	0x1f0,	C32,	PKTS },
	{ "Out127Octets",	0x1f8,	C32,	PKTS },
	{ "Out255Octets",	0x200,	C32,	PKTS },
	{ "Out511Octets",	0x208,	C32,	PKTS },
	{ "Out1023Octets",	0x210,	C32,	PKTS },
	{ "Out1518Octets",	0x218,	C32,	PKTS },
	{ "OutMaxOctets",	0x220,	C32,	PKTS },
	{ "Collisions",		0x230,	C32,	NONE },
	{ "Late",		0x238,	C32,	NONE },
	{ "Excessive",		0x240,	C32,	PKTS },
	{ "Multiple",		0x248,	C32,	PKTS },
	{ "Single",		0x250,	C32,	PKTS },
	{ "Underflow",		0x258,	C32,	PKTS },
};

#undef C32
#undef C64

#undef PKTS
#undef BYTES
#undef NONE

struct msk_kstat {
	struct rwlock		 lock;
	struct kstat		*ks;
};

static uint32_t		msk_mib_read32(struct sk_if_softc *, uint32_t);
static uint64_t		msk_mib_read64(struct sk_if_softc *, uint32_t);

void			msk_kstat_attach(struct sk_if_softc *);
void			msk_kstat_detach(struct sk_if_softc *);
int			msk_kstat_read(struct kstat *ks);
#endif

/* supported device vendors */
const struct pci_matchid mskc_devices[] = {
	{ PCI_VENDOR_DLINK,		PCI_PRODUCT_DLINK_DGE550SX },
	{ PCI_VENDOR_DLINK,		PCI_PRODUCT_DLINK_DGE550T_B1 },
	{ PCI_VENDOR_DLINK,		PCI_PRODUCT_DLINK_DGE560SX },
	{ PCI_VENDOR_DLINK,		PCI_PRODUCT_DLINK_DGE560T },
	{ PCI_VENDOR_MARVELL,		PCI_PRODUCT_MARVELL_YUKON_8021CU },
	{ PCI_VENDOR_MARVELL,		PCI_PRODUCT_MARVELL_YUKON_8021X },
	{ PCI_VENDOR_MARVELL,		PCI_PRODUCT_MARVELL_YUKON_8022CU },
	{ PCI_VENDOR_MARVELL,		PCI_PRODUCT_MARVELL_YUKON_8022X },
	{ PCI_VENDOR_MARVELL,		PCI_PRODUCT_MARVELL_YUKON_8035 },
	{ PCI_VENDOR_MARVELL,		PCI_PRODUCT_MARVELL_YUKON_8036 },
	{ PCI_VENDOR_MARVELL,		PCI_PRODUCT_MARVELL_YUKON_8038 },
	{ PCI_VENDOR_MARVELL,		PCI_PRODUCT_MARVELL_YUKON_8039 },
	{ PCI_VENDOR_MARVELL,		PCI_PRODUCT_MARVELL_YUKON_8040 },
	{ PCI_VENDOR_MARVELL,		PCI_PRODUCT_MARVELL_YUKON_8040T },
	{ PCI_VENDOR_MARVELL,		PCI_PRODUCT_MARVELL_YUKON_8042 },
	{ PCI_VENDOR_MARVELL,		PCI_PRODUCT_MARVELL_YUKON_8048 },
	{ PCI_VENDOR_MARVELL,		PCI_PRODUCT_MARVELL_YUKON_8050 },
	{ PCI_VENDOR_MARVELL,		PCI_PRODUCT_MARVELL_YUKON_8052 },
	{ PCI_VENDOR_MARVELL,		PCI_PRODUCT_MARVELL_YUKON_8053 },
	{ PCI_VENDOR_MARVELL,		PCI_PRODUCT_MARVELL_YUKON_8055 },
	{ PCI_VENDOR_MARVELL,		PCI_PRODUCT_MARVELL_YUKON_8055_2 },
	{ PCI_VENDOR_MARVELL,		PCI_PRODUCT_MARVELL_YUKON_8056 },
	{ PCI_VENDOR_MARVELL,		PCI_PRODUCT_MARVELL_YUKON_8057 },
	{ PCI_VENDOR_MARVELL,		PCI_PRODUCT_MARVELL_YUKON_8058 },
	{ PCI_VENDOR_MARVELL,		PCI_PRODUCT_MARVELL_YUKON_8059 },
	{ PCI_VENDOR_MARVELL,		PCI_PRODUCT_MARVELL_YUKON_8061CU },
	{ PCI_VENDOR_MARVELL,		PCI_PRODUCT_MARVELL_YUKON_8061X },
	{ PCI_VENDOR_MARVELL,		PCI_PRODUCT_MARVELL_YUKON_8062CU },
	{ PCI_VENDOR_MARVELL,		PCI_PRODUCT_MARVELL_YUKON_8062X },
	{ PCI_VENDOR_MARVELL,		PCI_PRODUCT_MARVELL_YUKON_8070 },
	{ PCI_VENDOR_MARVELL,		PCI_PRODUCT_MARVELL_YUKON_8071 },
	{ PCI_VENDOR_MARVELL,		PCI_PRODUCT_MARVELL_YUKON_8072 },
	{ PCI_VENDOR_MARVELL,		PCI_PRODUCT_MARVELL_YUKON_8075 },
	{ PCI_VENDOR_MARVELL,		PCI_PRODUCT_MARVELL_YUKON_8079 },
	{ PCI_VENDOR_MARVELL,		PCI_PRODUCT_MARVELL_YUKON_C032 },
	{ PCI_VENDOR_MARVELL,		PCI_PRODUCT_MARVELL_YUKON_C033 },
	{ PCI_VENDOR_MARVELL,		PCI_PRODUCT_MARVELL_YUKON_C034 },
	{ PCI_VENDOR_MARVELL,		PCI_PRODUCT_MARVELL_YUKON_C036 },
	{ PCI_VENDOR_MARVELL,		PCI_PRODUCT_MARVELL_YUKON_C042 },
	{ PCI_VENDOR_SCHNEIDERKOCH,	PCI_PRODUCT_SCHNEIDERKOCH_SK9EXX },
	{ PCI_VENDOR_SCHNEIDERKOCH,	PCI_PRODUCT_SCHNEIDERKOCH_SK9SXX }
};

static inline u_int32_t
sk_win_read_4(struct sk_softc *sc, u_int32_t reg)
{
	return CSR_READ_4(sc, reg);
}

static inline u_int16_t
sk_win_read_2(struct sk_softc *sc, u_int32_t reg)
{
	return CSR_READ_2(sc, reg);
}

static inline u_int8_t
sk_win_read_1(struct sk_softc *sc, u_int32_t reg)
{
	return CSR_READ_1(sc, reg);
}

static inline void
sk_win_write_4(struct sk_softc *sc, u_int32_t reg, u_int32_t x)
{
	CSR_WRITE_4(sc, reg, x);
}

static inline void
sk_win_write_2(struct sk_softc *sc, u_int32_t reg, u_int16_t x)
{
	CSR_WRITE_2(sc, reg, x);
}

static inline void
sk_win_write_1(struct sk_softc *sc, u_int32_t reg, u_int8_t x)
{
	CSR_WRITE_1(sc, reg, x);
}

int
msk_miibus_readreg(struct device *dev, int phy, int reg)
{
	struct sk_if_softc *sc_if = (struct sk_if_softc *)dev;
	u_int16_t val;
	int i;

        SK_YU_WRITE_2(sc_if, YUKON_SMICR, YU_SMICR_PHYAD(phy) |
		      YU_SMICR_REGAD(reg) | YU_SMICR_OP_READ);
        
	for (i = 0; i < SK_TIMEOUT; i++) {
		DELAY(1);
		val = SK_YU_READ_2(sc_if, YUKON_SMICR);
		if (val & YU_SMICR_READ_VALID)
			break;
	}

	if (i == SK_TIMEOUT) {
		printf("%s: phy failed to come ready\n",
		       sc_if->sk_dev.dv_xname);
		return (0);
	}
        
 	DPRINTFN(9, ("msk_miibus_readreg: i=%d, timeout=%d\n", i,
		     SK_TIMEOUT));

        val = SK_YU_READ_2(sc_if, YUKON_SMIDR);

	DPRINTFN(9, ("msk_miibus_readreg phy=%d, reg=%#x, val=%#x\n",
		     phy, reg, val));

	return (val);
}

void
msk_miibus_writereg(struct device *dev, int phy, int reg, int val)
{
	struct sk_if_softc *sc_if = (struct sk_if_softc *)dev;
	int i;

	DPRINTFN(9, ("msk_miibus_writereg phy=%d reg=%#x val=%#x\n",
		     phy, reg, val));

	SK_YU_WRITE_2(sc_if, YUKON_SMIDR, val);
	SK_YU_WRITE_2(sc_if, YUKON_SMICR, YU_SMICR_PHYAD(phy) |
		      YU_SMICR_REGAD(reg) | YU_SMICR_OP_WRITE);

	for (i = 0; i < SK_TIMEOUT; i++) {
		DELAY(1);
		if (!(SK_YU_READ_2(sc_if, YUKON_SMICR) & YU_SMICR_BUSY))
			break;
	}

	if (i == SK_TIMEOUT)
		printf("%s: phy write timed out\n", sc_if->sk_dev.dv_xname);
}

void
msk_miibus_statchg(struct device *dev)
{
	struct sk_if_softc *sc_if = (struct sk_if_softc *)dev;
	struct mii_data *mii = &sc_if->sk_mii;
	struct ifmedia_entry *ife = mii->mii_media.ifm_cur;
	int gpcr;

	gpcr = SK_YU_READ_2(sc_if, YUKON_GPCR);
	gpcr &= (YU_GPCR_TXEN | YU_GPCR_RXEN);

	if (IFM_SUBTYPE(ife->ifm_media) != IFM_AUTO ||
	    sc_if->sk_softc->sk_type == SK_YUKON_FE_P) {
		/* Set speed. */
		gpcr |= YU_GPCR_SPEED_DIS;
		switch (IFM_SUBTYPE(mii->mii_media_active)) {
		case IFM_1000_SX:
		case IFM_1000_LX:
		case IFM_1000_CX:
		case IFM_1000_T:
			gpcr |= (YU_GPCR_GIG | YU_GPCR_SPEED);
			break;
		case IFM_100_TX:
			gpcr |= YU_GPCR_SPEED;
			break;
		}

		/* Set duplex. */
		gpcr |= YU_GPCR_DPLX_DIS;
		if ((mii->mii_media_active & IFM_GMASK) == IFM_FDX)
			gpcr |= YU_GPCR_DUPLEX;

		/* Disable flow control. */
		gpcr |= YU_GPCR_FCTL_DIS;
		gpcr |= (YU_GPCR_FCTL_TX_DIS | YU_GPCR_FCTL_RX_DIS);
	}

	SK_YU_WRITE_2(sc_if, YUKON_GPCR, gpcr);

	DPRINTFN(9, ("msk_miibus_statchg: gpcr=%x\n",
		     SK_YU_READ_2(((struct sk_if_softc *)dev), YUKON_GPCR)));
}

void
msk_iff(struct sk_if_softc *sc_if)
{
	struct ifnet *ifp = &sc_if->arpcom.ac_if;
	struct arpcom *ac = &sc_if->arpcom;
	struct ether_multi *enm;
	struct ether_multistep step;
	u_int32_t hashes[2];
	u_int16_t rcr;
	int h;

	rcr = SK_YU_READ_2(sc_if, YUKON_RCR);
	rcr &= ~(YU_RCR_MUFLEN | YU_RCR_UFLEN);
	ifp->if_flags &= ~IFF_ALLMULTI;

	/*
	 * Always accept frames destined to our station address.
	 */
	rcr |= YU_RCR_UFLEN;

	if (ifp->if_flags & IFF_PROMISC || ac->ac_multirangecnt > 0) {
		ifp->if_flags |= IFF_ALLMULTI;
		if (ifp->if_flags & IFF_PROMISC)
			rcr &= ~YU_RCR_UFLEN;
		else
			rcr |= YU_RCR_MUFLEN;
		hashes[0] = hashes[1] = 0xFFFFFFFF;
	} else {
		rcr |= YU_RCR_MUFLEN;
		/* Program new filter. */
		bzero(hashes, sizeof(hashes));

		ETHER_FIRST_MULTI(step, ac, enm);
		while (enm != NULL) {
			h = ether_crc32_be(enm->enm_addrlo,
			    ETHER_ADDR_LEN) & ((1 << SK_HASH_BITS) - 1);

			if (h < 32)
				hashes[0] |= (1 << h);
			else
				hashes[1] |= (1 << (h - 32));

			ETHER_NEXT_MULTI(step, enm);
		}
	}

	SK_YU_WRITE_2(sc_if, YUKON_MCAH1, hashes[0] & 0xffff);
	SK_YU_WRITE_2(sc_if, YUKON_MCAH2, (hashes[0] >> 16) & 0xffff);
	SK_YU_WRITE_2(sc_if, YUKON_MCAH3, hashes[1] & 0xffff);
	SK_YU_WRITE_2(sc_if, YUKON_MCAH4, (hashes[1] >> 16) & 0xffff);
	SK_YU_WRITE_2(sc_if, YUKON_RCR, rcr);
}

int
msk_init_rx_ring(struct sk_if_softc *sc_if)
{
	struct msk_ring_data	*rd = sc_if->sk_rdata;
	struct msk_rx_desc	*r;

	memset(rd->sk_rx_ring, 0, sizeof(struct msk_rx_desc) * MSK_RX_RING_CNT);

	r = &rd->sk_rx_ring[0];
	r->sk_addr = htole32(0);
	r->sk_opcode = SK_Y2_RXOPC_OWN | SK_Y2_RXOPC_ADDR64;

	sc_if->sk_cdata.sk_rx_prod = 1;
	sc_if->sk_cdata.sk_rx_cons = 0;
	sc_if->sk_cdata.sk_rx_hiaddr = 0;

	/*
	 * up to two ring entries per packet, so the effective ring size is
	 * halved
	 */
	if_rxr_init(&sc_if->sk_cdata.sk_rx_ring, 2, (MSK_RX_RING_CNT/2) - 1);

	msk_fill_rx_ring(sc_if);
	return (0);
}

int
msk_init_tx_ring(struct sk_if_softc *sc_if)
{
	struct sk_softc		*sc = sc_if->sk_softc;
	struct msk_ring_data	*rd = sc_if->sk_rdata;
	struct msk_tx_desc	*t;
	int			i;

	memset(rd->sk_tx_ring, 0, sizeof(struct msk_tx_desc) * MSK_TX_RING_CNT);

	for (i = 0; i < MSK_TX_RING_CNT; i++) {
		if (bus_dmamap_create(sc->sc_dmatag, sc_if->sk_pktlen,
		    SK_NTXSEG, sc_if->sk_pktlen, 0,
		    BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW | BUS_DMA_64BIT,
		    &sc_if->sk_cdata.sk_tx_maps[i]))
			return (ENOBUFS);
	}

	t = &rd->sk_tx_ring[0];
	t->sk_addr = htole32(0);
	t->sk_opcode = SK_Y2_TXOPC_OWN | SK_Y2_TXOPC_ADDR64;

	sc_if->sk_cdata.sk_tx_prod = 1;
	sc_if->sk_cdata.sk_tx_cons = 0;
	sc_if->sk_cdata.sk_tx_hiaddr = 0;

	MSK_CDTXSYNC(sc_if, 0, MSK_TX_RING_CNT, BUS_DMASYNC_PREWRITE);

	return (0);
}

static int
msk_newbuf(struct sk_if_softc *sc_if)
{
	struct msk_ring_data	*rd = sc_if->sk_rdata;
	struct msk_rx_desc	*r;
	struct mbuf		*m;
	bus_dmamap_t		map;
	uint64_t		addr;
	uint32_t		prod, head;
	uint32_t		hiaddr;
	unsigned int		pktlen = sc_if->sk_pktlen + ETHER_ALIGN;

	m = MCLGETL(NULL, M_DONTWAIT, pktlen);
	if (m == NULL)
		return (0);
	m->m_len = m->m_pkthdr.len = pktlen;
	m_adj(m, ETHER_ALIGN);

	prod = sc_if->sk_cdata.sk_rx_prod;
	map = sc_if->sk_cdata.sk_rx_maps[prod];

	if (bus_dmamap_load_mbuf(sc_if->sk_softc->sc_dmatag, map, m,
	    BUS_DMA_READ|BUS_DMA_NOWAIT) != 0) {
		m_freem(m);
		return (0);
	}

	bus_dmamap_sync(sc_if->sk_softc->sc_dmatag, map, 0,
	    map->dm_mapsize, BUS_DMASYNC_PREREAD);

	head = prod;

	/* high 32 bits of address */
	addr = map->dm_segs[0].ds_addr;
	hiaddr = addr >> 32;
	if (sc_if->sk_cdata.sk_rx_hiaddr != hiaddr) {
		r = &rd->sk_rx_ring[prod];
		htolem32(&r->sk_addr, hiaddr);
		r->sk_len = htole16(0);
		r->sk_ctl = 0;
		r->sk_opcode = SK_Y2_RXOPC_OWN | SK_Y2_RXOPC_ADDR64;

		sc_if->sk_cdata.sk_rx_hiaddr = hiaddr;

		SK_INC(prod, MSK_RX_RING_CNT);
	}

	r = &rd->sk_rx_ring[prod];
	htolem32(&r->sk_addr, addr);
	htolem16(&r->sk_len, map->dm_segs[0].ds_len);
	r->sk_ctl = 0;
	r->sk_opcode = SK_Y2_RXOPC_OWN | SK_Y2_RXOPC_PACKET;

	sc_if->sk_cdata.sk_rx_maps[head] = sc_if->sk_cdata.sk_rx_maps[prod];
	sc_if->sk_cdata.sk_rx_maps[prod] = map;

	sc_if->sk_cdata.sk_rx_mbuf[prod] = m;

	SK_INC(prod, MSK_RX_RING_CNT);
	sc_if->sk_cdata.sk_rx_prod = prod;

	return (1);
}

/*
 * Set media options.
 */
int
msk_ifmedia_upd(struct ifnet *ifp)
{
	struct sk_if_softc *sc_if = ifp->if_softc;

	mii_mediachg(&sc_if->sk_mii);
	return (0);
}

/*
 * Report current media status.
 */
void
msk_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct sk_if_softc *sc_if = ifp->if_softc;

	mii_pollstat(&sc_if->sk_mii);
	ifmr->ifm_active = sc_if->sk_mii.mii_media_active;
	ifmr->ifm_status = sc_if->sk_mii.mii_media_status;
}

int
msk_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct sk_if_softc *sc_if = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *) data;
	struct mii_data *mii;
	int s, error = 0;

	s = splnet();

	switch(command) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		if (!(ifp->if_flags & IFF_RUNNING))
			msk_init(sc_if);
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				error = ENETRESET;
			else
				msk_init(sc_if);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				msk_stop(sc_if, 0);
		}
		break;

	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		mii = &sc_if->sk_mii;
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, command);
		break;

	case SIOCGIFRXR:
		error = if_rxr_ioctl((struct if_rxrinfo *)ifr->ifr_data,
		    NULL, sc_if->sk_pktlen, &sc_if->sk_cdata.sk_rx_ring);
 		break;

	default:
		error = ether_ioctl(ifp, &sc_if->arpcom, command, data);
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			msk_iff(sc_if);
		error = 0;
	}

	splx(s);
	return (error);
}

/*
 * Probe for a SysKonnect GEnesis chip. Check the PCI vendor and device
 * IDs against our list and return a device name if we find a match.
 */
int
mskc_probe(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid((struct pci_attach_args *)aux, mskc_devices,
	    nitems(mskc_devices)));
}

/*
 * Force the GEnesis into reset, then bring it out of reset.
 */
void
mskc_reset(struct sk_softc *sc)
{
	u_int32_t imtimer_ticks, reg1;
	int reg;
	unsigned int i;

	DPRINTFN(2, ("mskc_reset\n"));

	CSR_WRITE_1(sc, SK_CSR, SK_CSR_SW_RESET);
	CSR_WRITE_1(sc, SK_CSR, SK_CSR_MASTER_RESET);

	DELAY(1000);
	CSR_WRITE_1(sc, SK_CSR, SK_CSR_SW_UNRESET);
	DELAY(2);
	CSR_WRITE_1(sc, SK_CSR, SK_CSR_MASTER_UNRESET);

	sk_win_write_1(sc, SK_TESTCTL1, 2);

	if (sc->sk_type == SK_YUKON_EC_U || sc->sk_type == SK_YUKON_EX ||
	    sc->sk_type >= SK_YUKON_FE_P) {
		/* enable all clocks. */
		sk_win_write_4(sc, SK_Y2_PCI_REG(SK_PCI_OURREG3), 0);
		reg1 = sk_win_read_4(sc, SK_Y2_PCI_REG(SK_PCI_OURREG4));
		reg1 &= (SK_Y2_REG4_FORCE_ASPM_REQUEST|
		    SK_Y2_REG4_ASPM_GPHY_LINK_DOWN|
		    SK_Y2_REG4_ASPM_INT_FIFO_EMPTY|
		    SK_Y2_REG4_ASPM_CLKRUN_REQUEST);
		sk_win_write_4(sc, SK_Y2_PCI_REG(SK_PCI_OURREG4), reg1);

		reg1 = sk_win_read_4(sc, SK_Y2_PCI_REG(SK_PCI_OURREG5));
		reg1 &= SK_Y2_REG5_TIM_VMAIN_AV_MASK;
		sk_win_write_4(sc, SK_Y2_PCI_REG(SK_PCI_OURREG5), reg1);
		sk_win_write_4(sc, SK_Y2_PCI_REG(SK_PCI_CFGREG1), 0);

		/*
		 * Disable status race, workaround for Yukon EC Ultra &
		 * Yukon EX.
		 */
		reg1 = sk_win_read_4(sc, SK_GPIO);
		reg1 |= SK_Y2_GPIO_STAT_RACE_DIS;
		sk_win_write_4(sc, SK_GPIO, reg1);
		sk_win_read_4(sc, SK_GPIO);
	}

	reg1 = sk_win_read_4(sc, SK_Y2_PCI_REG(SK_PCI_OURREG1));
	if (sc->sk_type == SK_YUKON_XL && sc->sk_rev > SK_YUKON_XL_REV_A1)
		reg1 |= (SK_Y2_REG1_PHY1_COMA | SK_Y2_REG1_PHY2_COMA);
	else
		reg1 &= ~(SK_Y2_REG1_PHY1_COMA | SK_Y2_REG1_PHY2_COMA);
	sk_win_write_4(sc, SK_Y2_PCI_REG(SK_PCI_OURREG1), reg1);

	if (sc->sk_type == SK_YUKON_XL && sc->sk_rev > SK_YUKON_XL_REV_A1)
		sk_win_write_1(sc, SK_Y2_CLKGATE,
		    SK_Y2_CLKGATE_LINK1_GATE_DIS |
		    SK_Y2_CLKGATE_LINK2_GATE_DIS |
		    SK_Y2_CLKGATE_LINK1_CORE_DIS |
		    SK_Y2_CLKGATE_LINK2_CORE_DIS |
		    SK_Y2_CLKGATE_LINK1_PCI_DIS | SK_Y2_CLKGATE_LINK2_PCI_DIS);
	else
		sk_win_write_1(sc, SK_Y2_CLKGATE, 0);

	CSR_WRITE_2(sc, SK_LINK_CTRL, SK_LINK_RESET_SET);
	CSR_WRITE_2(sc, SK_LINK_CTRL + SK_WIN_LEN, SK_LINK_RESET_SET);
	DELAY(1000);
	CSR_WRITE_2(sc, SK_LINK_CTRL, SK_LINK_RESET_CLEAR);
	CSR_WRITE_2(sc, SK_LINK_CTRL + SK_WIN_LEN, SK_LINK_RESET_CLEAR);

	if (sc->sk_type == SK_YUKON_EX || sc->sk_type == SK_YUKON_SUPR) {
		CSR_WRITE_2(sc, SK_GMAC_CTRL, SK_GMAC_BYP_MACSECRX |
		    SK_GMAC_BYP_MACSECTX | SK_GMAC_BYP_RETR_FIFO);
	}

	sk_win_write_1(sc, SK_TESTCTL1, 1);

	DPRINTFN(2, ("mskc_reset: sk_csr=%x\n", CSR_READ_1(sc, SK_CSR)));
	DPRINTFN(2, ("mskc_reset: sk_link_ctrl=%x\n",
		     CSR_READ_2(sc, SK_LINK_CTRL)));

	/* Disable ASF */
	CSR_WRITE_1(sc, SK_Y2_ASF_CSR, SK_Y2_ASF_RESET);
	CSR_WRITE_2(sc, SK_CSR, SK_CSR_ASF_OFF);

	/* Clear I2C IRQ noise */
	CSR_WRITE_4(sc, SK_I2CHWIRQ, 1);

	/* Disable hardware timer */
	CSR_WRITE_1(sc, SK_TIMERCTL, SK_IMCTL_STOP);
	CSR_WRITE_1(sc, SK_TIMERCTL, SK_IMCTL_IRQ_CLEAR);

	/* Disable descriptor polling */
	CSR_WRITE_4(sc, SK_DPT_TIMER_CTRL, SK_DPT_TCTL_STOP);

	/* Disable time stamps */
	CSR_WRITE_1(sc, SK_TSTAMP_CTL, SK_TSTAMP_STOP);
	CSR_WRITE_1(sc, SK_TSTAMP_CTL, SK_TSTAMP_IRQ_CLEAR);

	/* Enable RAM interface */
	sk_win_write_1(sc, SK_RAMCTL, SK_RAMCTL_UNRESET);
	for (reg = SK_TO0;reg <= SK_TO11; reg++)
		sk_win_write_1(sc, reg, 36);
	sk_win_write_1(sc, SK_RAMCTL + (SK_WIN_LEN / 2), SK_RAMCTL_UNRESET);
	for (reg = SK_TO0;reg <= SK_TO11; reg++)
		sk_win_write_1(sc, reg + (SK_WIN_LEN / 2), 36);

	/*
	 * Configure interrupt moderation. The moderation timer
	 * defers interrupts specified in the interrupt moderation
	 * timer mask based on the timeout specified in the interrupt
	 * moderation timer init register. Each bit in the timer
	 * register represents one tick, so to specify a timeout in
	 * microseconds, we have to multiply by the correct number of
	 * ticks-per-microsecond.
	 */
	switch (sc->sk_type) {
	case SK_YUKON_EC:
	case SK_YUKON_EC_U:
	case SK_YUKON_EX:
	case SK_YUKON_SUPR:
	case SK_YUKON_ULTRA2:
	case SK_YUKON_OPTIMA:
	case SK_YUKON_PRM:
	case SK_YUKON_OPTIMA2:
		imtimer_ticks = SK_IMTIMER_TICKS_YUKON_EC;
		break;
	case SK_YUKON_FE:
		imtimer_ticks = SK_IMTIMER_TICKS_YUKON_FE;
		break;
	case SK_YUKON_FE_P:
		imtimer_ticks = SK_IMTIMER_TICKS_YUKON_FE_P;
		break;
	case SK_YUKON_XL:
		imtimer_ticks = SK_IMTIMER_TICKS_YUKON_XL;
		break;
	default:
		imtimer_ticks = SK_IMTIMER_TICKS_YUKON;
		break;
	}

	/* Reset status ring. */
	for (i = 0; i < MSK_STATUS_RING_CNT; i++)
		sc->sk_status_ring[i] = htole64(0);
	sc->sk_status_idx = 0;

	sk_win_write_4(sc, SK_STAT_BMU_CSR, SK_STAT_BMU_RESET);
	sk_win_write_4(sc, SK_STAT_BMU_CSR, SK_STAT_BMU_UNRESET);

	sk_win_write_2(sc, SK_STAT_BMU_LIDX, MSK_STATUS_RING_CNT - 1);
	sk_win_write_4(sc, SK_STAT_BMU_ADDRLO,
	    sc->sk_status_map->dm_segs[0].ds_addr);
	sk_win_write_4(sc, SK_STAT_BMU_ADDRHI,
	    (u_int64_t)sc->sk_status_map->dm_segs[0].ds_addr >> 32);
	sk_win_write_2(sc, SK_STAT_BMU_TX_THRESH, 10);
	sk_win_write_1(sc, SK_STAT_BMU_FIFOWM, 16);
	sk_win_write_1(sc, SK_STAT_BMU_FIFOIWM, 16);

#if 0
	sk_win_write_4(sc, SK_Y2_LEV_ITIMERINIT, SK_IM_USECS(100));
	sk_win_write_4(sc, SK_Y2_TX_ITIMERINIT, SK_IM_USECS(1000));
	sk_win_write_4(sc, SK_Y2_ISR_ITIMERINIT, SK_IM_USECS(20));
#else
	sk_win_write_4(sc, SK_Y2_ISR_ITIMERINIT, SK_IM_USECS(4));
#endif

	sk_win_write_4(sc, SK_STAT_BMU_CSR, SK_STAT_BMU_ON);

	sk_win_write_1(sc, SK_Y2_LEV_ITIMERCTL, SK_IMCTL_START);
	sk_win_write_1(sc, SK_Y2_TX_ITIMERCTL, SK_IMCTL_START);
	sk_win_write_1(sc, SK_Y2_ISR_ITIMERCTL, SK_IMCTL_START);
}

int
msk_probe(struct device *parent, void *match, void *aux)
{
	struct skc_attach_args *sa = aux;

	if (sa->skc_port != SK_PORT_A && sa->skc_port != SK_PORT_B)
		return (0);

	switch (sa->skc_type) {
	case SK_YUKON_XL:
	case SK_YUKON_EC_U:
	case SK_YUKON_EX:
	case SK_YUKON_EC:
	case SK_YUKON_FE:
	case SK_YUKON_FE_P:
	case SK_YUKON_SUPR:
	case SK_YUKON_ULTRA2:
	case SK_YUKON_OPTIMA:
	case SK_YUKON_PRM:
	case SK_YUKON_OPTIMA2:
		return (1);
	}

	return (0);
}

void
msk_reset(struct sk_if_softc *sc_if)
{
	/* GMAC and GPHY Reset */
	SK_IF_WRITE_4(sc_if, 0, SK_GMAC_CTRL, SK_GMAC_RESET_SET);
	SK_IF_WRITE_4(sc_if, 0, SK_GPHY_CTRL, SK_GPHY_RESET_SET);
	DELAY(1000);
	SK_IF_WRITE_4(sc_if, 0, SK_GPHY_CTRL, SK_GPHY_RESET_CLEAR);
	SK_IF_WRITE_4(sc_if, 0, SK_GMAC_CTRL, SK_GMAC_LOOP_OFF |
		      SK_GMAC_PAUSE_ON | SK_GMAC_RESET_CLEAR);
}

/*
 * Each XMAC chip is attached as a separate logical IP interface.
 * Single port cards will have only one logical interface of course.
 */
void
msk_attach(struct device *parent, struct device *self, void *aux)
{
	struct sk_if_softc *sc_if = (struct sk_if_softc *)self;
	struct sk_softc *sc = (struct sk_softc *)parent;
	struct skc_attach_args *sa = aux;
	struct ifnet *ifp;
	caddr_t kva;
	int i;
	u_int32_t chunk;
	int mii_flags;
	int error;

	sc_if->sk_port = sa->skc_port;
	sc_if->sk_softc = sc;
	sc->sk_if[sa->skc_port] = sc_if;

	DPRINTFN(2, ("begin msk_attach: port=%d\n", sc_if->sk_port));

	/*
	 * Get station address for this interface. Note that
	 * dual port cards actually come with three station
	 * addresses: one for each port, plus an extra. The
	 * extra one is used by the SysKonnect driver software
	 * as a 'virtual' station address for when both ports
	 * are operating in failover mode. Currently we don't
	 * use this extra address.
	 */
	for (i = 0; i < ETHER_ADDR_LEN; i++)
		sc_if->arpcom.ac_enaddr[i] =
		    sk_win_read_1(sc, SK_MAC0_0 + (sa->skc_port * 8) + i);

	printf(": address %s\n",
	    ether_sprintf(sc_if->arpcom.ac_enaddr));

	/*
	 * Set up RAM buffer addresses. The Yukon2 has a small amount
	 * of SRAM on it, somewhere between 4K and 48K.  We need to
	 * divide this up between the transmitter and receiver.  We
	 * give the receiver 2/3 of the memory (rounded down), and the
	 * transmitter whatever remains.
	 */
	chunk = (2 * (sc->sk_ramsize / sizeof(u_int64_t)) / 3) & ~0xff;
	sc_if->sk_rx_ramstart = 0;
	sc_if->sk_rx_ramend = sc_if->sk_rx_ramstart + chunk - 1;
	chunk = (sc->sk_ramsize / sizeof(u_int64_t)) - chunk;
	sc_if->sk_tx_ramstart = sc_if->sk_rx_ramend + 1;
	sc_if->sk_tx_ramend = sc_if->sk_tx_ramstart + chunk - 1;

	DPRINTFN(2, ("msk_attach: rx_ramstart=%#x rx_ramend=%#x\n"
		     "           tx_ramstart=%#x tx_ramend=%#x\n",
		     sc_if->sk_rx_ramstart, sc_if->sk_rx_ramend,
		     sc_if->sk_tx_ramstart, sc_if->sk_tx_ramend));

	/* Allocate the descriptor queues. */
	if (bus_dmamem_alloc(sc->sc_dmatag, sizeof(struct msk_ring_data),
	    PAGE_SIZE, 0, &sc_if->sk_ring_seg, 1, &sc_if->sk_ring_nseg,
	    BUS_DMA_NOWAIT | BUS_DMA_ZERO)) {
		printf(": can't alloc rx buffers\n");
		goto fail;
	}
	if (bus_dmamem_map(sc->sc_dmatag, &sc_if->sk_ring_seg,
	    sc_if->sk_ring_nseg,
	    sizeof(struct msk_ring_data), &kva, BUS_DMA_NOWAIT)) {
		printf(": can't map dma buffers (%lu bytes)\n",
		       (ulong)sizeof(struct msk_ring_data));
		goto fail_1;
	}
	if (bus_dmamap_create(sc->sc_dmatag, sizeof(struct msk_ring_data), 1,
	    sizeof(struct msk_ring_data), 0,
	    BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW | BUS_DMA_64BIT,
            &sc_if->sk_ring_map)) {
		printf(": can't create dma map\n");
		goto fail_2;
	}
	if (bus_dmamap_load(sc->sc_dmatag, sc_if->sk_ring_map, kva,
	    sizeof(struct msk_ring_data), NULL, BUS_DMA_NOWAIT)) {
		printf(": can't load dma map\n");
		goto fail_3;
	}
        sc_if->sk_rdata = (struct msk_ring_data *)kva;

	if (sc->sk_type != SK_YUKON_FE &&
	    sc->sk_type != SK_YUKON_FE_P)
		sc_if->sk_pktlen = SK_JLEN;
	else
		sc_if->sk_pktlen = MCLBYTES;

	for (i = 0; i < MSK_RX_RING_CNT; i++) {
		if ((error = bus_dmamap_create(sc->sc_dmatag,
		    sc_if->sk_pktlen, 1, sc_if->sk_pktlen, 0,
		    BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW | BUS_DMA_64BIT,
		    &sc_if->sk_cdata.sk_rx_maps[i])) != 0) {
			printf("\n%s: unable to create rx DMA map %d, "
			    "error = %d\n", sc->sk_dev.dv_xname, i, error);
			goto fail_4;
		}
	}

	ifp = &sc_if->arpcom.ac_if;
	ifp->if_softc = sc_if;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = msk_ioctl;
	ifp->if_start = msk_start;
	ifp->if_watchdog = msk_watchdog;
	if (sc->sk_type != SK_YUKON_FE &&
	    sc->sk_type != SK_YUKON_FE_P)
		ifp->if_hardmtu = SK_JUMBO_MTU;
	ifq_init_maxlen(&ifp->if_snd, MSK_TX_RING_CNT - 1);
	bcopy(sc_if->sk_dev.dv_xname, ifp->if_xname, IFNAMSIZ);

	ifp->if_capabilities = IFCAP_VLAN_MTU;

	msk_reset(sc_if);

	/*
	 * Do miibus setup.
	 */
	msk_init_yukon(sc_if);

 	DPRINTFN(2, ("msk_attach: 1\n"));

	sc_if->sk_mii.mii_ifp = ifp;
	sc_if->sk_mii.mii_readreg = msk_miibus_readreg;
	sc_if->sk_mii.mii_writereg = msk_miibus_writereg;
	sc_if->sk_mii.mii_statchg = msk_miibus_statchg;

	ifmedia_init(&sc_if->sk_mii.mii_media, 0,
	    msk_ifmedia_upd, msk_ifmedia_sts);
	mii_flags = MIIF_DOPAUSE;
	if (sc->sk_fibertype)
		mii_flags |= MIIF_HAVEFIBER;
	mii_attach(self, &sc_if->sk_mii, 0xffffffff, 0,
	    MII_OFFSET_ANY, mii_flags);
	if (LIST_FIRST(&sc_if->sk_mii.mii_phys) == NULL) {
		printf("%s: no PHY found!\n", sc_if->sk_dev.dv_xname);
		ifmedia_add(&sc_if->sk_mii.mii_media, IFM_ETHER|IFM_MANUAL,
			    0, NULL);
		ifmedia_set(&sc_if->sk_mii.mii_media, IFM_ETHER|IFM_MANUAL);
	} else
		ifmedia_set(&sc_if->sk_mii.mii_media, IFM_ETHER|IFM_AUTO);

	timeout_set(&sc_if->sk_tick_ch, msk_tick, sc_if);
	timeout_set(&sc_if->sk_tick_rx, msk_fill_rx_tick, sc_if);

	/*
	 * Call MI attach routines.
	 */
	if_attach(ifp);
	ether_ifattach(ifp);

#if NKSTAT > 0
	msk_kstat_attach(sc_if);
#endif

	DPRINTFN(2, ("msk_attach: end\n"));
	return;

fail_4:
	for (i = 0; i < MSK_RX_RING_CNT; i++) {
		if (sc_if->sk_cdata.sk_rx_maps[i] != NULL)
			bus_dmamap_destroy(sc->sc_dmatag,
			    sc_if->sk_cdata.sk_rx_maps[i]);
	}

fail_3:
	bus_dmamap_destroy(sc->sc_dmatag, sc_if->sk_ring_map);
fail_2:
	bus_dmamem_unmap(sc->sc_dmatag, kva, sizeof(struct msk_ring_data));
fail_1:
	bus_dmamem_free(sc->sc_dmatag, &sc_if->sk_ring_seg, sc_if->sk_ring_nseg);
fail:
	sc->sk_if[sa->skc_port] = NULL;
}

int
msk_detach(struct device *self, int flags)
{
	struct sk_if_softc *sc_if = (struct sk_if_softc *)self;
	struct sk_softc *sc = sc_if->sk_softc;
	struct ifnet *ifp= &sc_if->arpcom.ac_if;

	if (sc->sk_if[sc_if->sk_port] == NULL)
		return (0);

	msk_stop(sc_if, 1);

#if NKSTAT > 0
	msk_kstat_detach(sc_if);
#endif

	/* Detach any PHYs we might have. */
	if (LIST_FIRST(&sc_if->sk_mii.mii_phys) != NULL)
		mii_detach(&sc_if->sk_mii, MII_PHY_ANY, MII_OFFSET_ANY);

	/* Delete any remaining media. */
	ifmedia_delete_instance(&sc_if->sk_mii.mii_media, IFM_INST_ANY);

	ether_ifdetach(ifp);
	if_detach(ifp);

	bus_dmamem_unmap(sc->sc_dmatag, (caddr_t)sc_if->sk_rdata,
	    sizeof(struct msk_ring_data));
	bus_dmamem_free(sc->sc_dmatag,
	    &sc_if->sk_ring_seg, sc_if->sk_ring_nseg);
	bus_dmamap_destroy(sc->sc_dmatag, sc_if->sk_ring_map);
	sc->sk_if[sc_if->sk_port] = NULL;

	return (0);
}

int
msk_activate(struct device *self, int act)
{
	struct sk_if_softc *sc_if = (void *)self;
	struct ifnet *ifp = &sc_if->arpcom.ac_if;

	switch (act) {
	case DVACT_RESUME:
		msk_reset(sc_if);
		if (ifp->if_flags & IFF_RUNNING)
			msk_init(sc_if);
		break;
	}
	return (0);
}

int
mskcprint(void *aux, const char *pnp)
{
	struct skc_attach_args *sa = aux;

	if (pnp)
		printf("msk port %c at %s",
		    (sa->skc_port == SK_PORT_A) ? 'A' : 'B', pnp);
	else
		printf(" port %c", (sa->skc_port == SK_PORT_A) ? 'A' : 'B');
	return (UNCONF);
}

/*
 * Attach the interface. Allocate softc structures, do ifmedia
 * setup and ethernet/BPF attach.
 */
void
mskc_attach(struct device *parent, struct device *self, void *aux)
{
	struct sk_softc *sc = (struct sk_softc *)self;
	struct pci_attach_args *pa = aux;
	struct skc_attach_args skca;
	pci_chipset_tag_t pc = pa->pa_pc;
	pcireg_t memtype;
	pci_intr_handle_t ih;
	const char *intrstr = NULL;
	u_int8_t hw, pmd;
	char *revstr = NULL;
	caddr_t kva;

	DPRINTFN(2, ("begin mskc_attach\n"));

	pci_set_powerstate(pa->pa_pc, pa->pa_tag, PCI_PMCSR_STATE_D0);

	/*
	 * Map control/status registers.
	 */
	memtype = pci_mapreg_type(pc, pa->pa_tag, SK_PCI_LOMEM);
	if (pci_mapreg_map(pa, SK_PCI_LOMEM, memtype, 0, &sc->sk_btag,
	    &sc->sk_bhandle, NULL, &sc->sk_bsize, 0)) {
		printf(": can't map mem space\n");
		return;
	}

	sc->sc_dmatag = pa->pa_dmat;

	sc->sk_type = sk_win_read_1(sc, SK_CHIPVER);
	sc->sk_rev = (sk_win_read_1(sc, SK_CONFIG) >> 4);

	/* bail out here if chip is not recognized */
	if (!(SK_IS_YUKON2(sc))) {
		printf(": unknown chip type: %d\n", sc->sk_type);
		goto fail_1;
	}
	DPRINTFN(2, ("mskc_attach: allocate interrupt\n"));

	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_MARVELL) {
		switch (PCI_PRODUCT(pa->pa_id)) {
		case PCI_PRODUCT_MARVELL_YUKON_8036:
		case PCI_PRODUCT_MARVELL_YUKON_8053:
			pa->pa_flags &= ~PCI_FLAGS_MSI_ENABLED;
		}
	}

	/* Allocate interrupt */
	if (pci_intr_map_msi(pa, &ih) != 0 && pci_intr_map(pa, &ih) != 0) {
		printf(": couldn't map interrupt\n");
		goto fail_1;
	}

	intrstr = pci_intr_string(pc, ih);
	sc->sk_intrhand = pci_intr_establish(pc, ih, IPL_NET, msk_intr, sc,
	    self->dv_xname);
	if (sc->sk_intrhand == NULL) {
		printf(": couldn't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		goto fail_1;
	}
	sc->sk_pc = pc;

	if (bus_dmamem_alloc(sc->sc_dmatag,
	    MSK_STATUS_RING_CNT * sizeof(uint64_t),
	    MSK_STATUS_RING_CNT * sizeof(uint64_t),
	    0, &sc->sk_status_seg, 1, &sc->sk_status_nseg,
	    BUS_DMA_NOWAIT | BUS_DMA_ZERO)) {
		printf(": can't alloc status buffers\n");
		goto fail_2;
	}

	if (bus_dmamem_map(sc->sc_dmatag,
	    &sc->sk_status_seg, sc->sk_status_nseg,
	    MSK_STATUS_RING_CNT * sizeof(uint64_t),
	    &kva, BUS_DMA_NOWAIT)) {
		printf(": can't map dma buffers (%zu bytes)\n",
		    MSK_STATUS_RING_CNT * sizeof(uint64_t));
		goto fail_3;
	}
	if (bus_dmamap_create(sc->sc_dmatag,
	    MSK_STATUS_RING_CNT * sizeof(uint64_t), 1,
	    MSK_STATUS_RING_CNT * sizeof(uint64_t), 0,
	    BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW | BUS_DMA_64BIT,
	    &sc->sk_status_map)) {
		printf(": can't create dma map\n");
		goto fail_4;
	}
	if (bus_dmamap_load(sc->sc_dmatag, sc->sk_status_map, kva,
	    MSK_STATUS_RING_CNT * sizeof(uint64_t),
	    NULL, BUS_DMA_NOWAIT)) {
		printf(": can't load dma map\n");
		goto fail_5;
	}
	sc->sk_status_ring = (uint64_t *)kva;

	/* Reset the adapter. */
	mskc_reset(sc);

	sc->sk_ramsize = sk_win_read_1(sc, SK_EPROM0) * 4096;
	DPRINTFN(2, ("mskc_attach: ramsize=%dK\n", sc->sk_ramsize / 1024));

	pmd = sk_win_read_1(sc, SK_PMDTYPE);
	if (pmd == 'L' || pmd == 'S' || pmd == 'P')
		sc->sk_fibertype = 1;

	switch (sc->sk_type) {
	case SK_YUKON_XL:
		sc->sk_name = "Yukon-2 XL";
		break;
	case SK_YUKON_EC_U:
		sc->sk_name = "Yukon-2 EC Ultra";
		break;
	case SK_YUKON_EX:
		sc->sk_name = "Yukon-2 Extreme";
		break;
	case SK_YUKON_EC:
		sc->sk_name = "Yukon-2 EC";
		break;
	case SK_YUKON_FE:
		sc->sk_name = "Yukon-2 FE";
		break;
	case SK_YUKON_FE_P:
		sc->sk_name = "Yukon-2 FE+";
		break;
	case SK_YUKON_SUPR:
		sc->sk_name = "Yukon-2 Supreme";
		break;
	case SK_YUKON_ULTRA2:
		sc->sk_name = "Yukon-2 Ultra 2";
		break;
	case SK_YUKON_OPTIMA:
		sc->sk_name = "Yukon-2 Optima";
		break;
	case SK_YUKON_PRM:
		sc->sk_name = "Yukon-2 Optima Prime";
		break;
	case SK_YUKON_OPTIMA2:
		sc->sk_name = "Yukon-2 Optima 2";
		break;
	default:
		sc->sk_name = "Yukon (Unknown)";
	}

	if (sc->sk_type == SK_YUKON_XL) {
		switch (sc->sk_rev) {
		case SK_YUKON_XL_REV_A0:
			revstr = "A0";
			break;
		case SK_YUKON_XL_REV_A1:
			revstr = "A1";
			break;
		case SK_YUKON_XL_REV_A2:
			revstr = "A2";
			break;
		case SK_YUKON_XL_REV_A3:
			revstr = "A3";
			break;
		default:
			;
		}
	}

	if (sc->sk_type == SK_YUKON_EC) {
		switch (sc->sk_rev) {
		case SK_YUKON_EC_REV_A1:
			revstr = "A1";
			break;
		case SK_YUKON_EC_REV_A2:
			revstr = "A2";
			break;
		case SK_YUKON_EC_REV_A3:
			revstr = "A3";
			break;
		default:
			;
		}
	}

	if (sc->sk_type == SK_YUKON_EC_U) {
		switch (sc->sk_rev) {
		case SK_YUKON_EC_U_REV_A0:
			revstr = "A0";
			break;
		case SK_YUKON_EC_U_REV_A1:
			revstr = "A1";
			break;
		case SK_YUKON_EC_U_REV_B0:
			revstr = "B0";
			break;
		case SK_YUKON_EC_U_REV_B1:
			revstr = "B1";
			break;
		default:
			;
		}
	}

	if (sc->sk_type == SK_YUKON_FE) {
		switch (sc->sk_rev) {
		case SK_YUKON_FE_REV_A1:
			revstr = "A1";
			break;
		case SK_YUKON_FE_REV_A2:
			revstr = "A2";
			break;
		default:
			;
		}
	}

	if (sc->sk_type == SK_YUKON_FE_P && sc->sk_rev == SK_YUKON_FE_P_REV_A0)
		revstr = "A0";

	if (sc->sk_type == SK_YUKON_EX) {
		switch (sc->sk_rev) {
		case SK_YUKON_EX_REV_A0:
			revstr = "A0";
			break;
		case SK_YUKON_EX_REV_B0:
			revstr = "B0";
			break;
		default:
			;
		}
	}

	if (sc->sk_type == SK_YUKON_SUPR) {
		switch (sc->sk_rev) {
		case SK_YUKON_SUPR_REV_A0:
			revstr = "A0";
			break;
		case SK_YUKON_SUPR_REV_B0:
			revstr = "B0";
			break;
		case SK_YUKON_SUPR_REV_B1:
			revstr = "B1";
			break;
		default:
			;
		}
	}

	if (sc->sk_type == SK_YUKON_PRM) {
		switch (sc->sk_rev) {
		case SK_YUKON_PRM_REV_Z1:
			revstr = "Z1";
			break;
		case SK_YUKON_PRM_REV_A0:
			revstr = "A0";
			break;
		default:
			;
		}
	}

	/* Announce the product name. */
	printf(", %s", sc->sk_name);
	if (revstr != NULL)
		printf(" rev. %s", revstr);
	printf(" (0x%x): %s\n", sc->sk_rev, intrstr);

	sc->sk_macs = 1;

	hw = sk_win_read_1(sc, SK_Y2_HWRES);
	if ((hw & SK_Y2_HWRES_LINK_MASK) == SK_Y2_HWRES_LINK_DUAL) {
		if ((sk_win_read_1(sc, SK_Y2_CLKGATE) &
		    SK_Y2_CLKGATE_LINK2_INACTIVE) == 0)
			sc->sk_macs++;
	}

	skca.skc_port = SK_PORT_A;
	skca.skc_type = sc->sk_type;
	skca.skc_rev = sc->sk_rev;
	(void)config_found(&sc->sk_dev, &skca, mskcprint);

	if (sc->sk_macs > 1) {
		skca.skc_port = SK_PORT_B;
		skca.skc_type = sc->sk_type;
		skca.skc_rev = sc->sk_rev;
		(void)config_found(&sc->sk_dev, &skca, mskcprint);
	}

	/* Turn on the 'driver is loaded' LED. */
	CSR_WRITE_2(sc, SK_LED, SK_LED_GREEN_ON);

	return;

fail_4:
	bus_dmamem_unmap(sc->sc_dmatag, (caddr_t)sc->sk_status_ring,
	    MSK_STATUS_RING_CNT * sizeof(uint64_t));
fail_3:
	bus_dmamem_free(sc->sc_dmatag,
	    &sc->sk_status_seg, sc->sk_status_nseg);
	sc->sk_status_nseg = 0;
fail_5:
	bus_dmamap_destroy(sc->sc_dmatag, sc->sk_status_map);
fail_2:
	pci_intr_disestablish(sc->sk_pc, sc->sk_intrhand);
	sc->sk_intrhand = NULL;
fail_1:
	bus_space_unmap(sc->sk_btag, sc->sk_bhandle, sc->sk_bsize);
	sc->sk_bsize = 0;
}

int
mskc_detach(struct device *self, int flags)
{
	struct sk_softc *sc = (struct sk_softc *)self;
	int rv;

	if (sc->sk_intrhand)
		pci_intr_disestablish(sc->sk_pc, sc->sk_intrhand);

	rv = config_detach_children(self, flags);
	if (rv != 0)
		return (rv);

	if (sc->sk_status_nseg > 0) {
		bus_dmamap_destroy(sc->sc_dmatag, sc->sk_status_map);
		bus_dmamem_unmap(sc->sc_dmatag, (caddr_t)sc->sk_status_ring,
		    MSK_STATUS_RING_CNT * sizeof(uint64_t));
		bus_dmamem_free(sc->sc_dmatag,
		    &sc->sk_status_seg, sc->sk_status_nseg);
	}

	if (sc->sk_bsize > 0)
		bus_space_unmap(sc->sk_btag, sc->sk_bhandle, sc->sk_bsize);

	return(0);
}

int
mskc_activate(struct device *self, int act)
{
	struct sk_softc *sc = (void *)self;
	int rv = 0;

	switch (act) {
	case DVACT_RESUME:
		mskc_reset(sc);
		rv = config_activate_children(self, act);
		break;
	default:
		rv = config_activate_children(self, act);
		break;
	}
	return (rv);
}

static unsigned int
msk_encap(struct sk_if_softc *sc_if, struct mbuf *m, uint32_t prod)
{
	struct sk_softc		*sc = sc_if->sk_softc;
	struct msk_ring_data	*rd = sc_if->sk_rdata;
	struct msk_tx_desc	*t;
	bus_dmamap_t		map;
	uint64_t		addr;
	uint32_t		hiaddr;
	uint32_t		next, last;
	uint8_t			opcode;
	unsigned int		entries = 0;
	int			i;

	map = sc_if->sk_cdata.sk_tx_maps[prod];

	switch (bus_dmamap_load_mbuf(sc->sc_dmatag, map, m,
	    BUS_DMA_STREAMING | BUS_DMA_NOWAIT)) {
	case 0:
		break;
	case EFBIG: /* mbuf chain is too fragmented */
		if (m_defrag(m, M_DONTWAIT) == 0 &&
		    bus_dmamap_load_mbuf(sc->sc_dmatag, map, m,
		    BUS_DMA_STREAMING | BUS_DMA_NOWAIT) == 0)
			break;
		/* FALLTHROUGH */
	default:
		return (0);
	}

	bus_dmamap_sync(sc->sc_dmatag, map, 0, map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);

	opcode = SK_Y2_TXOPC_OWN | SK_Y2_TXOPC_PACKET;
	next = prod;
	for (i = 0; i < map->dm_nsegs; i++) {
		/* high 32 bits of address */
		addr = map->dm_segs[i].ds_addr;
		hiaddr = addr >> 32;
		if (sc_if->sk_cdata.sk_tx_hiaddr != hiaddr) {
			t = &rd->sk_tx_ring[next];
			htolem32(&t->sk_addr, hiaddr);
			t->sk_opcode = SK_Y2_TXOPC_OWN | SK_Y2_TXOPC_ADDR64;

			sc_if->sk_cdata.sk_tx_hiaddr = hiaddr;

			SK_INC(next, MSK_TX_RING_CNT);
			entries++;
		}

		/* low 32 bits of address + length */
		t = &rd->sk_tx_ring[next];
		htolem32(&t->sk_addr, addr);
		htolem16(&t->sk_len, map->dm_segs[i].ds_len);
		t->sk_ctl = 0;
		t->sk_opcode = opcode;

		last = next;
		SK_INC(next, MSK_TX_RING_CNT);
		entries++;

		opcode = SK_Y2_TXOPC_OWN | SK_Y2_TXOPC_BUFFER;
	}
	t->sk_ctl = SK_Y2_TXCTL_LASTFRAG;

	sc_if->sk_cdata.sk_tx_maps[prod] = sc_if->sk_cdata.sk_tx_maps[last];
	sc_if->sk_cdata.sk_tx_maps[last] = map;
	sc_if->sk_cdata.sk_tx_mbuf[last] = m;

	return (entries);
}

void
msk_start(struct ifnet *ifp)
{
	struct sk_if_softc	*sc_if = ifp->if_softc;
	struct mbuf		*m = NULL;
	uint32_t		prod, free, used;
	int			post = 0;

	prod = sc_if->sk_cdata.sk_tx_prod;
	free = sc_if->sk_cdata.sk_tx_cons;
	if (free <= prod)
		free += MSK_TX_RING_CNT;
	free -= prod;

	MSK_CDTXSYNC(sc_if, 0, MSK_TX_RING_CNT, BUS_DMASYNC_POSTWRITE);

	for (;;) {
		if (free <= SK_NTXSEG * 2) {
			ifq_set_oactive(&ifp->if_snd);
			break;
		}

		m = ifq_dequeue(&ifp->if_snd);
		if (m == NULL)
			break;

		used = msk_encap(sc_if, m, prod);
		if (used == 0) {
			m_freem(m);
			continue;
		}

		free -= used;
		prod += used;
		prod &= MSK_TX_RING_CNT - 1;

#if NBPFILTER > 0
		if (ifp->if_bpf)
			bpf_mtap(ifp->if_bpf, m, BPF_DIRECTION_OUT);
#endif
		post = 1;
	}

	MSK_CDTXSYNC(sc_if, 0, MSK_TX_RING_CNT, BUS_DMASYNC_PREWRITE);

	if (post == 0)
		return;

	/* Transmit */
	sc_if->sk_cdata.sk_tx_prod = prod;
	SK_IF_WRITE_2(sc_if, 1, SK_TXQA1_Y2_PREF_PUTIDX, prod);

	/* Set a timeout in case the chip goes out to lunch. */
	ifp->if_timer = MSK_TX_TIMEOUT;
}

void
msk_watchdog(struct ifnet *ifp)
{
	struct sk_if_softc *sc_if = ifp->if_softc;

	if (sc_if->sk_cdata.sk_tx_prod != sc_if->sk_cdata.sk_tx_cons) {
		printf("%s: watchdog timeout\n", sc_if->sk_dev.dv_xname);

		ifp->if_oerrors++;

		/* XXX Resets both ports; we shouldn't do that. */
		mskc_reset(sc_if->sk_softc);
		msk_reset(sc_if);
		msk_init(sc_if);
	}
}

static inline int
msk_rxvalid(struct sk_softc *sc, u_int32_t stat, u_int32_t len)
{
	if ((stat & (YU_RXSTAT_CRCERR | YU_RXSTAT_LONGERR |
	    YU_RXSTAT_MIIERR | YU_RXSTAT_BADFC | YU_RXSTAT_GOODFC |
	    YU_RXSTAT_JABBER)) != 0 ||
	    (stat & YU_RXSTAT_RXOK) != YU_RXSTAT_RXOK ||
	    YU_RXSTAT_BYTES(stat) != len)
		return (0);

	return (1);
}

void
msk_rxeof(struct sk_if_softc *sc_if, struct mbuf_list *ml,
    uint16_t len, uint32_t rxstat)
{
	struct sk_softc		*sc = sc_if->sk_softc;
	struct ifnet		*ifp = &sc_if->arpcom.ac_if;
	struct mbuf		*m = NULL;
	int			prod, cons, tail;
	bus_dmamap_t		map;

	prod = sc_if->sk_cdata.sk_rx_prod;
	cons = sc_if->sk_cdata.sk_rx_cons;

	while (cons != prod) {
		tail = cons;
	  	SK_INC(cons, MSK_RX_RING_CNT);

		m = sc_if->sk_cdata.sk_rx_mbuf[tail];
		if (m != NULL) {
			/* found it */
			break;
		}
	}
	sc_if->sk_cdata.sk_rx_cons = cons;

	if (m == NULL) {
		/* maybe if ADDR64 is consumed? */
		return;
	}

	sc_if->sk_cdata.sk_rx_mbuf[tail] = NULL;

	map = sc_if->sk_cdata.sk_rx_maps[tail];
	if_rxr_put(&sc_if->sk_cdata.sk_rx_ring, 1);

	bus_dmamap_sync(sc_if->sk_softc->sc_dmatag, map, 0, map->dm_mapsize,
	    BUS_DMASYNC_POSTREAD);
	bus_dmamap_unload(sc_if->sk_softc->sc_dmatag, map);

	if (len < SK_MIN_FRAMELEN || len > SK_JUMBO_FRAMELEN ||
	    msk_rxvalid(sc, rxstat, len) == 0) {
		ifp->if_ierrors++;
		m_freem(m);
		return;
	}

	m->m_pkthdr.len = m->m_len = len;

	ml_enqueue(ml, m);
}

void
msk_txeof(struct sk_if_softc *sc_if, unsigned int prod)
{
	struct ifnet		*ifp = &sc_if->arpcom.ac_if;
	struct sk_softc		*sc = sc_if->sk_softc;
	uint32_t		cons;
	struct mbuf		*m;
	bus_dmamap_t		map;

	/*
	 * Go through our tx ring and free mbufs for those
	 * frames that have been sent.
	 */
	cons = sc_if->sk_cdata.sk_tx_cons;

	if (cons == prod)
		return;

	while (cons != prod) {
		m = sc_if->sk_cdata.sk_tx_mbuf[cons];
		if (m != NULL) {
			sc_if->sk_cdata.sk_tx_mbuf[cons] = NULL;

			map = sc_if->sk_cdata.sk_tx_maps[cons];
			bus_dmamap_sync(sc->sc_dmatag, map, 0,
			    map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_dmatag, map);

			m_freem(m);
		}

		SK_INC(cons, MSK_TX_RING_CNT);
	}
	if (cons == sc_if->sk_cdata.sk_tx_prod)
		ifp->if_timer = 0;

	sc_if->sk_cdata.sk_tx_cons = cons;

	if (ifq_is_oactive(&ifp->if_snd))
		ifq_restart(&ifp->if_snd);
}

void
msk_fill_rx_ring(struct sk_if_softc *sc_if)
{
	u_int slots, used;

	slots = if_rxr_get(&sc_if->sk_cdata.sk_rx_ring, MSK_RX_RING_CNT/2);

	MSK_CDRXSYNC(sc_if, 0, BUS_DMASYNC_POSTWRITE); /* XXX */
	while (slots > 0) {
		used = msk_newbuf(sc_if);
		if (used == 0)
			break;

		slots -= used;
	}
	MSK_CDRXSYNC(sc_if, 0, BUS_DMASYNC_PREWRITE); /* XXX */

	if_rxr_put(&sc_if->sk_cdata.sk_rx_ring, slots);
	if (if_rxr_inuse(&sc_if->sk_cdata.sk_rx_ring) == 0)
		timeout_add(&sc_if->sk_tick_rx, 1);
}

void
msk_fill_rx_tick(void *xsc_if)
{
	struct sk_if_softc *sc_if = xsc_if;
	int s;

	s = splnet();
	if (if_rxr_inuse(&sc_if->sk_cdata.sk_rx_ring) == 0) {
		msk_fill_rx_ring(sc_if);
		SK_IF_WRITE_2(sc_if, 0, SK_RXQ1_Y2_PREF_PUTIDX,
		    sc_if->sk_cdata.sk_rx_prod);
	}
	splx(s);
}

void
msk_tick(void *xsc_if)
{
	struct sk_if_softc *sc_if = xsc_if;  
	struct mii_data *mii = &sc_if->sk_mii;
	int s;

	s = splnet();
	mii_tick(mii);
	splx(s);
	timeout_add_sec(&sc_if->sk_tick_ch, 1);
}

void
msk_intr_yukon(struct sk_if_softc *sc_if)
{
	u_int8_t status;

	status = SK_IF_READ_1(sc_if, 0, SK_GMAC_ISR);
	/* RX overrun */
	if ((status & SK_GMAC_INT_RX_OVER) != 0) {
		SK_IF_WRITE_1(sc_if, 0, SK_RXMF1_CTRL_TEST,
		    SK_RFCTL_RX_FIFO_OVER);
	}
	/* TX underrun */
	if ((status & SK_GMAC_INT_TX_UNDER) != 0) {
		SK_IF_WRITE_1(sc_if, 0, SK_TXMF1_CTRL_TEST,
		    SK_TFCTL_TX_FIFO_UNDER);
	}

	DPRINTFN(2, ("msk_intr_yukon status=%#x\n", status));
}

int
msk_intr(void *xsc)
{
	struct sk_softc		*sc = xsc;
	struct sk_if_softc	*sc_if0 = sc->sk_if[SK_PORT_A];
	struct sk_if_softc	*sc_if1 = sc->sk_if[SK_PORT_B];
	struct mbuf_list	ml[2] = {
					MBUF_LIST_INITIALIZER(),
					MBUF_LIST_INITIALIZER(),
				};
	struct ifnet		*ifp0 = NULL, *ifp1 = NULL;
	int			claimed = 0;
	u_int32_t		status;
	uint64_t		*ring = sc->sk_status_ring;
	uint64_t		desc;

	status = CSR_READ_4(sc, SK_Y2_ISSR2);
	if (status == 0xffffffff)
		return (0);
	if (status == 0) {
		CSR_WRITE_4(sc, SK_Y2_ICR, 2);
		return (0);
	}

	status = CSR_READ_4(sc, SK_ISR);

	if (sc_if0 != NULL)
		ifp0 = &sc_if0->arpcom.ac_if;
	if (sc_if1 != NULL)
		ifp1 = &sc_if1->arpcom.ac_if;

	if (sc_if0 && (status & SK_Y2_IMR_MAC1) &&
	    (ifp0->if_flags & IFF_RUNNING)) {
		msk_intr_yukon(sc_if0);
	}

	if (sc_if1 && (status & SK_Y2_IMR_MAC2) &&
	    (ifp1->if_flags & IFF_RUNNING)) {
		msk_intr_yukon(sc_if1);
	}

	MSK_CDSTSYNC(sc, sc->sk_status_idx,
	    BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE);

	while (MSK_STATUS_OWN(desc = lemtoh64(&ring[sc->sk_status_idx]))) {
		unsigned int opcode, port;

		ring[sc->sk_status_idx] = htole64(0); /* clear ownership */

		opcode = MSK_STATUS_OPCODE(desc);
		switch (opcode) {
		case MSK_STATUS_OPCODE_RXSTAT:
			port = MSK_STATUS_RXSTAT_PORT(desc);
			msk_rxeof(sc->sk_if[port], &ml[port],
			    MSK_STATUS_RXSTAT_LEN(desc),
			    MSK_STATUS_RXSTAT_STATUS(desc));
			break;
		case SK_Y2_STOPC_TXSTAT:
			if (sc_if0) {
				msk_txeof(sc_if0,
				    MSK_STATUS_TXIDX_PORTA(desc));
			}
			if (sc_if1) {
				msk_txeof(sc_if1,
				    MSK_STATUS_TXIDX_PORTB(desc));
			}
			break;
		default:
			printf("opcode=0x%x\n", opcode);
			break;
		}

		SK_INC(sc->sk_status_idx, MSK_STATUS_RING_CNT);
	}

	MSK_CDSTSYNC(sc, sc->sk_status_idx,
	    BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE);

	if (status & SK_Y2_IMR_BMU) {
		CSR_WRITE_4(sc, SK_STAT_BMU_CSR, SK_STAT_BMU_IRQ_CLEAR);
		claimed = 1;
	}

	CSR_WRITE_4(sc, SK_Y2_ICR, 2);

	if (!ml_empty(&ml[0])) {
		if (ifiq_input(&ifp0->if_rcv, &ml[0]))
			if_rxr_livelocked(&sc_if0->sk_cdata.sk_rx_ring);
		msk_fill_rx_ring(sc_if0);
		SK_IF_WRITE_2(sc_if0, 0, SK_RXQ1_Y2_PREF_PUTIDX,
		    sc_if0->sk_cdata.sk_rx_prod);
	}
	if (!ml_empty(&ml[1])) {
		if (ifiq_input(&ifp1->if_rcv, &ml[1]))
			if_rxr_livelocked(&sc_if1->sk_cdata.sk_rx_ring);
		msk_fill_rx_ring(sc_if1);
		SK_IF_WRITE_2(sc_if1, 0, SK_RXQ1_Y2_PREF_PUTIDX,
		    sc_if1->sk_cdata.sk_rx_prod);
	}

	return (claimed);
}

void
msk_init_yukon(struct sk_if_softc *sc_if)
{
	u_int32_t		v;
	u_int16_t		reg;
	struct sk_softc		*sc;
	int			i;

	sc = sc_if->sk_softc;

	DPRINTFN(2, ("msk_init_yukon: start: sk_csr=%#x\n",
		     CSR_READ_4(sc_if->sk_softc, SK_CSR)));

	DPRINTFN(6, ("msk_init_yukon: 1\n"));

	DPRINTFN(3, ("msk_init_yukon: gmac_ctrl=%#x\n",
		     SK_IF_READ_4(sc_if, 0, SK_GMAC_CTRL)));

	DPRINTFN(6, ("msk_init_yukon: 3\n"));

	/* unused read of the interrupt source register */
	DPRINTFN(6, ("msk_init_yukon: 4\n"));
	SK_IF_READ_2(sc_if, 0, SK_GMAC_ISR);

	DPRINTFN(6, ("msk_init_yukon: 4a\n"));
	reg = SK_YU_READ_2(sc_if, YUKON_PAR);
	DPRINTFN(6, ("msk_init_yukon: YUKON_PAR=%#x\n", reg));

	/* MIB Counter Clear Mode set */
        reg |= YU_PAR_MIB_CLR;
	DPRINTFN(6, ("msk_init_yukon: YUKON_PAR=%#x\n", reg));
	DPRINTFN(6, ("msk_init_yukon: 4b\n"));
	SK_YU_WRITE_2(sc_if, YUKON_PAR, reg);

	/* MIB Counter Clear Mode clear */
	DPRINTFN(6, ("msk_init_yukon: 5\n"));
        reg &= ~YU_PAR_MIB_CLR;
	SK_YU_WRITE_2(sc_if, YUKON_PAR, reg);

	/* receive control reg */
	DPRINTFN(6, ("msk_init_yukon: 7\n"));
	SK_YU_WRITE_2(sc_if, YUKON_RCR, YU_RCR_CRCR);

	/* transmit parameter register */
	DPRINTFN(6, ("msk_init_yukon: 8\n"));
	SK_YU_WRITE_2(sc_if, YUKON_TPR, YU_TPR_JAM_LEN(0x3) |
		      YU_TPR_JAM_IPG(0xb) | YU_TPR_JAM2DATA_IPG(0x1a) );

	/* serial mode register */
	DPRINTFN(6, ("msk_init_yukon: 9\n"));
	reg = YU_SMR_DATA_BLIND(0x1c) |
	      YU_SMR_MFL_VLAN |
	      YU_SMR_IPG_DATA(0x1e);

	if (sc->sk_type != SK_YUKON_FE &&
	    sc->sk_type != SK_YUKON_FE_P)
		reg |= YU_SMR_MFL_JUMBO;

	SK_YU_WRITE_2(sc_if, YUKON_SMR, reg);

	DPRINTFN(6, ("msk_init_yukon: 10\n"));
	/* Setup Yukon's address */
	for (i = 0; i < 3; i++) {
		/* Write Source Address 1 (unicast filter) */
		SK_YU_WRITE_2(sc_if, YUKON_SAL1 + i * 4, 
			      sc_if->arpcom.ac_enaddr[i * 2] |
			      sc_if->arpcom.ac_enaddr[i * 2 + 1] << 8);
	}

	for (i = 0; i < 3; i++) {
		reg = sk_win_read_2(sc_if->sk_softc,
				    SK_MAC1_0 + i * 2 + sc_if->sk_port * 8);
		SK_YU_WRITE_2(sc_if, YUKON_SAL2 + i * 4, reg);
	}

	/* Program promiscuous mode and multicast filters */
	DPRINTFN(6, ("msk_init_yukon: 11\n"));
	msk_iff(sc_if);

	/* enable interrupt mask for counter overflows */
	DPRINTFN(6, ("msk_init_yukon: 12\n"));
	SK_YU_WRITE_2(sc_if, YUKON_TIMR, 0);
	SK_YU_WRITE_2(sc_if, YUKON_RIMR, 0);
	SK_YU_WRITE_2(sc_if, YUKON_TRIMR, 0);

	/* Configure RX MAC FIFO Flush Mask */
	v = YU_RXSTAT_FOFL | YU_RXSTAT_CRCERR | YU_RXSTAT_MIIERR |
	    YU_RXSTAT_BADFC | YU_RXSTAT_GOODFC | YU_RXSTAT_RUNT |
	    YU_RXSTAT_JABBER;
	SK_IF_WRITE_2(sc_if, 0, SK_RXMF1_FLUSH_MASK, v);

	/* Configure RX MAC FIFO */
	SK_IF_WRITE_1(sc_if, 0, SK_RXMF1_CTRL_TEST, SK_RFCTL_RESET_CLEAR);
	SK_IF_WRITE_2(sc_if, 0, SK_RXMF1_CTRL_TEST, SK_RFCTL_OPERATION_ON |
	    SK_RFCTL_FIFO_FLUSH_ON);

	/* Increase flush threshold to 64 bytes */
	SK_IF_WRITE_2(sc_if, 0, SK_RXMF1_FLUSH_THRESHOLD,
	    SK_RFCTL_FIFO_THRESHOLD + 1);

	/* Configure TX MAC FIFO */
	SK_IF_WRITE_1(sc_if, 0, SK_TXMF1_CTRL_TEST, SK_TFCTL_RESET_CLEAR);
	SK_IF_WRITE_2(sc_if, 0, SK_TXMF1_CTRL_TEST, SK_TFCTL_OPERATION_ON);

#if 1
	SK_YU_WRITE_2(sc_if, YUKON_GPCR, YU_GPCR_TXEN | YU_GPCR_RXEN);
#endif
	DPRINTFN(6, ("msk_init_yukon: end\n"));
}

/*
 * Note that to properly initialize any part of the GEnesis chip,
 * you first have to take it out of reset mode.
 */
void
msk_init(void *xsc_if)
{
	struct sk_if_softc	*sc_if = xsc_if;
	struct sk_softc		*sc = sc_if->sk_softc;
	struct ifnet		*ifp = &sc_if->arpcom.ac_if;
	struct mii_data		*mii = &sc_if->sk_mii;
	int			s;

	DPRINTFN(2, ("msk_init\n"));

	s = splnet();

	/* Cancel pending I/O and free all RX/TX buffers. */
	msk_stop(sc_if, 0);

	/* Configure I2C registers */

	/* Configure XMAC(s) */
	msk_init_yukon(sc_if);
	mii_mediachg(mii);

	/* Configure transmit arbiter(s) */
	SK_IF_WRITE_1(sc_if, 0, SK_TXAR1_COUNTERCTL, SK_TXARCTL_ON);
#if 0
	    SK_TXARCTL_ON|SK_TXARCTL_FSYNC_ON);
#endif

	/* Configure RAMbuffers */
	SK_IF_WRITE_4(sc_if, 0, SK_RXRB1_CTLTST, SK_RBCTL_UNRESET);
	SK_IF_WRITE_4(sc_if, 0, SK_RXRB1_START, sc_if->sk_rx_ramstart);
	SK_IF_WRITE_4(sc_if, 0, SK_RXRB1_WR_PTR, sc_if->sk_rx_ramstart);
	SK_IF_WRITE_4(sc_if, 0, SK_RXRB1_RD_PTR, sc_if->sk_rx_ramstart);
	SK_IF_WRITE_4(sc_if, 0, SK_RXRB1_END, sc_if->sk_rx_ramend);
	SK_IF_WRITE_4(sc_if, 0, SK_RXRB1_CTLTST, SK_RBCTL_ON);

	SK_IF_WRITE_4(sc_if, 1, SK_TXRBA1_CTLTST, SK_RBCTL_UNRESET);
	SK_IF_WRITE_4(sc_if, 1, SK_TXRBA1_CTLTST, SK_RBCTL_STORENFWD_ON);
	SK_IF_WRITE_4(sc_if, 1, SK_TXRBA1_START, sc_if->sk_tx_ramstart);
	SK_IF_WRITE_4(sc_if, 1, SK_TXRBA1_WR_PTR, sc_if->sk_tx_ramstart);
	SK_IF_WRITE_4(sc_if, 1, SK_TXRBA1_RD_PTR, sc_if->sk_tx_ramstart);
	SK_IF_WRITE_4(sc_if, 1, SK_TXRBA1_END, sc_if->sk_tx_ramend);
	SK_IF_WRITE_4(sc_if, 1, SK_TXRBA1_CTLTST, SK_RBCTL_ON);

	/* Configure BMUs */
	SK_IF_WRITE_4(sc_if, 0, SK_RXQ1_BMU_CSR, 0x00000016);
	SK_IF_WRITE_4(sc_if, 0, SK_RXQ1_BMU_CSR, 0x00000d28);
	SK_IF_WRITE_4(sc_if, 0, SK_RXQ1_BMU_CSR, 0x00000080);
	SK_IF_WRITE_4(sc_if, 0, SK_RXQ1_WATERMARK, 0x00000600);

	SK_IF_WRITE_4(sc_if, 1, SK_TXQA1_BMU_CSR, 0x00000016);
	SK_IF_WRITE_4(sc_if, 1, SK_TXQA1_BMU_CSR, 0x00000d28);
	SK_IF_WRITE_4(sc_if, 1, SK_TXQA1_BMU_CSR, 0x00000080);
	SK_IF_WRITE_4(sc_if, 1, SK_TXQA1_WATERMARK, 0x00000600);

	/* Make sure the sync transmit queue is disabled. */
	SK_IF_WRITE_4(sc_if, 1, SK_TXRBS1_CTLTST, SK_RBCTL_RESET);

	/* Init descriptors */
	if (msk_init_rx_ring(sc_if) == ENOBUFS) {
		printf("%s: initialization failed: no "
		    "memory for rx buffers\n", sc_if->sk_dev.dv_xname);
		msk_stop(sc_if, 0);
		splx(s);
		return;
	}

	if (msk_init_tx_ring(sc_if) == ENOBUFS) {
		printf("%s: initialization failed: no "
		    "memory for tx buffers\n", sc_if->sk_dev.dv_xname);
		msk_stop(sc_if, 0);
		splx(s);
		return;
	}

	/* Initialize prefetch engine. */
	SK_IF_WRITE_4(sc_if, 0, SK_RXQ1_Y2_PREF_CSR, 0x00000001);
	SK_IF_WRITE_4(sc_if, 0, SK_RXQ1_Y2_PREF_CSR, 0x00000002);
	SK_IF_WRITE_2(sc_if, 0, SK_RXQ1_Y2_PREF_LIDX, MSK_RX_RING_CNT - 1);
	SK_IF_WRITE_4(sc_if, 0, SK_RXQ1_Y2_PREF_ADDRLO,
	    MSK_RX_RING_ADDR(sc_if, 0));
	SK_IF_WRITE_4(sc_if, 0, SK_RXQ1_Y2_PREF_ADDRHI,
	    (u_int64_t)MSK_RX_RING_ADDR(sc_if, 0) >> 32);
	SK_IF_WRITE_4(sc_if, 0, SK_RXQ1_Y2_PREF_CSR, 0x00000008);
	SK_IF_READ_4(sc_if, 0, SK_RXQ1_Y2_PREF_CSR);

	SK_IF_WRITE_4(sc_if, 1, SK_TXQA1_Y2_PREF_CSR, 0x00000001);
	SK_IF_WRITE_4(sc_if, 1, SK_TXQA1_Y2_PREF_CSR, 0x00000002);
	SK_IF_WRITE_2(sc_if, 1, SK_TXQA1_Y2_PREF_LIDX, MSK_TX_RING_CNT - 1);
	SK_IF_WRITE_4(sc_if, 1, SK_TXQA1_Y2_PREF_ADDRLO,
	    MSK_TX_RING_ADDR(sc_if, 0));
	SK_IF_WRITE_4(sc_if, 1, SK_TXQA1_Y2_PREF_ADDRHI,
	    (u_int64_t)MSK_TX_RING_ADDR(sc_if, 0) >> 32);
	SK_IF_WRITE_4(sc_if, 1, SK_TXQA1_Y2_PREF_CSR, 0x00000008);
	SK_IF_READ_4(sc_if, 1, SK_TXQA1_Y2_PREF_CSR);

	SK_IF_WRITE_2(sc_if, 0, SK_RXQ1_Y2_PREF_PUTIDX,
	    sc_if->sk_cdata.sk_rx_prod);

	/*
	 * tell the chip the tx ring is empty for now. the first
	 * msk_start will end up posting the ADDR64 tx descriptor
	 * that resets the high address.
	 */
	SK_IF_WRITE_2(sc_if, 1, SK_TXQA1_Y2_PREF_PUTIDX, 0);

	/* Configure interrupt handling */
	if (sc_if->sk_port == SK_PORT_A)
		sc->sk_intrmask |= SK_Y2_INTRS1;
	else
		sc->sk_intrmask |= SK_Y2_INTRS2;
	sc->sk_intrmask |= SK_Y2_IMR_BMU;
	CSR_WRITE_4(sc, SK_IMR, sc->sk_intrmask);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	timeout_add_sec(&sc_if->sk_tick_ch, 1);

	splx(s);
}

void
msk_stop(struct sk_if_softc *sc_if, int softonly)
{
	struct sk_softc		*sc = sc_if->sk_softc;
	struct ifnet		*ifp = &sc_if->arpcom.ac_if;
	struct mbuf		*m;
	bus_dmamap_t		map;
	int			i;

	DPRINTFN(2, ("msk_stop\n"));

	timeout_del(&sc_if->sk_tick_ch);
	timeout_del(&sc_if->sk_tick_rx);

	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	/* Stop transfer of Tx descriptors */

	/* Stop transfer of Rx descriptors */

	if (!softonly) {
		/* Turn off various components of this interface. */
		SK_IF_WRITE_1(sc_if,0, SK_RXMF1_CTRL_TEST, SK_RFCTL_RESET_SET);
		SK_IF_WRITE_1(sc_if,0, SK_TXMF1_CTRL_TEST, SK_TFCTL_RESET_SET);
		SK_IF_WRITE_4(sc_if, 0, SK_RXQ1_BMU_CSR, SK_RXBMU_OFFLINE);
		SK_IF_WRITE_4(sc_if, 0, SK_RXRB1_CTLTST, SK_RBCTL_RESET|SK_RBCTL_OFF);
		SK_IF_WRITE_4(sc_if, 1, SK_TXQA1_BMU_CSR, SK_TXBMU_OFFLINE);
		SK_IF_WRITE_4(sc_if, 1, SK_TXRBA1_CTLTST, SK_RBCTL_RESET|SK_RBCTL_OFF);
		SK_IF_WRITE_1(sc_if, 0, SK_TXAR1_COUNTERCTL, SK_TXARCTL_OFF);
		SK_IF_WRITE_1(sc_if, 0, SK_RXLED1_CTL, SK_RXLEDCTL_COUNTER_STOP);
		SK_IF_WRITE_1(sc_if, 0, SK_TXLED1_CTL, SK_TXLEDCTL_COUNTER_STOP);
		SK_IF_WRITE_1(sc_if, 0, SK_LINKLED1_CTL, SK_LINKLED_OFF);
		SK_IF_WRITE_1(sc_if, 0, SK_LINKLED1_CTL, SK_LINKLED_LINKSYNC_OFF);

		SK_IF_WRITE_4(sc_if, 0, SK_RXQ1_Y2_PREF_CSR, 0x00000001);
		SK_IF_WRITE_4(sc_if, 1, SK_TXQA1_Y2_PREF_CSR, 0x00000001);

		/* Disable interrupts */
		if (sc_if->sk_port == SK_PORT_A)
			sc->sk_intrmask &= ~SK_Y2_INTRS1;
		else
			sc->sk_intrmask &= ~SK_Y2_INTRS2;
		CSR_WRITE_4(sc, SK_IMR, sc->sk_intrmask);
	}

	/* Free RX and TX mbufs still in the queues. */
	for (i = 0; i < MSK_RX_RING_CNT; i++) {
		m = sc_if->sk_cdata.sk_rx_mbuf[i];
		if (m == NULL)
			continue;

		map = sc_if->sk_cdata.sk_rx_maps[i];
		bus_dmamap_sync(sc->sc_dmatag, map, 0, map->dm_mapsize,
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->sc_dmatag, map);

		m_freem(m);

		sc_if->sk_cdata.sk_rx_mbuf[i] = NULL;
	}

	sc_if->sk_cdata.sk_rx_prod = 0;
	sc_if->sk_cdata.sk_rx_cons = 0;

	for (i = 0; i < MSK_TX_RING_CNT; i++) {
		m = sc_if->sk_cdata.sk_tx_mbuf[i];
		if (m == NULL)
			continue;

		map = sc_if->sk_cdata.sk_tx_maps[i];
		bus_dmamap_sync(sc->sc_dmatag, map, 0, map->dm_mapsize,
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->sc_dmatag, map);

		m_freem(m);

		sc_if->sk_cdata.sk_tx_mbuf[i] = NULL;
	}
}

const struct cfattach mskc_ca = {
	sizeof(struct sk_softc), mskc_probe, mskc_attach, mskc_detach,
	mskc_activate
};

struct cfdriver mskc_cd = {
	NULL, "mskc", DV_DULL
};

const struct cfattach msk_ca = {
	sizeof(struct sk_if_softc), msk_probe, msk_attach, msk_detach,
	msk_activate
};

struct cfdriver msk_cd = {
	NULL, "msk", DV_IFNET
};

#if NKSTAT > 0
static uint32_t
msk_mib_read32(struct sk_if_softc *sc_if, uint32_t r)
{
	uint16_t hi, lo, xx;

	hi = SK_YU_READ_2(sc_if, r + 4);
	for (;;) {
		/* XXX barriers? */
		lo = SK_YU_READ_2(sc_if, r);
		xx = SK_YU_READ_2(sc_if, r + 4);

		if (hi == xx)
			break;

		hi = xx;
	}

	return (((uint32_t)hi << 16) | (uint32_t) lo);
}

static uint64_t
msk_mib_read64(struct sk_if_softc *sc_if, uint32_t r)
{
	uint32_t hi, lo, xx;

	hi = msk_mib_read32(sc_if, r + 8);
	for (;;) {
		lo = msk_mib_read32(sc_if, r);
		xx = msk_mib_read32(sc_if, r + 8);

		if (hi == xx)
			break;

		hi = xx;
	}

	return (((uint64_t)hi << 32) | (uint64_t)lo);
}

void
msk_kstat_attach(struct sk_if_softc *sc_if)
{
	struct kstat *ks;
	struct kstat_kv *kvs;
	struct msk_kstat *mks;
	size_t i;

	ks = kstat_create(sc_if->sk_dev.dv_xname, 0, "msk-mib", 0,
	    KSTAT_T_KV, 0);
	if (ks == NULL) {
		/* oh well */
		return;
	}

	mks = malloc(sizeof(*mks), M_DEVBUF, M_WAITOK);
	rw_init(&mks->lock, "mskstat");
	mks->ks = ks;

	kvs = mallocarray(nitems(msk_mib), sizeof(*kvs),
	    M_DEVBUF, M_WAITOK|M_ZERO);
	for (i = 0; i < nitems(msk_mib); i++) {
		const struct msk_mib *m = &msk_mib[i];
		kstat_kv_unit_init(&kvs[i], m->name, m->type, m->unit);
	}

	ks->ks_softc = sc_if;
	ks->ks_data = kvs;
	ks->ks_datalen = nitems(msk_mib) * sizeof(*kvs);
	ks->ks_read = msk_kstat_read;
	kstat_set_wlock(ks, &mks->lock);

	kstat_install(ks);

	sc_if->sk_kstat = mks;
}

void
msk_kstat_detach(struct sk_if_softc *sc_if)
{
	struct msk_kstat *mks = sc_if->sk_kstat;
	struct kstat_kv *kvs;
	size_t kvslen;

	if (mks == NULL)
		return;

	sc_if->sk_kstat = NULL;

	kvs = mks->ks->ks_data;
	kvslen = mks->ks->ks_datalen;

	kstat_destroy(mks->ks);
	free(kvs, M_DEVBUF, kvslen);
	free(mks, M_DEVBUF, sizeof(*mks));
}

int
msk_kstat_read(struct kstat *ks)
{
	struct sk_if_softc *sc_if = ks->ks_softc;
	struct kstat_kv *kvs = ks->ks_data;
	size_t i;

	nanouptime(&ks->ks_updated);

	for (i = 0; i < nitems(msk_mib); i++) {
		const struct msk_mib *m = &msk_mib[i];

		switch (m->type) {
		case KSTAT_KV_T_COUNTER32:
			kstat_kv_u32(&kvs[i]) = msk_mib_read32(sc_if, m->reg);
			break;
		case KSTAT_KV_T_COUNTER64:
			kstat_kv_u64(&kvs[i]) = msk_mib_read64(sc_if, m->reg);
			break;
		default:
			panic("unexpected msk_mib type");
			/* NOTREACHED */
		}
	}

	return (0);
}
#endif /* NKSTAT */

#ifdef MSK_DEBUG
void
msk_dump_txdesc(struct msk_tx_desc *le, int idx)
{
#define DESC_PRINT(X)					\
	if (X)					\
		printf("txdesc[%d]." #X "=%#x\n",	\
		       idx, X);

	DESC_PRINT(letoh32(le->sk_addr));
	DESC_PRINT(letoh16(le->sk_len));
	DESC_PRINT(le->sk_ctl);
	DESC_PRINT(le->sk_opcode);
#undef DESC_PRINT
}

void
msk_dump_bytes(const char *data, int len)
{
	int c, i, j;

	for (i = 0; i < len; i += 16) {
		printf("%08x  ", i);
		c = len - i;
		if (c > 16) c = 16;

		for (j = 0; j < c; j++) {
			printf("%02x ", data[i + j] & 0xff);
			if ((j & 0xf) == 7 && j > 0)
				printf(" ");
		}
		
		for (; j < 16; j++)
			printf("   ");
		printf("  ");

		for (j = 0; j < c; j++) {
			int ch = data[i + j] & 0xff;
			printf("%c", ' ' <= ch && ch <= '~' ? ch : ' ');
		}
		
		printf("\n");
		
		if (c < 16)
			break;
	}
}

void
msk_dump_mbuf(struct mbuf *m)
{
	int count = m->m_pkthdr.len;

	printf("m=%#lx, m->m_pkthdr.len=%#d\n", m, m->m_pkthdr.len);

	while (count > 0 && m) {
		printf("m=%#lx, m->m_data=%#lx, m->m_len=%d\n",
		       m, m->m_data, m->m_len);
		msk_dump_bytes(mtod(m, char *), m->m_len);

		count -= m->m_len;
		m = m->m_next;
	}
}
#endif
