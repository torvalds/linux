/*	$OpenBSD: if_sk.c,v 1.200 2024/11/05 18:58:59 miod Exp $	*/

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

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>
#include <dev/mii/brgphyreg.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/pci/if_skreg.h>
#include <dev/pci/if_skvar.h>

int skc_probe(struct device *, void *, void *);
void skc_attach(struct device *, struct device *self, void *aux);
int skc_detach(struct device *, int);
int skc_activate(struct device *, int);
int sk_probe(struct device *, void *, void *);
void sk_attach(struct device *, struct device *self, void *aux);
int sk_detach(struct device *, int);
int sk_activate(struct device *, int);
int skcprint(void *, const char *);
int sk_intr(void *);
void sk_intr_bcom(struct sk_if_softc *);
void sk_intr_xmac(struct sk_if_softc *);
void sk_intr_yukon(struct sk_if_softc *);
static __inline int sk_rxvalid(struct sk_softc *, u_int32_t, u_int32_t);
void sk_rxeof(struct sk_if_softc *);
void sk_txeof(struct sk_if_softc *);
int sk_encap(struct sk_if_softc *, struct mbuf *, u_int32_t *);
void sk_start(struct ifnet *);
int sk_ioctl(struct ifnet *, u_long, caddr_t);
void sk_init(void *);
void sk_init_xmac(struct sk_if_softc *);
void sk_init_yukon(struct sk_if_softc *);
void sk_stop(struct sk_if_softc *, int softonly);
void sk_watchdog(struct ifnet *);
int sk_ifmedia_upd(struct ifnet *);
void sk_ifmedia_sts(struct ifnet *, struct ifmediareq *);
void skc_reset(struct sk_softc *);
int sk_newbuf(struct sk_if_softc *);
int sk_reset(struct sk_if_softc *);
int sk_init_rx_ring(struct sk_if_softc *);
void sk_fill_rx_ring(struct sk_if_softc *);
int sk_init_tx_ring(struct sk_if_softc *);

int sk_xmac_miibus_readreg(struct device *, int, int);
void sk_xmac_miibus_writereg(struct device *, int, int, int);
void sk_xmac_miibus_statchg(struct device *);

int sk_marv_miibus_readreg(struct device *, int, int);
void sk_marv_miibus_writereg(struct device *, int, int, int);
void sk_marv_miibus_statchg(struct device *);

void sk_setfilt(struct sk_if_softc *, caddr_t, int);
void sk_iff(struct sk_if_softc *);
void sk_iff_xmac(struct sk_if_softc *);
void sk_iff_yukon(struct sk_if_softc *);

void sk_tick(void *);
void sk_yukon_tick(void *);

#ifdef SK_DEBUG
#define DPRINTF(x)	if (skdebug) printf x
#define DPRINTFN(n,x)	if (skdebug >= (n)) printf x
int	skdebug = 0;

void sk_dump_txdesc(struct sk_tx_desc *, int);
void sk_dump_mbuf(struct mbuf *);
void sk_dump_bytes(const char *, int);
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

/* supported device vendors */
const struct pci_matchid skc_devices[] = {
	{ PCI_VENDOR_3COM,		PCI_PRODUCT_3COM_3C940 },
	{ PCI_VENDOR_3COM,		PCI_PRODUCT_3COM_3C940B },
	{ PCI_VENDOR_CNET,		PCI_PRODUCT_CNET_GIGACARD },
	{ PCI_VENDOR_DLINK,		PCI_PRODUCT_DLINK_DGE530T_A1 },
	{ PCI_VENDOR_DLINK,		PCI_PRODUCT_DLINK_DGE530T_B1 },
	{ PCI_VENDOR_LINKSYS,		PCI_PRODUCT_LINKSYS_EG1064 },
	{ PCI_VENDOR_MARVELL,		PCI_PRODUCT_MARVELL_YUKON },
	{ PCI_VENDOR_MARVELL,		PCI_PRODUCT_MARVELL_YUKON_BELKIN },
	{ PCI_VENDOR_SCHNEIDERKOCH,	PCI_PRODUCT_SCHNEIDERKOCH_SK98XX },
	{ PCI_VENDOR_SCHNEIDERKOCH,	PCI_PRODUCT_SCHNEIDERKOCH_SK98XX2 },
	{ PCI_VENDOR_SCHNEIDERKOCH,	PCI_PRODUCT_SCHNEIDERKOCH_SK9821 },
	{ PCI_VENDOR_SCHNEIDERKOCH,	PCI_PRODUCT_SCHNEIDERKOCH_SK9843 }
};

#define SK_LINKSYS_EG1032_SUBID 0x00151737

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
sk_xmac_miibus_readreg(struct device *dev, int phy, int reg)
{
	struct sk_if_softc *sc_if = (struct sk_if_softc *)dev;
	int i;

	DPRINTFN(9, ("sk_xmac_miibus_readreg\n"));

	if (sc_if->sk_phytype == SK_PHYTYPE_XMAC && phy != 0)
		return (0);

	SK_XM_WRITE_2(sc_if, XM_PHY_ADDR, reg|(phy << 8));
	SK_XM_READ_2(sc_if, XM_PHY_DATA);
	if (sc_if->sk_phytype != SK_PHYTYPE_XMAC) {
		for (i = 0; i < SK_TIMEOUT; i++) {
			DELAY(1);
			if (SK_XM_READ_2(sc_if, XM_MMUCMD) &
			    XM_MMUCMD_PHYDATARDY)
				break;
		}

		if (i == SK_TIMEOUT) {
			printf("%s: phy failed to come ready\n",
			    sc_if->sk_dev.dv_xname);
			return (0);
		}
	}
	DELAY(1);
	return (SK_XM_READ_2(sc_if, XM_PHY_DATA));
}

void
sk_xmac_miibus_writereg(struct device *dev, int phy, int reg, int val)
{
	struct sk_if_softc *sc_if = (struct sk_if_softc *)dev;
	int i;

	DPRINTFN(9, ("sk_xmac_miibus_writereg\n"));

	SK_XM_WRITE_2(sc_if, XM_PHY_ADDR, reg|(phy << 8));
	for (i = 0; i < SK_TIMEOUT; i++) {
		if (!(SK_XM_READ_2(sc_if, XM_MMUCMD) & XM_MMUCMD_PHYBUSY))
			break;
	}

	if (i == SK_TIMEOUT) {
		printf("%s: phy failed to come ready\n",
		    sc_if->sk_dev.dv_xname);
		return;
	}

	SK_XM_WRITE_2(sc_if, XM_PHY_DATA, val);
	for (i = 0; i < SK_TIMEOUT; i++) {
		DELAY(1);
		if (!(SK_XM_READ_2(sc_if, XM_MMUCMD) & XM_MMUCMD_PHYBUSY))
			break;
	}

	if (i == SK_TIMEOUT)
		printf("%s: phy write timed out\n", sc_if->sk_dev.dv_xname);
}

void
sk_xmac_miibus_statchg(struct device *dev)
{
	struct sk_if_softc *sc_if = (struct sk_if_softc *)dev;
	struct mii_data *mii = &sc_if->sk_mii;

	DPRINTFN(9, ("sk_xmac_miibus_statchg\n"));

	/*
	 * If this is a GMII PHY, manually set the XMAC's
	 * duplex mode accordingly.
	 */
	if (sc_if->sk_phytype != SK_PHYTYPE_XMAC) {
		if ((mii->mii_media_active & IFM_GMASK) == IFM_FDX)
			SK_XM_SETBIT_2(sc_if, XM_MMUCMD, XM_MMUCMD_GMIIFDX);
		else
			SK_XM_CLRBIT_2(sc_if, XM_MMUCMD, XM_MMUCMD_GMIIFDX);
	}
}

int
sk_marv_miibus_readreg(struct device *dev, int phy, int reg)
{
	struct sk_if_softc *sc_if = (struct sk_if_softc *)dev;
	u_int16_t val;
	int i;

	if (phy != 0 ||
	    (sc_if->sk_phytype != SK_PHYTYPE_MARV_COPPER &&
	     sc_if->sk_phytype != SK_PHYTYPE_MARV_FIBER)) {
		DPRINTFN(9, ("sk_marv_miibus_readreg (skip) phy=%d, reg=%#x\n",
		    phy, reg));
		return (0);
	}

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

 	DPRINTFN(9, ("sk_marv_miibus_readreg: i=%d, timeout=%d\n", i,
	    SK_TIMEOUT));

	val = SK_YU_READ_2(sc_if, YUKON_SMIDR);

	DPRINTFN(9, ("sk_marv_miibus_readreg phy=%d, reg=%#x, val=%#x\n",
	    phy, reg, val));

	return (val);
}

void
sk_marv_miibus_writereg(struct device *dev, int phy, int reg, int val)
{
	struct sk_if_softc *sc_if = (struct sk_if_softc *)dev;
	int i;

	DPRINTFN(9, ("sk_marv_miibus_writereg phy=%d reg=%#x val=%#x\n",
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
sk_marv_miibus_statchg(struct device *dev)
{
	DPRINTFN(9, ("sk_marv_miibus_statchg: gpcr=%x\n",
	    SK_YU_READ_2(((struct sk_if_softc *)dev), YUKON_GPCR)));
}

void
sk_setfilt(struct sk_if_softc *sc_if, caddr_t addr, int slot)
{
	int base = XM_RXFILT_ENTRY(slot);

	SK_XM_WRITE_2(sc_if, base, letoh16(*(u_int16_t *)(&addr[0])));
	SK_XM_WRITE_2(sc_if, base + 2, letoh16(*(u_int16_t *)(&addr[2])));
	SK_XM_WRITE_2(sc_if, base + 4, letoh16(*(u_int16_t *)(&addr[4])));
}

void
sk_iff(struct sk_if_softc *sc_if)
{
	struct sk_softc *sc = sc_if->sk_softc;

	if (SK_IS_GENESIS(sc))
		sk_iff_xmac(sc_if);
	else
		sk_iff_yukon(sc_if);
}

void
sk_iff_xmac(struct sk_if_softc *sc_if)
{
	struct ifnet *ifp = &sc_if->arpcom.ac_if;
	struct arpcom *ac = &sc_if->arpcom;
	struct ether_multi *enm;
	struct ether_multistep step;
	u_int32_t reg, hashes[2];
	u_int8_t dummy[] = { 0, 0, 0, 0, 0 ,0 };
	int h, i;

	reg = SK_XM_READ_4(sc_if, XM_MODE);
	reg &= ~(XM_MODE_RX_NOBROAD | XM_MODE_RX_PROMISC | XM_MODE_RX_USE_HASH |
	    XM_MODE_RX_USE_PERFECT | XM_MODE_RX_USE_STATION);
	ifp->if_flags &= ~IFF_ALLMULTI;

	/*
	 * Always accept frames destined to our station address.
	 */
	reg |= XM_MODE_RX_USE_STATION;

	/* don't use perfect filter. */
	for (i = 1; i < XM_RXFILT_MAX; i++)
		sk_setfilt(sc_if, (caddr_t)&dummy, i);

	if (ifp->if_flags & IFF_PROMISC || ac->ac_multirangecnt > 0) {
		ifp->if_flags |= IFF_ALLMULTI;
		if (ifp->if_flags & IFF_PROMISC)
			reg |= XM_MODE_RX_PROMISC;
		else
			reg |= XM_MODE_RX_USE_HASH;
		hashes[0] = hashes[1] = 0xFFFFFFFF;
	} else {
		reg |= XM_MODE_RX_USE_HASH;
		/* Program new filter. */
		bzero(hashes, sizeof(hashes));

		ETHER_FIRST_MULTI(step, ac, enm);
		while (enm != NULL) {
			h = ether_crc32_le(enm->enm_addrlo,
			    ETHER_ADDR_LEN) & ((1 << SK_HASH_BITS) - 1);

			if (h < 32)
				hashes[0] |= (1 << h);
			else
				hashes[1] |= (1 << (h - 32));

			ETHER_NEXT_MULTI(step, enm);
		}
	}

	SK_XM_WRITE_4(sc_if, XM_MAR0, hashes[0]);
	SK_XM_WRITE_4(sc_if, XM_MAR2, hashes[1]);
	SK_XM_WRITE_4(sc_if, XM_MODE, reg);
}

void
sk_iff_yukon(struct sk_if_softc *sc_if)
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
sk_init_rx_ring(struct sk_if_softc *sc_if)
{
	struct sk_chain_data	*cd = &sc_if->sk_cdata;
	struct sk_ring_data	*rd = sc_if->sk_rdata;
	int			i, nexti;

	bzero(rd->sk_rx_ring, sizeof(struct sk_rx_desc) * SK_RX_RING_CNT);

	for (i = 0; i < SK_RX_RING_CNT; i++) {
		cd->sk_rx_chain[i].sk_desc = &rd->sk_rx_ring[i];
		if (i == (SK_RX_RING_CNT - 1))
			nexti = 0;
		else
			nexti = i + 1;
		cd->sk_rx_chain[i].sk_next = &cd->sk_rx_chain[nexti];
		htolem32(&rd->sk_rx_ring[i].sk_next,
		    SK_RX_RING_ADDR(sc_if, nexti));
	}

	sc_if->sk_cdata.sk_rx_prod = 0;
	sc_if->sk_cdata.sk_rx_cons = 0;

	if_rxr_init(&sc_if->sk_cdata.sk_rx_ring, 2, SK_RX_RING_CNT);

	sk_fill_rx_ring(sc_if);

	return (0);
}

void
sk_fill_rx_ring(struct sk_if_softc *sc_if)
{
	struct if_rxring *rxr = &sc_if->sk_cdata.sk_rx_ring;
	u_int slots;

	for (slots = if_rxr_get(rxr, SK_RX_RING_CNT); slots > 0; slots--) {
		if (sk_newbuf(sc_if) == ENOBUFS)
			break;
	}
	if_rxr_put(rxr, slots);
}

int
sk_init_tx_ring(struct sk_if_softc *sc_if)
{
	struct sk_softc		*sc = sc_if->sk_softc;
	struct sk_chain_data	*cd = &sc_if->sk_cdata;
	struct sk_ring_data	*rd = sc_if->sk_rdata;
	bus_dmamap_t		dmamap;
	struct sk_txmap_entry	*entry;
	int			i, nexti;

	bzero(sc_if->sk_rdata->sk_tx_ring,
	    sizeof(struct sk_tx_desc) * SK_TX_RING_CNT);

	SIMPLEQ_INIT(&sc_if->sk_txmap_head);
	for (i = 0; i < SK_TX_RING_CNT; i++) {
		cd->sk_tx_chain[i].sk_desc = &rd->sk_tx_ring[i];
		if (i == (SK_TX_RING_CNT - 1))
			nexti = 0;
		else
			nexti = i + 1;
		cd->sk_tx_chain[i].sk_next = &cd->sk_tx_chain[nexti];
		htolem32(&rd->sk_tx_ring[i].sk_next,
		    SK_TX_RING_ADDR(sc_if, nexti));

		if (bus_dmamap_create(sc->sc_dmatag, SK_JLEN, SK_NTXSEG,
		   SK_JLEN, 0, BUS_DMA_NOWAIT, &dmamap))
			return (ENOBUFS);

		entry = malloc(sizeof(*entry), M_DEVBUF, M_NOWAIT);
		if (!entry) {
			bus_dmamap_destroy(sc->sc_dmatag, dmamap);
			return (ENOBUFS);
		}
		entry->dmamap = dmamap;
		SIMPLEQ_INSERT_HEAD(&sc_if->sk_txmap_head, entry, link);
	}

	sc_if->sk_cdata.sk_tx_prod = 0;
	sc_if->sk_cdata.sk_tx_cons = 0;
	sc_if->sk_cdata.sk_tx_cnt = 0;

	SK_CDTXSYNC(sc_if, 0, SK_TX_RING_CNT,
	    BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);

	return (0);
}

int
sk_newbuf(struct sk_if_softc *sc_if)
{
	struct mbuf		*m;
	struct sk_chain		*c;
	struct sk_rx_desc	*r;
	bus_dmamap_t		dmamap;
	u_int			prod;
	int			error;
	uint64_t		dva;

	m = MCLGETL(NULL, M_DONTWAIT, SK_JLEN);
	if (m == NULL)
		return (ENOBUFS);

	m->m_len = m->m_pkthdr.len = SK_JLEN;
	m_adj(m, ETHER_ALIGN);

	prod = sc_if->sk_cdata.sk_rx_prod;
	dmamap = sc_if->sk_cdata.sk_rx_map[prod];

	error = bus_dmamap_load_mbuf(sc_if->sk_softc->sc_dmatag, dmamap, m,
	    BUS_DMA_READ|BUS_DMA_NOWAIT);
	if (error) {
		m_freem(m);
		return (ENOBUFS);
	}

	bus_dmamap_sync(sc_if->sk_softc->sc_dmatag, dmamap, 0,
	    dmamap->dm_mapsize, BUS_DMASYNC_PREREAD);

	c = &sc_if->sk_cdata.sk_rx_chain[prod];
	c->sk_mbuf = m;

	r = c->sk_desc;
	dva = dmamap->dm_segs[0].ds_addr;
	htolem32(&r->sk_data_lo, dva);
	htolem32(&r->sk_data_hi, dva >> 32);
	htolem32(&r->sk_ctl, dmamap->dm_segs[0].ds_len | SK_RXSTAT);

	SK_CDRXSYNC(sc_if, prod, BUS_DMASYNC_PREWRITE|BUS_DMASYNC_PREREAD);

	SK_INC(prod, SK_RX_RING_CNT);
	sc_if->sk_cdata.sk_rx_prod = prod;

	return (0);
}

/*
 * Set media options.
 */
int
sk_ifmedia_upd(struct ifnet *ifp)
{
	struct sk_if_softc *sc_if = ifp->if_softc;

	mii_mediachg(&sc_if->sk_mii);
	return (0);
}

/*
 * Report current media status.
 */
void
sk_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct sk_if_softc *sc_if = ifp->if_softc;

	mii_pollstat(&sc_if->sk_mii);
	ifmr->ifm_active = sc_if->sk_mii.mii_media_active;
	ifmr->ifm_status = sc_if->sk_mii.mii_media_status;
}

int
sk_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
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
			sk_init(sc_if);
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				error = ENETRESET;
			else
				sk_init(sc_if);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				sk_stop(sc_if, 0);
		}
		break;

	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		mii = &sc_if->sk_mii;
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, command);
		break;

	case SIOCGIFRXR:
		error = if_rxr_ioctl((struct if_rxrinfo *)ifr->ifr_data,
		    NULL, SK_JLEN, &sc_if->sk_cdata.sk_rx_ring);

		break;

	default:
		error = ether_ioctl(ifp, &sc_if->arpcom, command, data);
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			sk_iff(sc_if);
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
skc_probe(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = aux;
	pci_chipset_tag_t pc = pa->pa_pc;
	pcireg_t subid;

	subid = pci_conf_read(pc, pa->pa_tag, PCI_SUBSYS_ID_REG);

	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_LINKSYS &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_LINKSYS_EG1032 &&
	    subid == SK_LINKSYS_EG1032_SUBID)
		return (1);

	return (pci_matchbyid((struct pci_attach_args *)aux, skc_devices,
	    nitems(skc_devices)));
}

/*
 * Force the GEnesis into reset, then bring it out of reset.
 */
void
skc_reset(struct sk_softc *sc)
{
	u_int32_t imtimer_ticks;

	DPRINTFN(2, ("skc_reset\n"));

	CSR_WRITE_2(sc, SK_CSR, SK_CSR_SW_RESET);
	CSR_WRITE_2(sc, SK_CSR, SK_CSR_MASTER_RESET);
	if (SK_IS_YUKON(sc))
		CSR_WRITE_2(sc, SK_LINK_CTRL, SK_LINK_RESET_SET);

	DELAY(1000);
	CSR_WRITE_2(sc, SK_CSR, SK_CSR_SW_UNRESET);
	DELAY(2);
	CSR_WRITE_2(sc, SK_CSR, SK_CSR_MASTER_UNRESET);
	if (SK_IS_YUKON(sc))
		CSR_WRITE_2(sc, SK_LINK_CTRL, SK_LINK_RESET_CLEAR);

	DPRINTFN(2, ("sk_reset: sk_csr=%x\n", CSR_READ_2(sc, SK_CSR)));
	DPRINTFN(2, ("sk_reset: sk_link_ctrl=%x\n",
	    CSR_READ_2(sc, SK_LINK_CTRL)));

	if (SK_IS_GENESIS(sc)) {
		/* Configure packet arbiter */
		sk_win_write_2(sc, SK_PKTARB_CTL, SK_PKTARBCTL_UNRESET);
		sk_win_write_2(sc, SK_RXPA1_TINIT, SK_PKTARB_TIMEOUT);
		sk_win_write_2(sc, SK_TXPA1_TINIT, SK_PKTARB_TIMEOUT);
		sk_win_write_2(sc, SK_RXPA2_TINIT, SK_PKTARB_TIMEOUT);
		sk_win_write_2(sc, SK_TXPA2_TINIT, SK_PKTARB_TIMEOUT);
	}

	/* Enable RAM interface */
	sk_win_write_4(sc, SK_RAMCTL, SK_RAMCTL_UNRESET);

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
	case SK_GENESIS:
		imtimer_ticks = SK_IMTIMER_TICKS_GENESIS;
		break;
	default:
		imtimer_ticks = SK_IMTIMER_TICKS_YUKON;
		break;
	}
	sk_win_write_4(sc, SK_IMTIMERINIT, SK_IM_USECS(100));
	sk_win_write_4(sc, SK_IMMR, SK_ISR_TX1_S_EOF|SK_ISR_TX2_S_EOF|
	    SK_ISR_RX1_EOF|SK_ISR_RX2_EOF);
	sk_win_write_1(sc, SK_IMTIMERCTL, SK_IMCTL_START);
}

int
sk_probe(struct device *parent, void *match, void *aux)
{
	struct skc_attach_args *sa = aux;

	if (sa->skc_port != SK_PORT_A && sa->skc_port != SK_PORT_B)
		return (0);

	switch (sa->skc_type) {
	case SK_GENESIS:
	case SK_YUKON:
	case SK_YUKON_LITE:
	case SK_YUKON_LP:
		return (1);
	}

	return (0);
}

/*
 * Each XMAC chip is attached as a separate logical IP interface.
 * Single port cards will have only one logical interface of course.
 */
void
sk_attach(struct device *parent, struct device *self, void *aux)
{
	struct sk_if_softc *sc_if = (struct sk_if_softc *) self;
	struct sk_softc *sc = (struct sk_softc *)parent;
	struct skc_attach_args *sa = aux;
	struct ifnet *ifp;
	caddr_t kva;
	int i, error;

	sc_if->sk_port = sa->skc_port;
	sc_if->sk_softc = sc;
	sc->sk_if[sa->skc_port] = sc_if;

	if (sa->skc_port == SK_PORT_A)
		sc_if->sk_tx_bmu = SK_BMU_TXS_CSR0;
	if (sa->skc_port == SK_PORT_B)
		sc_if->sk_tx_bmu = SK_BMU_TXS_CSR1;

	DPRINTFN(2, ("begin sk_attach: port=%d\n", sc_if->sk_port));

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
	 * Set up RAM buffer addresses. The NIC will have a certain
	 * amount of SRAM on it, somewhere between 512K and 2MB. We
	 * need to divide this up a) between the transmitter and
 	 * receiver and b) between the two XMACs, if this is a
	 * dual port NIC. Our algorithm is to divide up the memory
	 * evenly so that everyone gets a fair share.
	 */
	if (sk_win_read_1(sc, SK_CONFIG) & SK_CONFIG_SINGLEMAC) {
		u_int32_t		chunk, val;

		chunk = sc->sk_ramsize / 2;
		val = sc->sk_rboff / sizeof(u_int64_t);
		sc_if->sk_rx_ramstart = val;
		val += (chunk / sizeof(u_int64_t));
		sc_if->sk_rx_ramend = val - 1;
		sc_if->sk_tx_ramstart = val;
		val += (chunk / sizeof(u_int64_t));
		sc_if->sk_tx_ramend = val - 1;
	} else {
		u_int32_t		chunk, val;

		chunk = sc->sk_ramsize / 4;
		val = (sc->sk_rboff + (chunk * 2 * sc_if->sk_port)) /
		    sizeof(u_int64_t);
		sc_if->sk_rx_ramstart = val;
		val += (chunk / sizeof(u_int64_t));
		sc_if->sk_rx_ramend = val - 1;
		sc_if->sk_tx_ramstart = val;
		val += (chunk / sizeof(u_int64_t));
		sc_if->sk_tx_ramend = val - 1;
	}

	DPRINTFN(2, ("sk_attach: rx_ramstart=%#x rx_ramend=%#x\n"
	    "           tx_ramstart=%#x tx_ramend=%#x\n",
	    sc_if->sk_rx_ramstart, sc_if->sk_rx_ramend,
	    sc_if->sk_tx_ramstart, sc_if->sk_tx_ramend));

	/* Read and save PHY type */
	sc_if->sk_phytype = sk_win_read_1(sc, SK_EPROM1) & 0xF;

	/* Set PHY address */
	if (SK_IS_GENESIS(sc)) {
		switch (sc_if->sk_phytype) {
		case SK_PHYTYPE_XMAC:
			sc_if->sk_phyaddr = SK_PHYADDR_XMAC;
			break;
		case SK_PHYTYPE_BCOM:
			sc_if->sk_phyaddr = SK_PHYADDR_BCOM;
			break;
		default:
			printf("%s: unsupported PHY type: %d\n",
			    sc->sk_dev.dv_xname, sc_if->sk_phytype);
			return;
		}
	}

	if (SK_IS_YUKON(sc)) {
		if ((sc_if->sk_phytype < SK_PHYTYPE_MARV_COPPER &&
		    sc->sk_pmd != 'L' && sc->sk_pmd != 'S')) {
			/* not initialized, punt */
			sc_if->sk_phytype = SK_PHYTYPE_MARV_COPPER;

			sc->sk_coppertype = 1;
		}

		sc_if->sk_phyaddr = SK_PHYADDR_MARV;

		if (!(sc->sk_coppertype))
			sc_if->sk_phytype = SK_PHYTYPE_MARV_FIBER;
	}

	/* Allocate the descriptor queues. */
	if (bus_dmamem_alloc(sc->sc_dmatag, sizeof(struct sk_ring_data),
	    PAGE_SIZE, 0, &sc_if->sk_ring_seg, 1, &sc_if->sk_ring_nseg,
	    BUS_DMA_NOWAIT | BUS_DMA_ZERO)) {
		printf(": can't alloc rx buffers\n");
		goto fail;
	}
	if (bus_dmamem_map(sc->sc_dmatag, &sc_if->sk_ring_seg,
	    sc_if->sk_ring_nseg, sizeof(struct sk_ring_data),
	    &kva, BUS_DMA_NOWAIT)) {
		printf(": can't map dma buffers (%lu bytes)\n",
		    (ulong)sizeof(struct sk_ring_data));
		goto fail_1;
	}
	if (bus_dmamap_create(sc->sc_dmatag, sizeof(struct sk_ring_data), 1,
	    sizeof(struct sk_ring_data), 0, BUS_DMA_NOWAIT,
	    &sc_if->sk_ring_map)) {
		printf(": can't create dma map\n");
		goto fail_2;
	}
	if (bus_dmamap_load(sc->sc_dmatag, sc_if->sk_ring_map, kva,
	    sizeof(struct sk_ring_data), NULL, BUS_DMA_NOWAIT)) {
		printf(": can't load dma map\n");
		goto fail_3;
	}
	sc_if->sk_rdata = (struct sk_ring_data *)kva;

	for (i = 0; i < SK_RX_RING_CNT; i++) {
		error = bus_dmamap_create(sc->sc_dmatag, SK_JLEN, 1,
		    SK_JLEN, 0, 0, &sc_if->sk_cdata.sk_rx_map[i]);
		if (error != 0) {
			printf(": unable to create rx DMA map %d, "
			    "error = %d\n", i, error);
			goto fail_4;
		}
	}

	ifp = &sc_if->arpcom.ac_if;
	ifp->if_softc = sc_if;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = sk_ioctl;
	ifp->if_start = sk_start;
	ifp->if_watchdog = sk_watchdog;
	ifp->if_hardmtu = SK_JUMBO_MTU;
	ifq_init_maxlen(&ifp->if_snd, SK_TX_RING_CNT - 1);
	bcopy(sc_if->sk_dev.dv_xname, ifp->if_xname, IFNAMSIZ);

	ifp->if_capabilities = IFCAP_VLAN_MTU;

	if (sk_reset(sc_if) == -1) {
		printf(": unknown device type %d\n", sc_if->sk_softc->sk_type);
		/* dealloc jumbo on error */
		goto fail_3;
	}

 	DPRINTFN(2, ("sk_attach: 1\n"));

	sc_if->sk_mii.mii_ifp = ifp;
	if (SK_IS_GENESIS(sc)) {
		sc_if->sk_mii.mii_readreg = sk_xmac_miibus_readreg;
		sc_if->sk_mii.mii_writereg = sk_xmac_miibus_writereg;
		sc_if->sk_mii.mii_statchg = sk_xmac_miibus_statchg;
	} else {
		sc_if->sk_mii.mii_readreg = sk_marv_miibus_readreg;
		sc_if->sk_mii.mii_writereg = sk_marv_miibus_writereg;
		sc_if->sk_mii.mii_statchg = sk_marv_miibus_statchg;
	}

	ifmedia_init(&sc_if->sk_mii.mii_media, 0,
	    sk_ifmedia_upd, sk_ifmedia_sts);
	if (SK_IS_GENESIS(sc)) {
		mii_attach(self, &sc_if->sk_mii, 0xffffffff, MII_PHY_ANY,
		    MII_OFFSET_ANY, 0);
	} else {
		mii_attach(self, &sc_if->sk_mii, 0xffffffff, MII_PHY_ANY,
		    MII_OFFSET_ANY, MIIF_DOPAUSE);
	}
	if (LIST_FIRST(&sc_if->sk_mii.mii_phys) == NULL) {
		printf("%s: no PHY found!\n", sc_if->sk_dev.dv_xname);
		ifmedia_add(&sc_if->sk_mii.mii_media, IFM_ETHER|IFM_MANUAL,
			    0, NULL);
		ifmedia_set(&sc_if->sk_mii.mii_media, IFM_ETHER|IFM_MANUAL);
	} else
		ifmedia_set(&sc_if->sk_mii.mii_media, IFM_ETHER|IFM_AUTO);

	if (SK_IS_GENESIS(sc)) {
		timeout_set(&sc_if->sk_tick_ch, sk_tick, sc_if);
		timeout_add_sec(&sc_if->sk_tick_ch, 1);
	} else
		timeout_set(&sc_if->sk_tick_ch, sk_yukon_tick, sc_if);

	/*
	 * Call MI attach routines.
	 */
	if_attach(ifp);
	ether_ifattach(ifp);

	DPRINTFN(2, ("sk_attach: end\n"));
	return;
fail_4:
	for (i = 0; i < SK_RX_RING_CNT; i++) {
		if (sc_if->sk_cdata.sk_rx_map[i] == NULL)
			continue;

		bus_dmamap_destroy(sc->sc_dmatag, sc_if->sk_cdata.sk_rx_map[i]);
	}
fail_3:
	bus_dmamem_unmap(sc->sc_dmatag, kva, sizeof(struct sk_ring_data));
fail_2:
	bus_dmamem_free(sc->sc_dmatag, &sc_if->sk_ring_seg, sc_if->sk_ring_nseg);
fail_1:
	bus_dmamap_destroy(sc->sc_dmatag, sc_if->sk_ring_map);
fail:
	sc->sk_if[sa->skc_port] = NULL;
}

int
sk_reset(struct sk_if_softc *sc_if)
{
	/*
	 * Do miibus setup.
	 */
	switch (sc_if->sk_softc->sk_type) {
	case SK_GENESIS:
		sk_init_xmac(sc_if);
		break;
	case SK_YUKON:
	case SK_YUKON_LITE:
	case SK_YUKON_LP:
		sk_init_yukon(sc_if);
		break;
	default:
		return (-1);
	}
	return (0);
}

int
sk_detach(struct device *self, int flags)
{
	struct sk_if_softc *sc_if = (struct sk_if_softc *)self;
	struct sk_softc *sc = sc_if->sk_softc;
	struct ifnet *ifp= &sc_if->arpcom.ac_if;

	if (sc->sk_if[sc_if->sk_port] == NULL)
		return (0);

	sk_stop(sc_if, 1);

	/* Detach any PHYs we might have. */
	if (LIST_FIRST(&sc_if->sk_mii.mii_phys) != NULL)
		mii_detach(&sc_if->sk_mii, MII_PHY_ANY, MII_OFFSET_ANY);

	/* Delete any remaining media. */
	ifmedia_delete_instance(&sc_if->sk_mii.mii_media, IFM_INST_ANY);

	ether_ifdetach(ifp);
	if_detach(ifp);

	bus_dmamem_unmap(sc->sc_dmatag, (caddr_t)sc_if->sk_rdata,
	    sizeof(struct sk_ring_data));
	bus_dmamem_free(sc->sc_dmatag,
	    &sc_if->sk_ring_seg, sc_if->sk_ring_nseg);
	bus_dmamap_destroy(sc->sc_dmatag, sc_if->sk_ring_map);
	sc->sk_if[sc_if->sk_port] = NULL;

	return (0);
}

int
sk_activate(struct device *self, int act)
{
	struct sk_if_softc *sc_if = (void *)self;
	struct ifnet *ifp = &sc_if->arpcom.ac_if;

	switch (act) {
	case DVACT_RESUME:
		sk_reset(sc_if);
		if (ifp->if_flags & IFF_RUNNING)
			sk_init(sc_if);
		break;
	}
	return (0);
}

int
skcprint(void *aux, const char *pnp)
{
	struct skc_attach_args *sa = aux;

	if (pnp)
		printf("sk port %c at %s",
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
skc_attach(struct device *parent, struct device *self, void *aux)
{
	struct sk_softc *sc = (struct sk_softc *)self;
	struct pci_attach_args *pa = aux;
	struct skc_attach_args skca;
	pci_chipset_tag_t pc = pa->pa_pc;
	pcireg_t memtype;
	pci_intr_handle_t ih;
	const char *intrstr = NULL;
	u_int8_t skrs;
	char *revstr = NULL;

	DPRINTFN(2, ("begin skc_attach\n"));

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
	sc->sk_pc = pc;

	/* bail out here if chip is not recognized */
	if (! SK_IS_GENESIS(sc) && ! SK_IS_YUKON(sc)) {
		printf(": unknown chip type: %d\n", sc->sk_type);
		goto fail_1;
	}
	DPRINTFN(2, ("skc_attach: allocate interrupt\n"));

	/* Allocate interrupt */
	if (pci_intr_map(pa, &ih)) {
		printf(": couldn't map interrupt\n");
		goto fail_1;
	}

	intrstr = pci_intr_string(pc, ih);
	sc->sk_intrhand = pci_intr_establish(pc, ih, IPL_NET, sk_intr, sc,
	    self->dv_xname);
	if (sc->sk_intrhand == NULL) {
		printf(": couldn't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		goto fail_1;
	}

	/* Reset the adapter. */
	skc_reset(sc);

	skrs = sk_win_read_1(sc, SK_EPROM0);
	if (SK_IS_GENESIS(sc)) {
		/* Read and save RAM size and RAMbuffer offset */
		switch(skrs) {
		case SK_RAMSIZE_512K_64:
			sc->sk_ramsize = 0x80000;
			sc->sk_rboff = SK_RBOFF_0;
			break;
		case SK_RAMSIZE_1024K_64:
			sc->sk_ramsize = 0x100000;
			sc->sk_rboff = SK_RBOFF_80000;
			break;
		case SK_RAMSIZE_1024K_128:
			sc->sk_ramsize = 0x100000;
			sc->sk_rboff = SK_RBOFF_0;
			break;
		case SK_RAMSIZE_2048K_128:
			sc->sk_ramsize = 0x200000;
			sc->sk_rboff = SK_RBOFF_0;
			break;
		default:
			printf(": unknown ram size: %d\n", skrs);
			goto fail_2;
			break;
		}
	} else {
		if (skrs == 0x00)
			sc->sk_ramsize = 0x20000;
		else
			sc->sk_ramsize = skrs * (1<<12);
		sc->sk_rboff = SK_RBOFF_0;
	}

	DPRINTFN(2, ("skc_attach: ramsize=%d (%dk), rboff=%d\n",
	    sc->sk_ramsize, sc->sk_ramsize / 1024, sc->sk_rboff));

	/* Read and save physical media type */
	sc->sk_pmd = sk_win_read_1(sc, SK_PMDTYPE);

	if (sc->sk_pmd == 'T' || sc->sk_pmd == '1')
		sc->sk_coppertype = 1;
	else
		sc->sk_coppertype = 0;

	switch (sc->sk_type) {
	case SK_GENESIS:
		sc->sk_name = "GEnesis";
		break;
	case SK_YUKON:
		sc->sk_name = "Yukon";
		break;
	case SK_YUKON_LITE:
		sc->sk_name = "Yukon Lite";
		break;
	case SK_YUKON_LP:
		sc->sk_name = "Yukon LP";
		break;
	default:
		sc->sk_name = "Yukon (Unknown)";
	}

	/* Yukon Lite Rev A0 needs special test, from sk98lin driver */
	if (sc->sk_type == SK_YUKON || sc->sk_type == SK_YUKON_LP) {
		u_int32_t flashaddr;
		u_int8_t testbyte;

		flashaddr = sk_win_read_4(sc, SK_EP_ADDR);

		/* test Flash-Address Register */
		sk_win_write_1(sc, SK_EP_ADDR+3, 0xff);
		testbyte = sk_win_read_1(sc, SK_EP_ADDR+3);

		if (testbyte != 0) {
			/* This is a Yukon Lite Rev A0 */
			sc->sk_type = SK_YUKON_LITE;
			sc->sk_rev = SK_YUKON_LITE_REV_A0;
			/* restore Flash-Address Register */
			sk_win_write_4(sc, SK_EP_ADDR, flashaddr);
		}
	}

	if (sc->sk_type == SK_YUKON_LITE) {
		switch (sc->sk_rev) {
		case SK_YUKON_LITE_REV_A0:
			revstr = "A0";
			break;
		case SK_YUKON_LITE_REV_A1:
			revstr = "A1";
			break;
		case SK_YUKON_LITE_REV_A3:
			revstr = "A3";
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

	if (!(sk_win_read_1(sc, SK_CONFIG) & SK_CONFIG_SINGLEMAC))
		sc->sk_macs++;

	skca.skc_port = SK_PORT_A;
	skca.skc_type = sc->sk_type;
	skca.skc_rev = sc->sk_rev;
	(void)config_found(&sc->sk_dev, &skca, skcprint);

	if (sc->sk_macs > 1) {
		skca.skc_port = SK_PORT_B;
		skca.skc_type = sc->sk_type;
		skca.skc_rev = sc->sk_rev;
		(void)config_found(&sc->sk_dev, &skca, skcprint);
	}

	/* Turn on the 'driver is loaded' LED. */
	CSR_WRITE_2(sc, SK_LED, SK_LED_GREEN_ON);

	return;

fail_2:
	pci_intr_disestablish(pc, sc->sk_intrhand);
fail_1:
	bus_space_unmap(sc->sk_btag, sc->sk_bhandle, sc->sk_bsize);
}

int
skc_detach(struct device *self, int flags)
{
	struct sk_softc *sc = (struct sk_softc *)self;
	int rv;

	if (sc->sk_intrhand)
		pci_intr_disestablish(sc->sk_pc, sc->sk_intrhand);

	rv = config_detach_children(self, flags);
	if (rv != 0)
		return (rv);

	if (sc->sk_bsize > 0)
		bus_space_unmap(sc->sk_btag, sc->sk_bhandle, sc->sk_bsize);

	return(0);
}

int
skc_activate(struct device *self, int act)
{
	struct sk_softc *sc = (void *)self;
	int rv = 0;

	switch (act) {
	case DVACT_RESUME:
		skc_reset(sc);
		rv = config_activate_children(self, act);
		break;
	default:
		rv = config_activate_children(self, act);
		break;
	}
	return (rv);
}

int
sk_encap(struct sk_if_softc *sc_if, struct mbuf *m_head, u_int32_t *txidx)
{
	struct sk_softc		*sc = sc_if->sk_softc;
	struct sk_tx_desc	*f = NULL;
	u_int32_t		frag, cur, sk_ctl;
	int			i;
	struct sk_txmap_entry	*entry;
	bus_dmamap_t		txmap;
	uint64_t		dva;

	DPRINTFN(2, ("sk_encap\n"));

	entry = SIMPLEQ_FIRST(&sc_if->sk_txmap_head);
	if (entry == NULL) {
		DPRINTFN(2, ("sk_encap: no txmap available\n"));
		return (ENOBUFS);
	}
	txmap = entry->dmamap;

	cur = frag = *txidx;

	switch (bus_dmamap_load_mbuf(sc->sc_dmatag, txmap, m_head,
	    BUS_DMA_STREAMING | BUS_DMA_NOWAIT)) {
	case 0:
		break;

	case EFBIG: /* mbuf chain is too fragmented */
		if (m_defrag(m_head, M_DONTWAIT) == 0 &&
		    bus_dmamap_load_mbuf(sc->sc_dmatag, txmap, m_head,
		    BUS_DMA_STREAMING | BUS_DMA_NOWAIT) == 0)
			break;
	default:
		return (1);
	}

	/* Sync the DMA map. */
	bus_dmamap_sync(sc->sc_dmatag, txmap, 0, txmap->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);

	for (i = 0; i < txmap->dm_nsegs; i++) {
		f = &sc_if->sk_rdata->sk_tx_ring[frag];
		dva = txmap->dm_segs[i].ds_addr;
		htolem32(&f->sk_data_lo, dva);
		htolem32(&f->sk_data_hi, dva >> 32);
		sk_ctl = txmap->dm_segs[i].ds_len | SK_OPCODE_DEFAULT;
		if (i == 0)
			sk_ctl |= SK_TXCTL_FIRSTFRAG;
		else
			sk_ctl |= SK_TXCTL_OWN;
		htolem32(&f->sk_ctl, sk_ctl);
		cur = frag;
		SK_INC(frag, SK_TX_RING_CNT);
	}

	sc_if->sk_cdata.sk_tx_chain[cur].sk_mbuf = m_head;
	SIMPLEQ_REMOVE_HEAD(&sc_if->sk_txmap_head, link);

	sc_if->sk_cdata.sk_tx_map[cur] = entry;
	sc_if->sk_rdata->sk_tx_ring[cur].sk_ctl |=
		htole32(SK_TXCTL_LASTFRAG|SK_TXCTL_EOF_INTR);

	/* Sync descriptors before handing to chip */
	SK_CDTXSYNC(sc_if, *txidx, txmap->dm_nsegs,
	    BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);

	sc_if->sk_rdata->sk_tx_ring[*txidx].sk_ctl |=
		htole32(SK_TXCTL_OWN);

	/* Sync first descriptor to hand it off */
	SK_CDTXSYNC(sc_if, *txidx, 1, BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);

	sc_if->sk_cdata.sk_tx_cnt += txmap->dm_nsegs;

#ifdef SK_DEBUG
	if (skdebug >= 2) {
		struct sk_tx_desc *desc;
		u_int32_t idx;
		for (idx = *txidx; idx != frag; SK_INC(idx, SK_TX_RING_CNT)) {
			desc = &sc_if->sk_rdata->sk_tx_ring[idx];
			sk_dump_txdesc(desc, idx);
		}
	}
#endif

	*txidx = frag;

	DPRINTFN(2, ("sk_encap: completed successfully\n"));

	return (0);
}

void
sk_start(struct ifnet *ifp)
{
	struct sk_if_softc	*sc_if = ifp->if_softc;
	struct sk_softc		*sc = sc_if->sk_softc;
	struct mbuf		*m_head = NULL;
	u_int32_t		idx = sc_if->sk_cdata.sk_tx_prod;
	int			post = 0;

	DPRINTFN(2, ("sk_start\n"));

	for (;;) {
		if (sc_if->sk_cdata.sk_tx_cnt + SK_NTXSEG + 1 >
		    SK_TX_RING_CNT) {
			ifq_set_oactive(&ifp->if_snd);
			break;
		}
		
		m_head = ifq_dequeue(&ifp->if_snd);
		if (m_head == NULL)
			break;

		/*
		 * Pack the data into the transmit ring. If we
		 * don't have room, set the OACTIVE flag and wait
		 * for the NIC to drain the ring.
		 */
		if (sk_encap(sc_if, m_head, &idx)) {
			m_freem(m_head);
			continue;
		}

		/* now we are committed to transmit the packet */

		/*
		 * If there's a BPF listener, bounce a copy of this frame
		 * to him.
		 */
#if NBPFILTER > 0
		if (ifp->if_bpf)
			bpf_mtap(ifp->if_bpf, m_head, BPF_DIRECTION_OUT);
#endif

		post = 1;
	}
	if (post == 0)
		return;

	/* Transmit */
	sc_if->sk_cdata.sk_tx_prod = idx;
	CSR_WRITE_4(sc, sc_if->sk_tx_bmu, SK_TXBMU_TX_START);

	/* Set a timeout in case the chip goes out to lunch. */
	ifp->if_timer = SK_TX_TIMEOUT;
}


void
sk_watchdog(struct ifnet *ifp)
{
	struct sk_if_softc *sc_if = ifp->if_softc;

	/*
	 * Reclaim first as there is a possibility of losing Tx completion
	 * interrupts.
	 */
	sk_txeof(sc_if);
	if (sc_if->sk_cdata.sk_tx_cnt != 0) {
		printf("%s: watchdog timeout\n", sc_if->sk_dev.dv_xname);

		ifp->if_oerrors++;

		sk_init(sc_if);
	}
}

static __inline int
sk_rxvalid(struct sk_softc *sc, u_int32_t stat, u_int32_t len)
{
	if (sc->sk_type == SK_GENESIS) {
		if ((stat & XM_RXSTAT_ERRFRAME) == XM_RXSTAT_ERRFRAME ||
		    XM_RXSTAT_BYTES(stat) != len)
			return (0);
	} else {
		if ((stat & (YU_RXSTAT_CRCERR | YU_RXSTAT_LONGERR |
		    YU_RXSTAT_MIIERR | YU_RXSTAT_BADFC | YU_RXSTAT_GOODFC |
		    YU_RXSTAT_JABBER)) != 0 ||
		    (stat & YU_RXSTAT_RXOK) != YU_RXSTAT_RXOK ||
		    YU_RXSTAT_BYTES(stat) != len)
			return (0);
	}

	return (1);
}

void
sk_rxeof(struct sk_if_softc *sc_if)
{
	struct sk_softc		*sc = sc_if->sk_softc;
	struct ifnet		*ifp = &sc_if->arpcom.ac_if;
	struct if_rxring	*rxr = &sc_if->sk_cdata.sk_rx_ring;
	struct mbuf		*m;
	struct mbuf_list	ml = MBUF_LIST_INITIALIZER();
	struct sk_chain		*cur_rx;
	struct sk_rx_desc	*cur_desc;
	int			cur, total_len = 0;
	u_int32_t		rxstat, sk_ctl;
	bus_dmamap_t		dmamap;

	DPRINTFN(2, ("sk_rxeof\n"));

	cur = sc_if->sk_cdata.sk_rx_cons;
	while (if_rxr_inuse(rxr) > 0) {
		/* Sync the descriptor */
		SK_CDRXSYNC(sc_if, cur,
		    BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE);

		cur_rx = &sc_if->sk_cdata.sk_rx_chain[cur];
		if (cur_rx->sk_mbuf == NULL)
			break;

		cur_desc = &sc_if->sk_rdata->sk_rx_ring[cur];
		sk_ctl = lemtoh32(&cur_desc->sk_ctl);
		if ((sk_ctl & SK_RXCTL_OWN) != 0)
			break;

		dmamap = sc_if->sk_cdata.sk_rx_map[cur];

		bus_dmamap_sync(sc_if->sk_softc->sc_dmatag, dmamap, 0,
		    dmamap->dm_mapsize, BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc_if->sk_softc->sc_dmatag, dmamap);

		m = cur_rx->sk_mbuf;
		cur_rx->sk_mbuf = NULL;
		if_rxr_put(rxr, 1);
		SK_INC(cur, SK_RX_RING_CNT);

		total_len = SK_RXBYTES(sk_ctl);
		rxstat = lemtoh32(&cur_desc->sk_xmac_rxstat);

		if ((sk_ctl & (SK_RXCTL_STATUS_VALID | SK_RXCTL_FIRSTFRAG |
		    SK_RXCTL_LASTFRAG)) != (SK_RXCTL_STATUS_VALID |
		    SK_RXCTL_FIRSTFRAG | SK_RXCTL_LASTFRAG) ||
		    total_len < SK_MIN_FRAMELEN ||
		    total_len > SK_JUMBO_FRAMELEN ||
		    sk_rxvalid(sc, rxstat, total_len) == 0) {
			ifp->if_ierrors++;
			m_freem(m);
			continue;
		}

		m->m_pkthdr.len = m->m_len = total_len;

		ml_enqueue(&ml, m);
	}
	sc_if->sk_cdata.sk_rx_cons = cur;

	if (ifiq_input(&ifp->if_rcv, &ml))
		if_rxr_livelocked(rxr);

	sk_fill_rx_ring(sc_if);

}

void
sk_txeof(struct sk_if_softc *sc_if)
{
	struct sk_softc		*sc = sc_if->sk_softc;
	struct sk_tx_desc	*cur_tx;
	struct ifnet		*ifp = &sc_if->arpcom.ac_if;
	u_int32_t		idx, sk_ctl;
	struct sk_txmap_entry	*entry;

	DPRINTFN(2, ("sk_txeof\n"));

	/*
	 * Go through our tx ring and free mbufs for those
	 * frames that have been sent.
	 */
	idx = sc_if->sk_cdata.sk_tx_cons;
	while (idx != sc_if->sk_cdata.sk_tx_prod) {
		SK_CDTXSYNC(sc_if, idx, 1,
		    BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE);

		cur_tx = &sc_if->sk_rdata->sk_tx_ring[idx];
		sk_ctl = lemtoh32(&cur_tx->sk_ctl);
#ifdef SK_DEBUG
		if (skdebug >= 2)
			sk_dump_txdesc(cur_tx, idx);
#endif
		if (sk_ctl & SK_TXCTL_OWN) {
			SK_CDTXSYNC(sc_if, idx, 1, BUS_DMASYNC_PREREAD);
			break;
		}
		if (sc_if->sk_cdata.sk_tx_chain[idx].sk_mbuf != NULL) {
			entry = sc_if->sk_cdata.sk_tx_map[idx];

			m_freem(sc_if->sk_cdata.sk_tx_chain[idx].sk_mbuf);
			sc_if->sk_cdata.sk_tx_chain[idx].sk_mbuf = NULL;

			bus_dmamap_sync(sc->sc_dmatag, entry->dmamap, 0,
			    entry->dmamap->dm_mapsize, BUS_DMASYNC_POSTWRITE);

			bus_dmamap_unload(sc->sc_dmatag, entry->dmamap);
			SIMPLEQ_INSERT_TAIL(&sc_if->sk_txmap_head, entry,
					  link);
			sc_if->sk_cdata.sk_tx_map[idx] = NULL;
		}
		sc_if->sk_cdata.sk_tx_cnt--;
		SK_INC(idx, SK_TX_RING_CNT);
	}
	ifp->if_timer = sc_if->sk_cdata.sk_tx_cnt > 0 ? SK_TX_TIMEOUT : 0;

	if (sc_if->sk_cdata.sk_tx_cnt < SK_TX_RING_CNT - 2)
		ifq_clr_oactive(&ifp->if_snd);

	sc_if->sk_cdata.sk_tx_cons = idx;
}

void
sk_tick(void *xsc_if)
{
	struct sk_if_softc *sc_if = xsc_if;
	struct mii_data *mii = &sc_if->sk_mii;
	struct ifnet *ifp = &sc_if->arpcom.ac_if;
	int i;

	DPRINTFN(2, ("sk_tick\n"));

	if (!(ifp->if_flags & IFF_UP))
		return;

	if (sc_if->sk_phytype == SK_PHYTYPE_BCOM) {
		sk_intr_bcom(sc_if);
		return;
	}

	/*
	 * According to SysKonnect, the correct way to verify that
	 * the link has come back up is to poll bit 0 of the GPIO
	 * register three times. This pin has the signal from the
	 * link sync pin connected to it; if we read the same link
	 * state 3 times in a row, we know the link is up.
	 */
	for (i = 0; i < 3; i++) {
		if (SK_XM_READ_2(sc_if, XM_GPIO) & XM_GPIO_GP0_SET)
			break;
	}

	if (i != 3) {
		timeout_add_sec(&sc_if->sk_tick_ch, 1);
		return;
	}

	/* Turn the GP0 interrupt back on. */
	SK_XM_CLRBIT_2(sc_if, XM_IMR, XM_IMR_GP0_SET);
	SK_XM_READ_2(sc_if, XM_ISR);
	mii_tick(mii);
	timeout_del(&sc_if->sk_tick_ch);
}

void
sk_yukon_tick(void *xsc_if)
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
sk_intr_bcom(struct sk_if_softc *sc_if)
{
	struct mii_data *mii = &sc_if->sk_mii;
	struct ifnet *ifp = &sc_if->arpcom.ac_if;
	int status;

	DPRINTFN(2, ("sk_intr_bcom\n"));

	SK_XM_CLRBIT_2(sc_if, XM_MMUCMD, XM_MMUCMD_TX_ENB|XM_MMUCMD_RX_ENB);

	/*
	 * Read the PHY interrupt register to make sure
	 * we clear any pending interrupts.
	 */
	status = sk_xmac_miibus_readreg((struct device *)sc_if,
	    SK_PHYADDR_BCOM, BRGPHY_MII_ISR);

	if (!(ifp->if_flags & IFF_RUNNING)) {
		sk_init_xmac(sc_if);
		return;
	}

	if (status & (BRGPHY_ISR_LNK_CHG|BRGPHY_ISR_AN_PR)) {
		int lstat;
		lstat = sk_xmac_miibus_readreg((struct device *)sc_if,
		    SK_PHYADDR_BCOM, BRGPHY_MII_AUXSTS);

		if (!(lstat & BRGPHY_AUXSTS_LINK) && sc_if->sk_link) {
			mii_mediachg(mii);
			/* Turn off the link LED. */
			SK_IF_WRITE_1(sc_if, 0,
			    SK_LINKLED1_CTL, SK_LINKLED_OFF);
			sc_if->sk_link = 0;
		} else if (status & BRGPHY_ISR_LNK_CHG) {
			sk_xmac_miibus_writereg((struct device *)sc_if,
			    SK_PHYADDR_BCOM, BRGPHY_MII_IMR, 0xFF00);
			mii_tick(mii);
			sc_if->sk_link = 1;
			/* Turn on the link LED. */
			SK_IF_WRITE_1(sc_if, 0, SK_LINKLED1_CTL,
			    SK_LINKLED_ON|SK_LINKLED_LINKSYNC_OFF|
			    SK_LINKLED_BLINK_OFF);
		} else {
			mii_tick(mii);
			timeout_add_sec(&sc_if->sk_tick_ch, 1);
		}
	}

	SK_XM_SETBIT_2(sc_if, XM_MMUCMD, XM_MMUCMD_TX_ENB|XM_MMUCMD_RX_ENB);
}

void
sk_intr_xmac(struct sk_if_softc	*sc_if)
{
	u_int16_t status = SK_XM_READ_2(sc_if, XM_ISR);

	DPRINTFN(2, ("sk_intr_xmac\n"));

	if (sc_if->sk_phytype == SK_PHYTYPE_XMAC) {
		if (status & XM_ISR_GP0_SET) {
			SK_XM_SETBIT_2(sc_if, XM_IMR, XM_IMR_GP0_SET);
			timeout_add_sec(&sc_if->sk_tick_ch, 1);
		}

		if (status & XM_ISR_AUTONEG_DONE) {
			timeout_add_sec(&sc_if->sk_tick_ch, 1);
		}
	}

	if (status & XM_IMR_TX_UNDERRUN)
		SK_XM_SETBIT_4(sc_if, XM_MODE, XM_MODE_FLUSH_TXFIFO);

	if (status & XM_IMR_RX_OVERRUN)
		SK_XM_SETBIT_4(sc_if, XM_MODE, XM_MODE_FLUSH_RXFIFO);
}

void
sk_intr_yukon(struct sk_if_softc *sc_if)
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

	DPRINTFN(2, ("sk_intr_yukon status=%#x\n", status));
}

int
sk_intr(void *xsc)
{
	struct sk_softc		*sc = xsc;
	struct sk_if_softc	*sc_if0 = sc->sk_if[SK_PORT_A];
	struct sk_if_softc	*sc_if1 = sc->sk_if[SK_PORT_B];
	struct ifnet		*ifp0 = NULL, *ifp1 = NULL;
	u_int32_t		status;
	int			claimed = 0;

	status = CSR_READ_4(sc, SK_ISSR);
	if (status == 0 || status == 0xffffffff)
		return (0);

	if (sc_if0 != NULL)
		ifp0 = &sc_if0->arpcom.ac_if;
	if (sc_if1 != NULL)
		ifp1 = &sc_if1->arpcom.ac_if;

	for (; (status &= sc->sk_intrmask) != 0;) {
		claimed = 1;

		/* Handle receive interrupts first. */
		if (sc_if0 && (status & SK_ISR_RX1_EOF)) {
			sk_rxeof(sc_if0);
			CSR_WRITE_4(sc, SK_BMU_RX_CSR0,
			    SK_RXBMU_CLR_IRQ_EOF|SK_RXBMU_RX_START);
		}
		if (sc_if1 && (status & SK_ISR_RX2_EOF)) {
			sk_rxeof(sc_if1);
			CSR_WRITE_4(sc, SK_BMU_RX_CSR1,
			    SK_RXBMU_CLR_IRQ_EOF|SK_RXBMU_RX_START);
		}

		/* Then transmit interrupts. */
		if (sc_if0 && (status & SK_ISR_TX1_S_EOF)) {
			sk_txeof(sc_if0);
			CSR_WRITE_4(sc, SK_BMU_TXS_CSR0,
			    SK_TXBMU_CLR_IRQ_EOF);
		}
		if (sc_if1 && (status & SK_ISR_TX2_S_EOF)) {
			sk_txeof(sc_if1);
			CSR_WRITE_4(sc, SK_BMU_TXS_CSR1,
			    SK_TXBMU_CLR_IRQ_EOF);
		}

		/* Then MAC interrupts. */
		if (sc_if0 && (status & SK_ISR_MAC1) &&
		    (ifp0->if_flags & IFF_RUNNING)) {
			if (SK_IS_GENESIS(sc))
				sk_intr_xmac(sc_if0);
			else
				sk_intr_yukon(sc_if0);
		}

		if (sc_if1 && (status & SK_ISR_MAC2) &&
		    (ifp1->if_flags & IFF_RUNNING)) {
			if (SK_IS_GENESIS(sc))
				sk_intr_xmac(sc_if1);
			else
				sk_intr_yukon(sc_if1);

		}

		if (status & SK_ISR_EXTERNAL_REG) {
			if (sc_if0 != NULL &&
			    sc_if0->sk_phytype == SK_PHYTYPE_BCOM)
				sk_intr_bcom(sc_if0);

			if (sc_if1 != NULL &&
			    sc_if1->sk_phytype == SK_PHYTYPE_BCOM)
				sk_intr_bcom(sc_if1);
		}
		status = CSR_READ_4(sc, SK_ISSR);
	}

	CSR_WRITE_4(sc, SK_IMR, sc->sk_intrmask);

	if (ifp0 != NULL && !ifq_empty(&ifp0->if_snd))
		sk_start(ifp0);
	if (ifp1 != NULL && !ifq_empty(&ifp1->if_snd))
		sk_start(ifp1);

	return (claimed);
}

void
sk_init_xmac(struct sk_if_softc	*sc_if)
{
	struct sk_softc		*sc = sc_if->sk_softc;
	struct sk_bcom_hack	bhack[] = {
	{ 0x18, 0x0c20 }, { 0x17, 0x0012 }, { 0x15, 0x1104 }, { 0x17, 0x0013 },
	{ 0x15, 0x0404 }, { 0x17, 0x8006 }, { 0x15, 0x0132 }, { 0x17, 0x8006 },
	{ 0x15, 0x0232 }, { 0x17, 0x800D }, { 0x15, 0x000F }, { 0x18, 0x0420 },
	{ 0, 0 } };

	DPRINTFN(2, ("sk_init_xmac\n"));

	/* Unreset the XMAC. */
	SK_IF_WRITE_2(sc_if, 0, SK_TXF1_MACCTL, SK_TXMACCTL_XMAC_UNRESET);
	DELAY(1000);

	/* Reset the XMAC's internal state. */
	SK_XM_SETBIT_2(sc_if, XM_GPIO, XM_GPIO_RESETMAC);

	/* Save the XMAC II revision */
	sc_if->sk_xmac_rev = XM_XMAC_REV(SK_XM_READ_4(sc_if, XM_DEVID));

	/*
	 * Perform additional initialization for external PHYs,
	 * namely for the 1000baseTX cards that use the XMAC's
	 * GMII mode.
	 */
	if (sc_if->sk_phytype == SK_PHYTYPE_BCOM) {
		int			i = 0;
		u_int32_t		val;

		/* Take PHY out of reset. */
		val = sk_win_read_4(sc, SK_GPIO);
		if (sc_if->sk_port == SK_PORT_A)
			val |= SK_GPIO_DIR0|SK_GPIO_DAT0;
		else
			val |= SK_GPIO_DIR2|SK_GPIO_DAT2;
		sk_win_write_4(sc, SK_GPIO, val);

		/* Enable GMII mode on the XMAC. */
		SK_XM_SETBIT_2(sc_if, XM_HWCFG, XM_HWCFG_GMIIMODE);

		sk_xmac_miibus_writereg((struct device *)sc_if,
		    SK_PHYADDR_BCOM, MII_BMCR, BMCR_RESET);
		DELAY(10000);
		sk_xmac_miibus_writereg((struct device *)sc_if,
		    SK_PHYADDR_BCOM, BRGPHY_MII_IMR, 0xFFF0);

		/*
		 * Early versions of the BCM5400 apparently have
		 * a bug that requires them to have their reserved
		 * registers initialized to some magic values. I don't
		 * know what the numbers do, I'm just the messenger.
		 */
		if (sk_xmac_miibus_readreg((struct device *)sc_if,
		    SK_PHYADDR_BCOM, 0x03) == 0x6041) {
			while(bhack[i].reg) {
				sk_xmac_miibus_writereg((struct device *)sc_if,
				    SK_PHYADDR_BCOM, bhack[i].reg,
				    bhack[i].val);
				i++;
			}
		}
	}

	/* Set station address */
	SK_XM_WRITE_2(sc_if, XM_PAR0,
	    letoh16(*(u_int16_t *)(&sc_if->arpcom.ac_enaddr[0])));
	SK_XM_WRITE_2(sc_if, XM_PAR1,
	    letoh16(*(u_int16_t *)(&sc_if->arpcom.ac_enaddr[2])));
	SK_XM_WRITE_2(sc_if, XM_PAR2,
	    letoh16(*(u_int16_t *)(&sc_if->arpcom.ac_enaddr[4])));

	/* We don't need the FCS appended to the packet. */
	SK_XM_SETBIT_2(sc_if, XM_RXCMD, XM_RXCMD_STRIPFCS);

	/* We want short frames padded to 60 bytes. */
	SK_XM_SETBIT_2(sc_if, XM_TXCMD, XM_TXCMD_AUTOPAD);

	/*
	 * Enable the reception of all error frames. This is
	 * a necessary evil due to the design of the XMAC. The
	 * XMAC's receive FIFO is only 8K in size, however jumbo
	 * frames can be up to 9000 bytes in length. When bad
	 * frame filtering is enabled, the XMAC's RX FIFO operates
	 * in 'store and forward' mode. For this to work, the
	 * entire frame has to fit into the FIFO, but that means
	 * that jumbo frames larger than 8192 bytes will be
	 * truncated. Disabling all bad frame filtering causes
	 * the RX FIFO to operate in streaming mode, in which
	 * case the XMAC will start transferring frames out of the
	 * RX FIFO as soon as the FIFO threshold is reached.
	 */
	SK_XM_SETBIT_4(sc_if, XM_MODE, XM_MODE_RX_BADFRAMES|
	    XM_MODE_RX_GIANTS|XM_MODE_RX_RUNTS|XM_MODE_RX_CRCERRS|
	    XM_MODE_RX_INRANGELEN);

	SK_XM_SETBIT_2(sc_if, XM_RXCMD, XM_RXCMD_BIGPKTOK);

	/*
	 * Bump up the transmit threshold. This helps hold off transmit
	 * underruns when we're blasting traffic from both ports at once.
	 */
	SK_XM_WRITE_2(sc_if, XM_TX_REQTHRESH, SK_XM_TX_FIFOTHRESH);

	/* Program promiscuous mode and multicast filters. */
	sk_iff(sc_if);

	/* Clear and enable interrupts */
	SK_XM_READ_2(sc_if, XM_ISR);
	if (sc_if->sk_phytype == SK_PHYTYPE_XMAC)
		SK_XM_WRITE_2(sc_if, XM_IMR, XM_INTRS);
	else
		SK_XM_WRITE_2(sc_if, XM_IMR, 0xFFFF);

	/* Configure MAC arbiter */
	switch(sc_if->sk_xmac_rev) {
	case XM_XMAC_REV_B2:
		sk_win_write_1(sc, SK_RCINIT_RX1, SK_RCINIT_XMAC_B2);
		sk_win_write_1(sc, SK_RCINIT_TX1, SK_RCINIT_XMAC_B2);
		sk_win_write_1(sc, SK_RCINIT_RX2, SK_RCINIT_XMAC_B2);
		sk_win_write_1(sc, SK_RCINIT_TX2, SK_RCINIT_XMAC_B2);
		sk_win_write_1(sc, SK_MINIT_RX1, SK_MINIT_XMAC_B2);
		sk_win_write_1(sc, SK_MINIT_TX1, SK_MINIT_XMAC_B2);
		sk_win_write_1(sc, SK_MINIT_RX2, SK_MINIT_XMAC_B2);
		sk_win_write_1(sc, SK_MINIT_TX2, SK_MINIT_XMAC_B2);
		sk_win_write_1(sc, SK_RECOVERY_CTL, SK_RECOVERY_XMAC_B2);
		break;
	case XM_XMAC_REV_C1:
		sk_win_write_1(sc, SK_RCINIT_RX1, SK_RCINIT_XMAC_C1);
		sk_win_write_1(sc, SK_RCINIT_TX1, SK_RCINIT_XMAC_C1);
		sk_win_write_1(sc, SK_RCINIT_RX2, SK_RCINIT_XMAC_C1);
		sk_win_write_1(sc, SK_RCINIT_TX2, SK_RCINIT_XMAC_C1);
		sk_win_write_1(sc, SK_MINIT_RX1, SK_MINIT_XMAC_C1);
		sk_win_write_1(sc, SK_MINIT_TX1, SK_MINIT_XMAC_C1);
		sk_win_write_1(sc, SK_MINIT_RX2, SK_MINIT_XMAC_C1);
		sk_win_write_1(sc, SK_MINIT_TX2, SK_MINIT_XMAC_C1);
		sk_win_write_1(sc, SK_RECOVERY_CTL, SK_RECOVERY_XMAC_B2);
		break;
	default:
		break;
	}
	sk_win_write_2(sc, SK_MACARB_CTL,
	    SK_MACARBCTL_UNRESET|SK_MACARBCTL_FASTOE_OFF);

	sc_if->sk_link = 1;
}

void
sk_init_yukon(struct sk_if_softc *sc_if)
{
	u_int32_t		phy, v;
	u_int16_t		reg;
	struct sk_softc		*sc;
	int			i;

	sc = sc_if->sk_softc;

	DPRINTFN(2, ("sk_init_yukon: start: sk_csr=%#x\n",
	    CSR_READ_4(sc_if->sk_softc, SK_CSR)));

	if (sc->sk_type == SK_YUKON_LITE &&
	    sc->sk_rev >= SK_YUKON_LITE_REV_A3) {
		/*
		 * Workaround code for COMA mode, set PHY reset.
		 * Otherwise it will not correctly take chip out of
		 * powerdown (coma)
		 */
		v = sk_win_read_4(sc, SK_GPIO);
		v |= SK_GPIO_DIR9 | SK_GPIO_DAT9;
		sk_win_write_4(sc, SK_GPIO, v);
	}

	DPRINTFN(6, ("sk_init_yukon: 1\n"));

	/* GMAC and GPHY Reset */
	SK_IF_WRITE_4(sc_if, 0, SK_GPHY_CTRL, SK_GPHY_RESET_SET);
	SK_IF_WRITE_4(sc_if, 0, SK_GMAC_CTRL, SK_GMAC_RESET_SET);
	DELAY(1000);

	DPRINTFN(6, ("sk_init_yukon: 2\n"));

	if (sc->sk_type == SK_YUKON_LITE &&
	    sc->sk_rev >= SK_YUKON_LITE_REV_A3) {
		/*
		 * Workaround code for COMA mode, clear PHY reset
		 */
		v = sk_win_read_4(sc, SK_GPIO);
		v |= SK_GPIO_DIR9;
		v &= ~SK_GPIO_DAT9;
		sk_win_write_4(sc, SK_GPIO, v);
	}

	phy = SK_GPHY_INT_POL_HI | SK_GPHY_DIS_FC | SK_GPHY_DIS_SLEEP |
		SK_GPHY_ENA_XC | SK_GPHY_ANEG_ALL | SK_GPHY_ENA_PAUSE;

	if (sc->sk_coppertype)
		phy |= SK_GPHY_COPPER;
	else
		phy |= SK_GPHY_FIBER;

	DPRINTFN(3, ("sk_init_yukon: phy=%#x\n", phy));

	SK_IF_WRITE_4(sc_if, 0, SK_GPHY_CTRL, phy | SK_GPHY_RESET_SET);
	DELAY(1000);
	SK_IF_WRITE_4(sc_if, 0, SK_GPHY_CTRL, phy | SK_GPHY_RESET_CLEAR);
	SK_IF_WRITE_4(sc_if, 0, SK_GMAC_CTRL, SK_GMAC_LOOP_OFF |
	    SK_GMAC_PAUSE_ON | SK_GMAC_RESET_CLEAR);

	DPRINTFN(3, ("sk_init_yukon: gmac_ctrl=%#x\n",
	    SK_IF_READ_4(sc_if, 0, SK_GMAC_CTRL)));

	DPRINTFN(6, ("sk_init_yukon: 3\n"));

	/* unused read of the interrupt source register */
	DPRINTFN(6, ("sk_init_yukon: 4\n"));
	SK_IF_READ_2(sc_if, 0, SK_GMAC_ISR);

	DPRINTFN(6, ("sk_init_yukon: 4a\n"));
	reg = SK_YU_READ_2(sc_if, YUKON_PAR);
	DPRINTFN(6, ("sk_init_yukon: YUKON_PAR=%#x\n", reg));

	/* MIB Counter Clear Mode set */
	reg |= YU_PAR_MIB_CLR;
	DPRINTFN(6, ("sk_init_yukon: YUKON_PAR=%#x\n", reg));
	DPRINTFN(6, ("sk_init_yukon: 4b\n"));
	SK_YU_WRITE_2(sc_if, YUKON_PAR, reg);

	/* MIB Counter Clear Mode clear */
	DPRINTFN(6, ("sk_init_yukon: 5\n"));
	reg &= ~YU_PAR_MIB_CLR;
	SK_YU_WRITE_2(sc_if, YUKON_PAR, reg);

	/* receive control reg */
	DPRINTFN(6, ("sk_init_yukon: 7\n"));
	SK_YU_WRITE_2(sc_if, YUKON_RCR, YU_RCR_CRCR);

	/* transmit parameter register */
	DPRINTFN(6, ("sk_init_yukon: 8\n"));
	SK_YU_WRITE_2(sc_if, YUKON_TPR, YU_TPR_JAM_LEN(0x3) |
	    YU_TPR_JAM_IPG(0xb) | YU_TPR_JAM2DATA_IPG(0x1a) );

	/* serial mode register */
	DPRINTFN(6, ("sk_init_yukon: 9\n"));
	SK_YU_WRITE_2(sc_if, YUKON_SMR, YU_SMR_DATA_BLIND(0x1c) |
	    YU_SMR_MFL_VLAN | YU_SMR_MFL_JUMBO | YU_SMR_IPG_DATA(0x1e));

	DPRINTFN(6, ("sk_init_yukon: 10\n"));
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

	/* Program promiscuous mode and multicast filters. */
	DPRINTFN(6, ("sk_init_yukon: 11\n"));
	sk_iff(sc_if);

	/* enable interrupt mask for counter overflows */
	DPRINTFN(6, ("sk_init_yukon: 12\n"));
	SK_YU_WRITE_2(sc_if, YUKON_TIMR, 0);
	SK_YU_WRITE_2(sc_if, YUKON_RIMR, 0);
	SK_YU_WRITE_2(sc_if, YUKON_TRIMR, 0);

	/* Configure RX MAC FIFO Flush Mask */
	v = YU_RXSTAT_FOFL | YU_RXSTAT_CRCERR | YU_RXSTAT_MIIERR |
	    YU_RXSTAT_BADFC | YU_RXSTAT_GOODFC | YU_RXSTAT_RUNT |
	    YU_RXSTAT_JABBER;
	SK_IF_WRITE_2(sc_if, 0, SK_RXMF1_FLUSH_MASK, v);

	/* Disable RX MAC FIFO Flush for YUKON-Lite Rev. A0 only */
	if (sc->sk_type == SK_YUKON_LITE && sc->sk_rev == SK_YUKON_LITE_REV_A0)
		v = SK_TFCTL_OPERATION_ON;
	else
		v = SK_TFCTL_OPERATION_ON | SK_RFCTL_FIFO_FLUSH_ON;
	/* Configure RX MAC FIFO */
	SK_IF_WRITE_1(sc_if, 0, SK_RXMF1_CTRL_TEST, SK_RFCTL_RESET_CLEAR);
	SK_IF_WRITE_2(sc_if, 0, SK_RXMF1_CTRL_TEST, v);

	/* Increase flush threshold to 64 bytes */
	SK_IF_WRITE_2(sc_if, 0, SK_RXMF1_FLUSH_THRESHOLD,
	    SK_RFCTL_FIFO_THRESHOLD + 1);

	/* Configure TX MAC FIFO */
	SK_IF_WRITE_1(sc_if, 0, SK_TXMF1_CTRL_TEST, SK_TFCTL_RESET_CLEAR);
	SK_IF_WRITE_2(sc_if, 0, SK_TXMF1_CTRL_TEST, SK_TFCTL_OPERATION_ON);

	DPRINTFN(6, ("sk_init_yukon: end\n"));
}

/*
 * Note that to properly initialize any part of the GEnesis chip,
 * you first have to take it out of reset mode.
 */
void
sk_init(void *xsc_if)
{
	struct sk_if_softc	*sc_if = xsc_if;
	struct sk_softc		*sc = sc_if->sk_softc;
	struct ifnet		*ifp = &sc_if->arpcom.ac_if;
	struct mii_data		*mii = &sc_if->sk_mii;
	int			s;

	DPRINTFN(2, ("sk_init\n"));

	s = splnet();

	/* Cancel pending I/O and free all RX/TX buffers. */
	sk_stop(sc_if, 0);

	if (SK_IS_GENESIS(sc)) {
		/* Configure LINK_SYNC LED */
		SK_IF_WRITE_1(sc_if, 0, SK_LINKLED1_CTL, SK_LINKLED_ON);
		SK_IF_WRITE_1(sc_if, 0, SK_LINKLED1_CTL,
		    SK_LINKLED_LINKSYNC_ON);

		/* Configure RX LED */
		SK_IF_WRITE_1(sc_if, 0, SK_RXLED1_CTL,
		    SK_RXLEDCTL_COUNTER_START);

		/* Configure TX LED */
		SK_IF_WRITE_1(sc_if, 0, SK_TXLED1_CTL,
		    SK_TXLEDCTL_COUNTER_START);
	}

	/*
	 * Configure descriptor poll timer
	 *
	 * SK-NET GENESIS data sheet says that possibility of losing Start
	 * transmit command due to CPU/cache related interim storage problems
	 * under certain conditions. The document recommends a polling
	 * mechanism to send a Start transmit command to initiate transfer
	 * of ready descriptors regularly. To cope with this issue sk(4) now
	 * enables descriptor poll timer to initiate descriptor processing
	 * periodically as defined by SK_DPT_TIMER_MAX. However sk(4) still
	 * issue SK_TXBMU_TX_START to Tx BMU to get fast execution of Tx
	 * command instead of waiting for next descriptor polling time.
	 * The same rule may apply to Rx side too but it seems that is not
	 * needed at the moment.
	 * Since sk(4) uses descriptor polling as a last resort there is no
	 * need to set smaller polling time than maximum allowable one.
	 */
	SK_IF_WRITE_4(sc_if, 0, SK_DPT_INIT, SK_DPT_TIMER_MAX);

	/* Configure I2C registers */

	/* Configure XMAC(s) */
	switch (sc->sk_type) {
	case SK_GENESIS:
		sk_init_xmac(sc_if);
		break;
	case SK_YUKON:
	case SK_YUKON_LITE:
	case SK_YUKON_LP:
		sk_init_yukon(sc_if);
		break;
	}
	mii_mediachg(mii);

	if (SK_IS_GENESIS(sc)) {
		/* Configure MAC FIFOs */
		SK_IF_WRITE_4(sc_if, 0, SK_RXF1_CTL, SK_FIFO_UNRESET);
		SK_IF_WRITE_4(sc_if, 0, SK_RXF1_END, SK_FIFO_END);
		SK_IF_WRITE_4(sc_if, 0, SK_RXF1_CTL, SK_FIFO_ON);

		SK_IF_WRITE_4(sc_if, 0, SK_TXF1_CTL, SK_FIFO_UNRESET);
		SK_IF_WRITE_4(sc_if, 0, SK_TXF1_END, SK_FIFO_END);
		SK_IF_WRITE_4(sc_if, 0, SK_TXF1_CTL, SK_FIFO_ON);
	}

	/* Configure transmit arbiter(s) */
	SK_IF_WRITE_1(sc_if, 0, SK_TXAR1_COUNTERCTL,
	    SK_TXARCTL_ON|SK_TXARCTL_FSYNC_ON);

	/* Configure RAMbuffers */
	SK_IF_WRITE_4(sc_if, 0, SK_RXRB1_CTLTST, SK_RBCTL_UNRESET);
	SK_IF_WRITE_4(sc_if, 0, SK_RXRB1_START, sc_if->sk_rx_ramstart);
	SK_IF_WRITE_4(sc_if, 0, SK_RXRB1_WR_PTR, sc_if->sk_rx_ramstart);
	SK_IF_WRITE_4(sc_if, 0, SK_RXRB1_RD_PTR, sc_if->sk_rx_ramstart);
	SK_IF_WRITE_4(sc_if, 0, SK_RXRB1_END, sc_if->sk_rx_ramend);
	SK_IF_WRITE_4(sc_if, 0, SK_RXRB1_CTLTST, SK_RBCTL_ON);

	SK_IF_WRITE_4(sc_if, 1, SK_TXRBS1_CTLTST, SK_RBCTL_UNRESET);
	SK_IF_WRITE_4(sc_if, 1, SK_TXRBS1_CTLTST, SK_RBCTL_STORENFWD_ON);
	SK_IF_WRITE_4(sc_if, 1, SK_TXRBS1_START, sc_if->sk_tx_ramstart);
	SK_IF_WRITE_4(sc_if, 1, SK_TXRBS1_WR_PTR, sc_if->sk_tx_ramstart);
	SK_IF_WRITE_4(sc_if, 1, SK_TXRBS1_RD_PTR, sc_if->sk_tx_ramstart);
	SK_IF_WRITE_4(sc_if, 1, SK_TXRBS1_END, sc_if->sk_tx_ramend);
	SK_IF_WRITE_4(sc_if, 1, SK_TXRBS1_CTLTST, SK_RBCTL_ON);

	/* Configure BMUs */
	SK_IF_WRITE_4(sc_if, 0, SK_RXQ1_BMU_CSR, SK_RXBMU_ONLINE);
	SK_IF_WRITE_4(sc_if, 0, SK_RXQ1_CURADDR_LO,
	    SK_RX_RING_ADDR(sc_if, 0));
	SK_IF_WRITE_4(sc_if, 0, SK_RXQ1_CURADDR_HI, 0);

	SK_IF_WRITE_4(sc_if, 1, SK_TXQS1_BMU_CSR, SK_TXBMU_ONLINE);
	SK_IF_WRITE_4(sc_if, 1, SK_TXQS1_CURADDR_LO,
	    SK_TX_RING_ADDR(sc_if, 0));
	SK_IF_WRITE_4(sc_if, 1, SK_TXQS1_CURADDR_HI, 0);

	/* Init descriptors */
	if (sk_init_rx_ring(sc_if) == ENOBUFS) {
		printf("%s: initialization failed: no "
		    "memory for rx buffers\n", sc_if->sk_dev.dv_xname);
		sk_stop(sc_if, 0);
		splx(s);
		return;
	}

	if (sk_init_tx_ring(sc_if) == ENOBUFS) {
		printf("%s: initialization failed: no "
		    "memory for tx buffers\n", sc_if->sk_dev.dv_xname);
		sk_stop(sc_if, 0);
		splx(s);
		return;
	}

	/* Configure interrupt handling */
	CSR_READ_4(sc, SK_ISSR);
	if (sc_if->sk_port == SK_PORT_A)
		sc->sk_intrmask |= SK_INTRS1;
	else
		sc->sk_intrmask |= SK_INTRS2;

	sc->sk_intrmask |= SK_ISR_EXTERNAL_REG;

	CSR_WRITE_4(sc, SK_IMR, sc->sk_intrmask);

	/* Start BMUs. */
	SK_IF_WRITE_4(sc_if, 0, SK_RXQ1_BMU_CSR, SK_RXBMU_RX_START);

	if (SK_IS_GENESIS(sc)) {
		/* Enable XMACs TX and RX state machines */
		SK_XM_CLRBIT_2(sc_if, XM_MMUCMD, XM_MMUCMD_IGNPAUSE);
		SK_XM_SETBIT_2(sc_if, XM_MMUCMD,
		    XM_MMUCMD_TX_ENB|XM_MMUCMD_RX_ENB);
	}

	if (SK_IS_YUKON(sc)) {
		u_int16_t reg = SK_YU_READ_2(sc_if, YUKON_GPCR);
		reg |= YU_GPCR_TXEN | YU_GPCR_RXEN;
		SK_YU_WRITE_2(sc_if, YUKON_GPCR, reg);
	}

	/* Activate descriptor polling timer */
	SK_IF_WRITE_4(sc_if, 0, SK_DPT_TIMER_CTRL, SK_DPT_TCTL_START);
	/* start transfer of Tx descriptors */
	CSR_WRITE_4(sc, sc_if->sk_tx_bmu, SK_TXBMU_TX_START);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	if (SK_IS_YUKON(sc))
		timeout_add_sec(&sc_if->sk_tick_ch, 1);

	splx(s);
}

void
sk_stop(struct sk_if_softc *sc_if, int softonly)
{
	struct sk_softc		*sc = sc_if->sk_softc;
	struct ifnet		*ifp = &sc_if->arpcom.ac_if;
	bus_dmamap_t		dmamap;
	struct sk_txmap_entry	*dma;
	int			i;
	u_int32_t		val;

	DPRINTFN(2, ("sk_stop\n"));

	timeout_del(&sc_if->sk_tick_ch);

	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	if (!softonly) {
		/* stop Tx descriptor polling timer */
		SK_IF_WRITE_4(sc_if, 0, SK_DPT_TIMER_CTRL, SK_DPT_TCTL_STOP);
		/* stop transfer of Tx descriptors */
		CSR_WRITE_4(sc, sc_if->sk_tx_bmu, SK_TXBMU_TX_STOP);
		for (i = 0; i < SK_TIMEOUT; i++) {
			val = CSR_READ_4(sc, sc_if->sk_tx_bmu);
			if (!(val & SK_TXBMU_TX_STOP))
				break;
			DELAY(1);
		}
		if (i == SK_TIMEOUT) {
			printf("%s: cannot stop transfer of Tx descriptors\n",
			    sc_if->sk_dev.dv_xname);
		}
		/* stop transfer of Rx descriptors */
		SK_IF_WRITE_4(sc_if, 0, SK_RXQ1_BMU_CSR, SK_RXBMU_RX_STOP);
		for (i = 0; i < SK_TIMEOUT; i++) {
			val = SK_IF_READ_4(sc_if, 0, SK_RXQ1_BMU_CSR);
			if (!(val & SK_RXBMU_RX_STOP))
				break;
			DELAY(1);
		}
		if (i == SK_TIMEOUT) {
			printf("%s: cannot stop transfer of Rx descriptors\n",
			    sc_if->sk_dev.dv_xname);
		}

		if (sc_if->sk_phytype == SK_PHYTYPE_BCOM) {
			u_int32_t		val;

			/* Put PHY back into reset. */
			val = sk_win_read_4(sc, SK_GPIO);
			if (sc_if->sk_port == SK_PORT_A) {
				val |= SK_GPIO_DIR0;
				val &= ~SK_GPIO_DAT0;
			} else {
				val |= SK_GPIO_DIR2;
				val &= ~SK_GPIO_DAT2;
			}
			sk_win_write_4(sc, SK_GPIO, val);
		}

		/* Turn off various components of this interface. */
		SK_XM_SETBIT_2(sc_if, XM_GPIO, XM_GPIO_RESETMAC);
		switch (sc->sk_type) {
		case SK_GENESIS:
			SK_IF_WRITE_2(sc_if, 0, SK_TXF1_MACCTL,
			    SK_TXMACCTL_XMAC_RESET);
			SK_IF_WRITE_4(sc_if, 0, SK_RXF1_CTL, SK_FIFO_RESET);
			break;
		case SK_YUKON:
		case SK_YUKON_LITE:
		case SK_YUKON_LP:
			SK_IF_WRITE_1(sc_if,0, SK_RXMF1_CTRL_TEST, SK_RFCTL_RESET_SET);
			SK_IF_WRITE_1(sc_if,0, SK_TXMF1_CTRL_TEST, SK_TFCTL_RESET_SET);
			break;
		}
		SK_IF_WRITE_4(sc_if, 0, SK_RXQ1_BMU_CSR, SK_RXBMU_OFFLINE);
		SK_IF_WRITE_4(sc_if, 0, SK_RXRB1_CTLTST, SK_RBCTL_RESET|SK_RBCTL_OFF);
		SK_IF_WRITE_4(sc_if, 1, SK_TXQS1_BMU_CSR, SK_TXBMU_OFFLINE);
		SK_IF_WRITE_4(sc_if, 1, SK_TXRBS1_CTLTST, SK_RBCTL_RESET|SK_RBCTL_OFF);
		SK_IF_WRITE_1(sc_if, 0, SK_TXAR1_COUNTERCTL, SK_TXARCTL_OFF);
		SK_IF_WRITE_1(sc_if, 0, SK_RXLED1_CTL, SK_RXLEDCTL_COUNTER_STOP);
		SK_IF_WRITE_1(sc_if, 0, SK_TXLED1_CTL, SK_RXLEDCTL_COUNTER_STOP);
		SK_IF_WRITE_1(sc_if, 0, SK_LINKLED1_CTL, SK_LINKLED_OFF);
		SK_IF_WRITE_1(sc_if, 0, SK_LINKLED1_CTL, SK_LINKLED_LINKSYNC_OFF);

		/* Disable interrupts */
		if (sc_if->sk_port == SK_PORT_A)
			sc->sk_intrmask &= ~SK_INTRS1;
		else
			sc->sk_intrmask &= ~SK_INTRS2;
		CSR_WRITE_4(sc, SK_IMR, sc->sk_intrmask);

		SK_XM_READ_2(sc_if, XM_ISR);
		SK_XM_WRITE_2(sc_if, XM_IMR, 0xFFFF);
	}

	/* Free RX and TX mbufs still in the queues. */
	for (i = 0; i < SK_RX_RING_CNT; i++) {
		if (sc_if->sk_cdata.sk_rx_chain[i].sk_mbuf != NULL) {
			dmamap = sc_if->sk_cdata.sk_rx_map[i];
			bus_dmamap_sync(sc_if->sk_softc->sc_dmatag, dmamap, 0,
			    dmamap->dm_mapsize, BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(sc_if->sk_softc->sc_dmatag, dmamap);
			m_freem(sc_if->sk_cdata.sk_rx_chain[i].sk_mbuf);
			sc_if->sk_cdata.sk_rx_chain[i].sk_mbuf = NULL;
		}
	}

	for (i = 0; i < SK_TX_RING_CNT; i++) {
		if (sc_if->sk_cdata.sk_tx_chain[i].sk_mbuf != NULL) {
			m_freem(sc_if->sk_cdata.sk_tx_chain[i].sk_mbuf);
			sc_if->sk_cdata.sk_tx_chain[i].sk_mbuf = NULL;
			SIMPLEQ_INSERT_HEAD(&sc_if->sk_txmap_head,
			    sc_if->sk_cdata.sk_tx_map[i], link);
			sc_if->sk_cdata.sk_tx_map[i] = 0;
		}
	}

	while ((dma = SIMPLEQ_FIRST(&sc_if->sk_txmap_head))) {
		SIMPLEQ_REMOVE_HEAD(&sc_if->sk_txmap_head, link);
		bus_dmamap_destroy(sc->sc_dmatag, dma->dmamap);
		free(dma, M_DEVBUF, 0);
	}
}

const struct cfattach skc_ca = {
	sizeof(struct sk_softc), skc_probe, skc_attach, skc_detach,
	skc_activate
};

struct cfdriver skc_cd = {
	NULL, "skc", DV_DULL
};

const struct cfattach sk_ca = {
	sizeof(struct sk_if_softc), sk_probe, sk_attach, sk_detach,
	sk_activate
};

struct cfdriver sk_cd = {
	NULL, "sk", DV_IFNET
};

#ifdef SK_DEBUG
void
sk_dump_txdesc(struct sk_tx_desc *desc, int idx)
{
#define DESC_PRINT(X)					\
	if (X)						\
		printf("txdesc[%d]." #X "=%#x\n", idx, X);

	DESC_PRINT(letoh32(desc->sk_ctl));
	DESC_PRINT(letoh32(desc->sk_next));
	DESC_PRINT(letoh32(desc->sk_data_lo));
	DESC_PRINT(letoh32(desc->sk_data_hi));
	DESC_PRINT(letoh32(desc->sk_xmac_txstat));
	DESC_PRINT(letoh16(desc->sk_rsvd0));
	DESC_PRINT(letoh16(desc->sk_rsvd1));
#undef PRINT
}

void
sk_dump_bytes(const char *data, int len)
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
sk_dump_mbuf(struct mbuf *m)
{
	int count = m->m_pkthdr.len;

	printf("m=%#lx, m->m_pkthdr.len=%#d\n", m, m->m_pkthdr.len);

	while (count > 0 && m) {
		printf("m=%#lx, m->m_data=%#lx, m->m_len=%d\n",
		    m, m->m_data, m->m_len);
		sk_dump_bytes(mtod(m, char *), m->m_len);

		count -= m->m_len;
		m = m->m_next;
	}
}
#endif
