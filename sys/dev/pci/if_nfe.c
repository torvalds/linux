/*	$OpenBSD: if_nfe.c,v 1.127 2024/08/31 16:23:09 deraadt Exp $	*/

/*-
 * Copyright (c) 2006, 2007 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2005, 2006 Jonathan Gray <jsg@openbsd.org>
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

/* Driver for NVIDIA nForce MCP Fast Ethernet and Gigabit Ethernet */

#include "bpfilter.h"
#include "vlan.h"

#include <sys/param.h>
#include <sys/endian.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/queue.h>
#include <sys/device.h>
#include <sys/timeout.h>

#include <machine/bus.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <dev/mii/miivar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/pci/if_nfereg.h>
#include <dev/pci/if_nfevar.h>

int	nfe_match(struct device *, void *, void *);
void	nfe_attach(struct device *, struct device *, void *);
int	nfe_activate(struct device *, int);
void	nfe_miibus_statchg(struct device *);
int	nfe_miibus_readreg(struct device *, int, int);
void	nfe_miibus_writereg(struct device *, int, int, int);
int	nfe_intr(void *);
int	nfe_ioctl(struct ifnet *, u_long, caddr_t);
void	nfe_txdesc32_sync(struct nfe_softc *, struct nfe_desc32 *, int);
void	nfe_txdesc64_sync(struct nfe_softc *, struct nfe_desc64 *, int);
void	nfe_txdesc32_rsync(struct nfe_softc *, int, int, int);
void	nfe_txdesc64_rsync(struct nfe_softc *, int, int, int);
void	nfe_rxdesc32_sync(struct nfe_softc *, struct nfe_desc32 *, int);
void	nfe_rxdesc64_sync(struct nfe_softc *, struct nfe_desc64 *, int);
void	nfe_rxeof(struct nfe_softc *);
void	nfe_txeof(struct nfe_softc *);
int	nfe_encap(struct nfe_softc *, struct mbuf *);
void	nfe_start(struct ifnet *);
void	nfe_watchdog(struct ifnet *);
int	nfe_init(struct ifnet *);
void	nfe_stop(struct ifnet *, int);
int	nfe_alloc_rx_ring(struct nfe_softc *, struct nfe_rx_ring *);
void	nfe_reset_rx_ring(struct nfe_softc *, struct nfe_rx_ring *);
void	nfe_free_rx_ring(struct nfe_softc *, struct nfe_rx_ring *);
int	nfe_alloc_tx_ring(struct nfe_softc *, struct nfe_tx_ring *);
void	nfe_reset_tx_ring(struct nfe_softc *, struct nfe_tx_ring *);
void	nfe_free_tx_ring(struct nfe_softc *, struct nfe_tx_ring *);
int	nfe_ifmedia_upd(struct ifnet *);
void	nfe_ifmedia_sts(struct ifnet *, struct ifmediareq *);
void	nfe_iff(struct nfe_softc *);
void	nfe_get_macaddr(struct nfe_softc *, uint8_t *);
void	nfe_set_macaddr(struct nfe_softc *, const uint8_t *);
void	nfe_tick(void *);
#ifndef SMALL_KERNEL
int	nfe_wol(struct ifnet*, int);
#endif

const struct cfattach nfe_ca = {
	sizeof (struct nfe_softc), nfe_match, nfe_attach, NULL,
	nfe_activate
};

struct cfdriver nfe_cd = {
	NULL, "nfe", DV_IFNET
};

#ifdef NFE_DEBUG
int nfedebug = 0;
#define DPRINTF(x)	do { if (nfedebug) printf x; } while (0)
#define DPRINTFN(n,x)	do { if (nfedebug >= (n)) printf x; } while (0)
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

const struct pci_matchid nfe_devices[] = {
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_NFORCE_LAN },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_NFORCE2_LAN },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_NFORCE3_LAN1 },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_NFORCE3_LAN2 },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_NFORCE3_LAN3 },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_NFORCE3_LAN4 },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_NFORCE3_LAN5 },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_CK804_LAN1 },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_CK804_LAN2 },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP04_LAN1 },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP04_LAN2 },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP51_LAN1 },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP51_LAN2 },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP55_LAN1 },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP55_LAN2 },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP61_LAN1 },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP61_LAN2 },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP61_LAN3 },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP61_LAN4 },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP65_LAN1 },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP65_LAN2 },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP65_LAN3 },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP65_LAN4 },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP67_LAN1 },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP67_LAN2 },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP67_LAN3 },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP67_LAN4 },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP73_LAN1 },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP73_LAN2 },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP73_LAN3 },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP73_LAN4 },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP77_LAN1 },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP77_LAN2 },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP77_LAN3 },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP77_LAN4 },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP79_LAN1 },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP79_LAN2 },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP79_LAN3 },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP79_LAN4 },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_MCP89_LAN }
};

int
nfe_match(struct device *dev, void *match, void *aux)
{
	return pci_matchbyid((struct pci_attach_args *)aux, nfe_devices,
	    sizeof (nfe_devices) / sizeof (nfe_devices[0]));
}

int
nfe_activate(struct device *self, int act)
{
	struct nfe_softc *sc = (struct nfe_softc *)self;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;

	switch (act) {
	case DVACT_SUSPEND:
		if (ifp->if_flags & IFF_RUNNING)
			nfe_stop(ifp, 0);
		break;
	case DVACT_RESUME:
		if (ifp->if_flags & IFF_UP)
			nfe_init(ifp);
		break;
	}
	return (0);
}


void
nfe_attach(struct device *parent, struct device *self, void *aux)
{
	struct nfe_softc *sc = (struct nfe_softc *)self;
	struct pci_attach_args *pa = aux;
	pci_chipset_tag_t pc = pa->pa_pc;
	pci_intr_handle_t ih;
	const char *intrstr;
	struct ifnet *ifp;
	bus_size_t memsize;
	pcireg_t memtype;

	memtype = pci_mapreg_type(pa->pa_pc, pa->pa_tag, NFE_PCI_BA);
	if (pci_mapreg_map(pa, NFE_PCI_BA, memtype, 0, &sc->sc_memt,
	    &sc->sc_memh, NULL, &memsize, 0)) {
		printf(": can't map mem space\n");
		return;
	}

	if (pci_intr_map(pa, &ih) != 0) {
		printf(": can't map interrupt\n");
		return;
	}

	intrstr = pci_intr_string(pc, ih);
	sc->sc_ih = pci_intr_establish(pc, ih, IPL_NET, nfe_intr, sc,
	    sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": could not establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		return;
	}
	printf(": %s", intrstr);

	sc->sc_dmat = pa->pa_dmat;
	sc->sc_flags = 0;

	switch (PCI_PRODUCT(pa->pa_id)) {
	case PCI_PRODUCT_NVIDIA_NFORCE3_LAN2:
	case PCI_PRODUCT_NVIDIA_NFORCE3_LAN3:
	case PCI_PRODUCT_NVIDIA_NFORCE3_LAN4:
	case PCI_PRODUCT_NVIDIA_NFORCE3_LAN5:
		sc->sc_flags |= NFE_JUMBO_SUP | NFE_HW_CSUM;
		break;
	case PCI_PRODUCT_NVIDIA_MCP51_LAN1:
	case PCI_PRODUCT_NVIDIA_MCP51_LAN2:
		sc->sc_flags |= NFE_40BIT_ADDR | NFE_PWR_MGMT;
		break;
	case PCI_PRODUCT_NVIDIA_MCP61_LAN1:
	case PCI_PRODUCT_NVIDIA_MCP61_LAN2:
	case PCI_PRODUCT_NVIDIA_MCP61_LAN3:
	case PCI_PRODUCT_NVIDIA_MCP61_LAN4:
	case PCI_PRODUCT_NVIDIA_MCP67_LAN1:
	case PCI_PRODUCT_NVIDIA_MCP67_LAN2:
	case PCI_PRODUCT_NVIDIA_MCP67_LAN3:
	case PCI_PRODUCT_NVIDIA_MCP67_LAN4:
	case PCI_PRODUCT_NVIDIA_MCP73_LAN1:
	case PCI_PRODUCT_NVIDIA_MCP73_LAN2:
	case PCI_PRODUCT_NVIDIA_MCP73_LAN3:
	case PCI_PRODUCT_NVIDIA_MCP73_LAN4:
		sc->sc_flags |= NFE_40BIT_ADDR | NFE_CORRECT_MACADDR |
		    NFE_PWR_MGMT;
		break;
	case PCI_PRODUCT_NVIDIA_MCP77_LAN1:
	case PCI_PRODUCT_NVIDIA_MCP77_LAN2:
	case PCI_PRODUCT_NVIDIA_MCP77_LAN3:
	case PCI_PRODUCT_NVIDIA_MCP77_LAN4:
		sc->sc_flags |= NFE_40BIT_ADDR | NFE_HW_CSUM |
		    NFE_CORRECT_MACADDR | NFE_PWR_MGMT;
		break;
	case PCI_PRODUCT_NVIDIA_MCP79_LAN1:
	case PCI_PRODUCT_NVIDIA_MCP79_LAN2:
	case PCI_PRODUCT_NVIDIA_MCP79_LAN3:
	case PCI_PRODUCT_NVIDIA_MCP79_LAN4:
	case PCI_PRODUCT_NVIDIA_MCP89_LAN:
		sc->sc_flags |= NFE_JUMBO_SUP | NFE_40BIT_ADDR | NFE_HW_CSUM |
		    NFE_CORRECT_MACADDR | NFE_PWR_MGMT;
		break;
	case PCI_PRODUCT_NVIDIA_CK804_LAN1:
	case PCI_PRODUCT_NVIDIA_CK804_LAN2:
	case PCI_PRODUCT_NVIDIA_MCP04_LAN1:
	case PCI_PRODUCT_NVIDIA_MCP04_LAN2:
		sc->sc_flags |= NFE_JUMBO_SUP | NFE_40BIT_ADDR | NFE_HW_CSUM;
		break;
	case PCI_PRODUCT_NVIDIA_MCP65_LAN1:
	case PCI_PRODUCT_NVIDIA_MCP65_LAN2:
	case PCI_PRODUCT_NVIDIA_MCP65_LAN3:
	case PCI_PRODUCT_NVIDIA_MCP65_LAN4:
		sc->sc_flags |= NFE_JUMBO_SUP | NFE_40BIT_ADDR |
		    NFE_CORRECT_MACADDR | NFE_PWR_MGMT;
		break;
	case PCI_PRODUCT_NVIDIA_MCP55_LAN1:
	case PCI_PRODUCT_NVIDIA_MCP55_LAN2:
		sc->sc_flags |= NFE_JUMBO_SUP | NFE_40BIT_ADDR | NFE_HW_CSUM |
		    NFE_HW_VLAN | NFE_PWR_MGMT;
		break;
	}

	if (sc->sc_flags & NFE_PWR_MGMT) {
		NFE_WRITE(sc, NFE_RXTX_CTL, NFE_RXTX_RESET | NFE_RXTX_BIT2);
		NFE_WRITE(sc, NFE_MAC_RESET, NFE_MAC_RESET_MAGIC);
		DELAY(100);
		NFE_WRITE(sc, NFE_MAC_RESET, 0);
		DELAY(100);
		NFE_WRITE(sc, NFE_RXTX_CTL, NFE_RXTX_BIT2);
		NFE_WRITE(sc, NFE_PWR2_CTL,
		    NFE_READ(sc, NFE_PWR2_CTL) & ~NFE_PWR2_WAKEUP_MASK);
	}

	nfe_get_macaddr(sc, sc->sc_arpcom.ac_enaddr);
	printf(", address %s\n", ether_sprintf(sc->sc_arpcom.ac_enaddr));

	/*
	 * Allocate Tx and Rx rings.
	 */
	if (nfe_alloc_tx_ring(sc, &sc->txq) != 0) {
		printf("%s: could not allocate Tx ring\n",
		    sc->sc_dev.dv_xname);
		return;
	}

	if (nfe_alloc_rx_ring(sc, &sc->rxq) != 0) {
		printf("%s: could not allocate Rx ring\n",
		    sc->sc_dev.dv_xname);
		nfe_free_tx_ring(sc, &sc->txq);
		return;
	}

	ifp = &sc->sc_arpcom.ac_if;
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = nfe_ioctl;
	ifp->if_start = nfe_start;
	ifp->if_watchdog = nfe_watchdog;
	ifq_init_maxlen(&ifp->if_snd, NFE_IFQ_MAXLEN);
	strlcpy(ifp->if_xname, sc->sc_dev.dv_xname, IFNAMSIZ);

	ifp->if_capabilities = IFCAP_VLAN_MTU;

#ifndef SMALL_KERNEL
	ifp->if_capabilities |= IFCAP_WOL;
	ifp->if_wol = nfe_wol;
	nfe_wol(ifp, 0);
#endif

#if NVLAN > 0
	if (sc->sc_flags & NFE_HW_VLAN)
		ifp->if_capabilities |= IFCAP_VLAN_HWTAGGING;
#endif

	if (sc->sc_flags & NFE_HW_CSUM) {
		ifp->if_capabilities |= IFCAP_CSUM_IPv4 | IFCAP_CSUM_TCPv4 |
		    IFCAP_CSUM_UDPv4;
	}

	sc->sc_mii.mii_ifp = ifp;
	sc->sc_mii.mii_readreg = nfe_miibus_readreg;
	sc->sc_mii.mii_writereg = nfe_miibus_writereg;
	sc->sc_mii.mii_statchg = nfe_miibus_statchg;

	ifmedia_init(&sc->sc_mii.mii_media, 0, nfe_ifmedia_upd,
	    nfe_ifmedia_sts);
	mii_attach(self, &sc->sc_mii, 0xffffffff, MII_PHY_ANY, 0, 0);
	if (LIST_FIRST(&sc->sc_mii.mii_phys) == NULL) {
		printf("%s: no PHY found!\n", sc->sc_dev.dv_xname);
		ifmedia_add(&sc->sc_mii.mii_media, IFM_ETHER | IFM_MANUAL,
		    0, NULL);
		ifmedia_set(&sc->sc_mii.mii_media, IFM_ETHER | IFM_MANUAL);
	} else
		ifmedia_set(&sc->sc_mii.mii_media, IFM_ETHER | IFM_AUTO);

	if_attach(ifp);
	ether_ifattach(ifp);

	timeout_set(&sc->sc_tick_ch, nfe_tick, sc);
}

void
nfe_miibus_statchg(struct device *dev)
{
	struct nfe_softc *sc = (struct nfe_softc *)dev;
	struct mii_data *mii = &sc->sc_mii;
	uint32_t phy, seed, misc = NFE_MISC1_MAGIC, link = NFE_MEDIA_SET;

	phy = NFE_READ(sc, NFE_PHY_IFACE);
	phy &= ~(NFE_PHY_HDX | NFE_PHY_100TX | NFE_PHY_1000T);

	seed = NFE_READ(sc, NFE_RNDSEED);
	seed &= ~NFE_SEED_MASK;

	if ((mii->mii_media_active & IFM_GMASK) == IFM_HDX) {
		phy  |= NFE_PHY_HDX;	/* half-duplex */
		misc |= NFE_MISC1_HDX;
	}

	switch (IFM_SUBTYPE(mii->mii_media_active)) {
	case IFM_1000_T:	/* full-duplex only */
		link |= NFE_MEDIA_1000T;
		seed |= NFE_SEED_1000T;
		phy  |= NFE_PHY_1000T;
		break;
	case IFM_100_TX:
		link |= NFE_MEDIA_100TX;
		seed |= NFE_SEED_100TX;
		phy  |= NFE_PHY_100TX;
		break;
	case IFM_10_T:
		link |= NFE_MEDIA_10T;
		seed |= NFE_SEED_10T;
		break;
	}

	NFE_WRITE(sc, NFE_RNDSEED, seed);	/* XXX: gigabit NICs only? */

	NFE_WRITE(sc, NFE_PHY_IFACE, phy);
	NFE_WRITE(sc, NFE_MISC1, misc);
	NFE_WRITE(sc, NFE_LINKSPEED, link);
}

int
nfe_miibus_readreg(struct device *dev, int phy, int reg)
{
	struct nfe_softc *sc = (struct nfe_softc *)dev;
	uint32_t val;
	int ntries;

	NFE_WRITE(sc, NFE_PHY_STATUS, 0xf);

	if (NFE_READ(sc, NFE_PHY_CTL) & NFE_PHY_BUSY) {
		NFE_WRITE(sc, NFE_PHY_CTL, NFE_PHY_BUSY);
		DELAY(100);
	}

	NFE_WRITE(sc, NFE_PHY_CTL, (phy << NFE_PHYADD_SHIFT) | reg);

	for (ntries = 0; ntries < 1000; ntries++) {
		DELAY(100);
		if (!(NFE_READ(sc, NFE_PHY_CTL) & NFE_PHY_BUSY))
			break;
	}
	if (ntries == 1000) {
		DPRINTFN(2, ("%s: timeout waiting for PHY\n",
		    sc->sc_dev.dv_xname));
		return 0;
	}

	if (NFE_READ(sc, NFE_PHY_STATUS) & NFE_PHY_ERROR) {
		DPRINTFN(2, ("%s: could not read PHY\n",
		    sc->sc_dev.dv_xname));
		return 0;
	}

	val = NFE_READ(sc, NFE_PHY_DATA);
	if (val != 0xffffffff && val != 0)
		sc->mii_phyaddr = phy;

	DPRINTFN(2, ("%s: mii read phy %d reg 0x%x ret 0x%x\n",
	    sc->sc_dev.dv_xname, phy, reg, val));

	return val;
}

void
nfe_miibus_writereg(struct device *dev, int phy, int reg, int val)
{
	struct nfe_softc *sc = (struct nfe_softc *)dev;
	uint32_t ctl;
	int ntries;

	NFE_WRITE(sc, NFE_PHY_STATUS, 0xf);

	if (NFE_READ(sc, NFE_PHY_CTL) & NFE_PHY_BUSY) {
		NFE_WRITE(sc, NFE_PHY_CTL, NFE_PHY_BUSY);
		DELAY(100);
	}

	NFE_WRITE(sc, NFE_PHY_DATA, val);
	ctl = NFE_PHY_WRITE | (phy << NFE_PHYADD_SHIFT) | reg;
	NFE_WRITE(sc, NFE_PHY_CTL, ctl);

	for (ntries = 0; ntries < 1000; ntries++) {
		DELAY(100);
		if (!(NFE_READ(sc, NFE_PHY_CTL) & NFE_PHY_BUSY))
			break;
	}
#ifdef NFE_DEBUG
	if (nfedebug >= 2 && ntries == 1000)
		printf("could not write to PHY\n");
#endif
}

int
nfe_intr(void *arg)
{
	struct nfe_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	uint32_t r;

	if ((r = NFE_READ(sc, NFE_IRQ_STATUS) & NFE_IRQ_WANTED) == 0)
		return 0;	/* not for us */
	NFE_WRITE(sc, NFE_IRQ_STATUS, r);

	DPRINTFN(5, ("nfe_intr: interrupt register %x\n", r));

	if (r & NFE_IRQ_LINK) {
		NFE_READ(sc, NFE_PHY_STATUS);
		NFE_WRITE(sc, NFE_PHY_STATUS, 0xf);
		DPRINTF(("%s: link state changed\n", sc->sc_dev.dv_xname));
	}

	if (ifp->if_flags & IFF_RUNNING) {
		/* check Rx ring */
		nfe_rxeof(sc);

		/* check Tx ring */
		nfe_txeof(sc);
	}

	return 1;
}

int
nfe_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct nfe_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int s, error = 0;

	s = splnet();

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		if (!(ifp->if_flags & IFF_RUNNING))
			nfe_init(ifp);
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				error = ENETRESET;
			else
				nfe_init(ifp);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				nfe_stop(ifp, 1);
		}
		break;

	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_mii.mii_media, cmd);
		break;

	default:
		error = ether_ioctl(ifp, &sc->sc_arpcom, cmd, data);
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			nfe_iff(sc);
		error = 0;
	}

	splx(s);
	return error;
}

void
nfe_txdesc32_sync(struct nfe_softc *sc, struct nfe_desc32 *desc32, int ops)
{
	bus_dmamap_sync(sc->sc_dmat, sc->txq.map,
	    (caddr_t)desc32 - (caddr_t)sc->txq.desc32,
	    sizeof (struct nfe_desc32), ops);
}

void
nfe_txdesc64_sync(struct nfe_softc *sc, struct nfe_desc64 *desc64, int ops)
{
	bus_dmamap_sync(sc->sc_dmat, sc->txq.map,
	    (caddr_t)desc64 - (caddr_t)sc->txq.desc64,
	    sizeof (struct nfe_desc64), ops);
}

void
nfe_txdesc32_rsync(struct nfe_softc *sc, int start, int end, int ops)
{
	if (end > start) {
		bus_dmamap_sync(sc->sc_dmat, sc->txq.map,
		    (caddr_t)&sc->txq.desc32[start] - (caddr_t)sc->txq.desc32,
		    (caddr_t)&sc->txq.desc32[end] -
		    (caddr_t)&sc->txq.desc32[start], ops);
		return;
	}
	/* sync from 'start' to end of ring */
	bus_dmamap_sync(sc->sc_dmat, sc->txq.map,
	    (caddr_t)&sc->txq.desc32[start] - (caddr_t)sc->txq.desc32,
	    (caddr_t)&sc->txq.desc32[NFE_TX_RING_COUNT] -
	    (caddr_t)&sc->txq.desc32[start], ops);

	/* sync from start of ring to 'end' */
	bus_dmamap_sync(sc->sc_dmat, sc->txq.map, 0,
	    (caddr_t)&sc->txq.desc32[end] - (caddr_t)sc->txq.desc32, ops);
}

void
nfe_txdesc64_rsync(struct nfe_softc *sc, int start, int end, int ops)
{
	if (end > start) {
		bus_dmamap_sync(sc->sc_dmat, sc->txq.map,
		    (caddr_t)&sc->txq.desc64[start] - (caddr_t)sc->txq.desc64,
		    (caddr_t)&sc->txq.desc64[end] -
		    (caddr_t)&sc->txq.desc64[start], ops);
		return;
	}
	/* sync from 'start' to end of ring */
	bus_dmamap_sync(sc->sc_dmat, sc->txq.map,
	    (caddr_t)&sc->txq.desc64[start] - (caddr_t)sc->txq.desc64,
	    (caddr_t)&sc->txq.desc64[NFE_TX_RING_COUNT] -
	    (caddr_t)&sc->txq.desc64[start], ops);

	/* sync from start of ring to 'end' */
	bus_dmamap_sync(sc->sc_dmat, sc->txq.map, 0,
	    (caddr_t)&sc->txq.desc64[end] - (caddr_t)sc->txq.desc64, ops);
}

void
nfe_rxdesc32_sync(struct nfe_softc *sc, struct nfe_desc32 *desc32, int ops)
{
	bus_dmamap_sync(sc->sc_dmat, sc->rxq.map,
	    (caddr_t)desc32 - (caddr_t)sc->rxq.desc32,
	    sizeof (struct nfe_desc32), ops);
}

void
nfe_rxdesc64_sync(struct nfe_softc *sc, struct nfe_desc64 *desc64, int ops)
{
	bus_dmamap_sync(sc->sc_dmat, sc->rxq.map,
	    (caddr_t)desc64 - (caddr_t)sc->rxq.desc64,
	    sizeof (struct nfe_desc64), ops);
}

void
nfe_rxeof(struct nfe_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct nfe_desc32 *desc32;
	struct nfe_desc64 *desc64;
	struct nfe_rx_data *data;
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct mbuf *m, *mnew;
	bus_addr_t physaddr;
#if NVLAN > 0
	uint32_t vtag;
#endif
	uint16_t flags;
	int error, len;

	for (;;) {
		data = &sc->rxq.data[sc->rxq.cur];

		if (sc->sc_flags & NFE_40BIT_ADDR) {
			desc64 = &sc->rxq.desc64[sc->rxq.cur];
			nfe_rxdesc64_sync(sc, desc64, BUS_DMASYNC_POSTREAD);

			flags = letoh16(desc64->flags);
			len = letoh16(desc64->length) & 0x3fff;
#if NVLAN > 0
			vtag = letoh32(desc64->physaddr[1]);
#endif
		} else {
			desc32 = &sc->rxq.desc32[sc->rxq.cur];
			nfe_rxdesc32_sync(sc, desc32, BUS_DMASYNC_POSTREAD);

			flags = letoh16(desc32->flags);
			len = letoh16(desc32->length) & 0x3fff;
		}

		if (flags & NFE_RX_READY)
			break;

		if ((sc->sc_flags & (NFE_JUMBO_SUP | NFE_40BIT_ADDR)) == 0) {
			if (!(flags & NFE_RX_VALID_V1))
				goto skip;

			if ((flags & NFE_RX_FIXME_V1) == NFE_RX_FIXME_V1) {
				flags &= ~NFE_RX_ERROR;
				len--;	/* fix buffer length */
			}
		} else {
			if (!(flags & NFE_RX_VALID_V2))
				goto skip;

			if ((flags & NFE_RX_FIXME_V2) == NFE_RX_FIXME_V2) {
				flags &= ~NFE_RX_ERROR;
				len--;	/* fix buffer length */
			}
		}

		if (flags & NFE_RX_ERROR) {
			ifp->if_ierrors++;
			goto skip;
		}

		/*
		 * Try to allocate a new mbuf for this ring element and load
		 * it before processing the current mbuf. If the ring element
		 * cannot be loaded, drop the received packet and reuse the
		 * old mbuf. In the unlikely case that the old mbuf can't be
		 * reloaded either, explicitly panic.
		 */
		mnew = MCLGETL(NULL, M_DONTWAIT, MCLBYTES);
		if (mnew == NULL) {
			ifp->if_ierrors++;
			goto skip;
		}
		mnew->m_pkthdr.len = mnew->m_len = MCLBYTES;

		bus_dmamap_sync(sc->sc_dmat, data->map, 0,
		    data->map->dm_mapsize, BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->sc_dmat, data->map);

		error = bus_dmamap_load_mbuf(sc->sc_dmat, data->map, mnew,
		    BUS_DMA_READ | BUS_DMA_NOWAIT);
		if (error != 0) {
			m_freem(mnew);

			/* try to reload the old mbuf */
			error = bus_dmamap_load_mbuf(sc->sc_dmat, data->map,
			    m, BUS_DMA_READ | BUS_DMA_NOWAIT);
			if (error != 0) {
				/* very unlikely that it will fail.. */
				panic("%s: could not load old rx mbuf",
				    sc->sc_dev.dv_xname);
			}
			ifp->if_ierrors++;
			goto skip;
		}
		physaddr = data->map->dm_segs[0].ds_addr;

		/*
		 * New mbuf successfully loaded, update Rx ring and continue
		 * processing.
		 */
		m = data->m;
		data->m = mnew;

		/* finalize mbuf */
		m->m_pkthdr.len = m->m_len = len;

		if ((sc->sc_flags & NFE_HW_CSUM) &&
		    (flags & NFE_RX_IP_CSUMOK)) {
			m->m_pkthdr.csum_flags |= M_IPV4_CSUM_IN_OK;
			if (flags & NFE_RX_UDP_CSUMOK)
				m->m_pkthdr.csum_flags |= M_UDP_CSUM_IN_OK;
			if (flags & NFE_RX_TCP_CSUMOK)
				m->m_pkthdr.csum_flags |= M_TCP_CSUM_IN_OK;
		}

#if NVLAN > 0
		if ((vtag & NFE_RX_VTAG) && (sc->sc_flags & NFE_HW_VLAN)) {
			m->m_pkthdr.ether_vtag = vtag & 0xffff;
			m->m_flags |= M_VLANTAG;
		}
#endif

		ml_enqueue(&ml, m);

		/* update mapping address in h/w descriptor */
		if (sc->sc_flags & NFE_40BIT_ADDR) {
#if defined(__LP64__)
			desc64->physaddr[0] = htole32(physaddr >> 32);
#endif
			desc64->physaddr[1] = htole32(physaddr & 0xffffffff);
		} else {
			desc32->physaddr = htole32(physaddr);
		}

skip:		if (sc->sc_flags & NFE_40BIT_ADDR) {
			desc64->length = htole16(sc->rxq.bufsz);
			desc64->flags = htole16(NFE_RX_READY);

			nfe_rxdesc64_sync(sc, desc64, BUS_DMASYNC_PREWRITE);
		} else {
			desc32->length = htole16(sc->rxq.bufsz);
			desc32->flags = htole16(NFE_RX_READY);

			nfe_rxdesc32_sync(sc, desc32, BUS_DMASYNC_PREWRITE);
		}

		sc->rxq.cur = (sc->rxq.cur + 1) % NFE_RX_RING_COUNT;
	}
	if_input(ifp, &ml);
}

void
nfe_txeof(struct nfe_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct nfe_desc32 *desc32;
	struct nfe_desc64 *desc64;
	struct nfe_tx_data *data = NULL;
	uint16_t flags;

	while (sc->txq.next != sc->txq.cur) {
		if (sc->sc_flags & NFE_40BIT_ADDR) {
			desc64 = &sc->txq.desc64[sc->txq.next];
			nfe_txdesc64_sync(sc, desc64, BUS_DMASYNC_POSTREAD);

			flags = letoh16(desc64->flags);
		} else {
			desc32 = &sc->txq.desc32[sc->txq.next];
			nfe_txdesc32_sync(sc, desc32, BUS_DMASYNC_POSTREAD);

			flags = letoh16(desc32->flags);
		}

		if (flags & NFE_TX_VALID)
			break;

		data = &sc->txq.data[sc->txq.next];

		if ((sc->sc_flags & (NFE_JUMBO_SUP | NFE_40BIT_ADDR)) == 0) {
			if (!(flags & NFE_TX_LASTFRAG_V1) && data->m == NULL)
				goto skip;

			if ((flags & NFE_TX_ERROR_V1) != 0) {
				printf("%s: tx v1 error %b\n",
				    sc->sc_dev.dv_xname, flags, NFE_V1_TXERR);
				ifp->if_oerrors++;
			}
		} else {
			if (!(flags & NFE_TX_LASTFRAG_V2) && data->m == NULL)
				goto skip;

			if ((flags & NFE_TX_ERROR_V2) != 0) {
				printf("%s: tx v2 error %b\n",
				    sc->sc_dev.dv_xname, flags, NFE_V2_TXERR);
				ifp->if_oerrors++;
			}
		}

		if (data->m == NULL) {	/* should not get there */
			printf("%s: last fragment bit w/o associated mbuf!\n",
			    sc->sc_dev.dv_xname);
			goto skip;
		}

		/* last fragment of the mbuf chain transmitted */
		bus_dmamap_sync(sc->sc_dmat, data->active, 0,
		    data->active->dm_mapsize, BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, data->active);
		m_freem(data->m);
		data->m = NULL;

		ifp->if_timer = 0;

skip:		sc->txq.queued--;
		sc->txq.next = (sc->txq.next + 1) % NFE_TX_RING_COUNT;
	}

	if (data != NULL) {	/* at least one slot freed */
		ifq_clr_oactive(&ifp->if_snd);
		nfe_start(ifp);
	}
}

int
nfe_encap(struct nfe_softc *sc, struct mbuf *m0)
{
	struct nfe_desc32 *desc32;
	struct nfe_desc64 *desc64;
	struct nfe_tx_data *data;
	bus_dmamap_t map;
	uint16_t flags = 0;
	uint32_t vtag = 0;
	int error, i, first = sc->txq.cur;

	map = sc->txq.data[first].map;

	error = bus_dmamap_load_mbuf(sc->sc_dmat, map, m0, BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: can't map mbuf (error %d)\n",
		    sc->sc_dev.dv_xname, error);
		return error;
	}

	if (sc->txq.queued + map->dm_nsegs >= NFE_TX_RING_COUNT - 1) {
		bus_dmamap_unload(sc->sc_dmat, map);
		return ENOBUFS;
	}

#if NVLAN > 0
	/* setup h/w VLAN tagging */
	if (m0->m_flags & M_VLANTAG)
		vtag = NFE_TX_VTAG | m0->m_pkthdr.ether_vtag;
#endif
	if (m0->m_pkthdr.csum_flags & M_IPV4_CSUM_OUT)
		flags |= NFE_TX_IP_CSUM;
	if (m0->m_pkthdr.csum_flags & (M_TCP_CSUM_OUT | M_UDP_CSUM_OUT))
		flags |= NFE_TX_TCP_UDP_CSUM;

	for (i = 0; i < map->dm_nsegs; i++) {
		data = &sc->txq.data[sc->txq.cur];

		if (sc->sc_flags & NFE_40BIT_ADDR) {
			desc64 = &sc->txq.desc64[sc->txq.cur];
#if defined(__LP64__)
			desc64->physaddr[0] =
			    htole32(map->dm_segs[i].ds_addr >> 32);
#endif
			desc64->physaddr[1] =
			    htole32(map->dm_segs[i].ds_addr & 0xffffffff);
			desc64->length = htole16(map->dm_segs[i].ds_len - 1);
			desc64->flags = htole16(flags);
			desc64->vtag = htole32(vtag);
		} else {
			desc32 = &sc->txq.desc32[sc->txq.cur];

			desc32->physaddr = htole32(map->dm_segs[i].ds_addr);
			desc32->length = htole16(map->dm_segs[i].ds_len - 1);
			desc32->flags = htole16(flags);
		}

		if (map->dm_nsegs > 1) {
			/*
			 * Checksum flags and vtag belong to the first fragment
			 * only.
			 */
			flags &= ~(NFE_TX_IP_CSUM | NFE_TX_TCP_UDP_CSUM);
			vtag = 0;

			/*
			 * Setting of the valid bit in the first descriptor is
			 * deferred until the whole chain is fully setup.
			 */
			flags |= NFE_TX_VALID;
		}

		sc->txq.queued++;
		sc->txq.cur = (sc->txq.cur + 1) % NFE_TX_RING_COUNT;
	}

	/* the whole mbuf chain has been setup */
	if (sc->sc_flags & NFE_40BIT_ADDR) {
		/* fix last descriptor */
		flags |= NFE_TX_LASTFRAG_V2;
		desc64->flags = htole16(flags);

		/* finally, set the valid bit in the first descriptor */
		sc->txq.desc64[first].flags |= htole16(NFE_TX_VALID);
	} else {
		/* fix last descriptor */
		if (sc->sc_flags & NFE_JUMBO_SUP)
			flags |= NFE_TX_LASTFRAG_V2;
		else
			flags |= NFE_TX_LASTFRAG_V1;
		desc32->flags = htole16(flags);

		/* finally, set the valid bit in the first descriptor */
		sc->txq.desc32[first].flags |= htole16(NFE_TX_VALID);
	}

	data->m = m0;
	data->active = map;

	bus_dmamap_sync(sc->sc_dmat, map, 0, map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);

	return 0;
}

void
nfe_start(struct ifnet *ifp)
{
	struct nfe_softc *sc = ifp->if_softc;
	int old = sc->txq.cur;
	struct mbuf *m0;

	if (!(ifp->if_flags & IFF_RUNNING) || ifq_is_oactive(&ifp->if_snd))
		return;

	for (;;) {
		m0 = ifq_deq_begin(&ifp->if_snd);
		if (m0 == NULL)
			break;

		if (nfe_encap(sc, m0) != 0) {
			ifq_deq_rollback(&ifp->if_snd, m0);
			ifq_set_oactive(&ifp->if_snd);
			break;
		}

		/* packet put in h/w queue, remove from s/w queue */
		ifq_deq_commit(&ifp->if_snd, m0);

#if NBPFILTER > 0
		if (ifp->if_bpf != NULL)
			bpf_mtap_ether(ifp->if_bpf, m0, BPF_DIRECTION_OUT);
#endif
	}
	if (sc->txq.cur == old)	/* nothing sent */
		return;

	if (sc->sc_flags & NFE_40BIT_ADDR)
		nfe_txdesc64_rsync(sc, old, sc->txq.cur, BUS_DMASYNC_PREWRITE);
	else
		nfe_txdesc32_rsync(sc, old, sc->txq.cur, BUS_DMASYNC_PREWRITE);

	/* kick Tx */
	NFE_WRITE(sc, NFE_RXTX_CTL, NFE_RXTX_KICKTX | sc->rxtxctl);

	/*
	 * Set a timeout in case the chip goes out to lunch.
	 */
	ifp->if_timer = 5;
}

void
nfe_watchdog(struct ifnet *ifp)
{
	struct nfe_softc *sc = ifp->if_softc;

	printf("%s: watchdog timeout\n", sc->sc_dev.dv_xname);

	nfe_init(ifp);

	ifp->if_oerrors++;
}

int
nfe_init(struct ifnet *ifp)
{
	struct nfe_softc *sc = ifp->if_softc;
	uint32_t tmp;

	nfe_stop(ifp, 0);

	NFE_WRITE(sc, NFE_TX_UNK, 0);
	NFE_WRITE(sc, NFE_STATUS, 0);

	sc->rxtxctl = NFE_RXTX_BIT2;
	if (sc->sc_flags & NFE_40BIT_ADDR)
		sc->rxtxctl |= NFE_RXTX_V3MAGIC;
	else if (sc->sc_flags & NFE_JUMBO_SUP)
		sc->rxtxctl |= NFE_RXTX_V2MAGIC;

	if (sc->sc_flags & NFE_HW_CSUM)
		sc->rxtxctl |= NFE_RXTX_RXCSUM;
	if (ifp->if_capabilities & IFCAP_VLAN_HWTAGGING)
		sc->rxtxctl |= NFE_RXTX_VTAG_INSERT | NFE_RXTX_VTAG_STRIP;

	NFE_WRITE(sc, NFE_RXTX_CTL, NFE_RXTX_RESET | sc->rxtxctl);
	DELAY(10);
	NFE_WRITE(sc, NFE_RXTX_CTL, sc->rxtxctl);

	if (ifp->if_capabilities & IFCAP_VLAN_HWTAGGING)
		NFE_WRITE(sc, NFE_VTAG_CTL, NFE_VTAG_ENABLE);
	else
		NFE_WRITE(sc, NFE_VTAG_CTL, 0);

	NFE_WRITE(sc, NFE_SETUP_R6, 0);

	/* set MAC address */
	nfe_set_macaddr(sc, sc->sc_arpcom.ac_enaddr);

	/* tell MAC where rings are in memory */
#ifdef __LP64__
	NFE_WRITE(sc, NFE_RX_RING_ADDR_HI, sc->rxq.physaddr >> 32);
#endif
	NFE_WRITE(sc, NFE_RX_RING_ADDR_LO, sc->rxq.physaddr & 0xffffffff);
#ifdef __LP64__
	NFE_WRITE(sc, NFE_TX_RING_ADDR_HI, sc->txq.physaddr >> 32);
#endif
	NFE_WRITE(sc, NFE_TX_RING_ADDR_LO, sc->txq.physaddr & 0xffffffff);

	NFE_WRITE(sc, NFE_RING_SIZE,
	    (NFE_RX_RING_COUNT - 1) << 16 |
	    (NFE_TX_RING_COUNT - 1));

	NFE_WRITE(sc, NFE_RXBUFSZ, sc->rxq.bufsz);

	/* force MAC to wakeup */
	tmp = NFE_READ(sc, NFE_PWR_STATE);
	NFE_WRITE(sc, NFE_PWR_STATE, tmp | NFE_PWR_WAKEUP);
	DELAY(10);
	tmp = NFE_READ(sc, NFE_PWR_STATE);
	NFE_WRITE(sc, NFE_PWR_STATE, tmp | NFE_PWR_VALID);

#if 1
	/* configure interrupts coalescing/mitigation */
	NFE_WRITE(sc, NFE_IMTIMER, NFE_IM_DEFAULT);
#else
	/* no interrupt mitigation: one interrupt per packet */
	NFE_WRITE(sc, NFE_IMTIMER, 970);
#endif

	NFE_WRITE(sc, NFE_SETUP_R1, NFE_R1_MAGIC);
	NFE_WRITE(sc, NFE_SETUP_R2, NFE_R2_MAGIC);
	NFE_WRITE(sc, NFE_SETUP_R6, NFE_R6_MAGIC);

	/* update MAC knowledge of PHY; generates a NFE_IRQ_LINK interrupt */
	NFE_WRITE(sc, NFE_STATUS, sc->mii_phyaddr << 24 | NFE_STATUS_MAGIC);

	NFE_WRITE(sc, NFE_SETUP_R4, NFE_R4_MAGIC);

	sc->rxtxctl &= ~NFE_RXTX_BIT2;
	NFE_WRITE(sc, NFE_RXTX_CTL, sc->rxtxctl);
	DELAY(10);
	NFE_WRITE(sc, NFE_RXTX_CTL, NFE_RXTX_BIT1 | sc->rxtxctl);

	/* program promiscuous mode and multicast filters */
	nfe_iff(sc);

	nfe_ifmedia_upd(ifp);

	/* enable Rx */
	NFE_WRITE(sc, NFE_RX_CTL, NFE_RX_START);

	/* enable Tx */
	NFE_WRITE(sc, NFE_TX_CTL, NFE_TX_START);

	NFE_WRITE(sc, NFE_PHY_STATUS, 0xf);

	/* enable interrupts */
	NFE_WRITE(sc, NFE_IRQ_MASK, NFE_IRQ_WANTED);

	timeout_add_sec(&sc->sc_tick_ch, 1);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	return 0;
}

void
nfe_stop(struct ifnet *ifp, int disable)
{
	struct nfe_softc *sc = ifp->if_softc;

	timeout_del(&sc->sc_tick_ch);

	ifp->if_timer = 0;
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	mii_down(&sc->sc_mii);

	/* abort Tx */
	NFE_WRITE(sc, NFE_TX_CTL, 0);

	if ((sc->sc_flags & NFE_WOL) == 0) {
		/* disable Rx */
		NFE_WRITE(sc, NFE_RX_CTL, 0);

		/* disable interrupts */
		NFE_WRITE(sc, NFE_IRQ_MASK, 0);
	}

	/* reset Tx and Rx rings */
	nfe_reset_tx_ring(sc, &sc->txq);
	nfe_reset_rx_ring(sc, &sc->rxq);
}

int
nfe_alloc_rx_ring(struct nfe_softc *sc, struct nfe_rx_ring *ring)
{
	struct nfe_desc32 *desc32;
	struct nfe_desc64 *desc64;
	struct nfe_rx_data *data;
	void **desc;
	bus_addr_t physaddr;
	int i, nsegs, error, descsize;

	if (sc->sc_flags & NFE_40BIT_ADDR) {
		desc = (void **)&ring->desc64;
		descsize = sizeof (struct nfe_desc64);
	} else {
		desc = (void **)&ring->desc32;
		descsize = sizeof (struct nfe_desc32);
	}

	ring->cur = ring->next = 0;
	ring->bufsz = MCLBYTES;

	error = bus_dmamap_create(sc->sc_dmat, NFE_RX_RING_COUNT * descsize, 1,
	    NFE_RX_RING_COUNT * descsize, 0, BUS_DMA_NOWAIT, &ring->map);
	if (error != 0) {
		printf("%s: could not create desc DMA map\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamem_alloc(sc->sc_dmat, NFE_RX_RING_COUNT * descsize,
	    PAGE_SIZE, 0, &ring->seg, 1, &nsegs, BUS_DMA_NOWAIT | BUS_DMA_ZERO);
	if (error != 0) {
		printf("%s: could not allocate DMA memory\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamem_map(sc->sc_dmat, &ring->seg, nsegs,
	    NFE_RX_RING_COUNT * descsize, (caddr_t *)desc, BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: can't map desc DMA memory\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamap_load(sc->sc_dmat, ring->map, *desc,
	    NFE_RX_RING_COUNT * descsize, NULL, BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: could not load desc DMA map\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}
	ring->physaddr = ring->map->dm_segs[0].ds_addr;

	/*
	 * Pre-allocate Rx buffers and populate Rx ring.
	 */
	for (i = 0; i < NFE_RX_RING_COUNT; i++) {
		data = &sc->rxq.data[i];

		data->m = MCLGETL(NULL, M_DONTWAIT, MCLBYTES);
		if (data->m == NULL) {
			printf("%s: could not allocate rx mbuf\n",
			    sc->sc_dev.dv_xname);
			error = ENOMEM;
			goto fail;
		}
		data->m->m_pkthdr.len = data->m->m_len = MCLBYTES;

		error = bus_dmamap_create(sc->sc_dmat, MCLBYTES, 1,
		    MCLBYTES, 0, BUS_DMA_NOWAIT, &data->map);
		if (error != 0) {
			printf("%s: could not create DMA map\n",
			    sc->sc_dev.dv_xname);
			goto fail;
		}

		error = bus_dmamap_load_mbuf(sc->sc_dmat, data->map, data->m,
		    BUS_DMA_READ | BUS_DMA_NOWAIT);
		if (error != 0) {
			printf("%s: could not load rx buf DMA map",
			    sc->sc_dev.dv_xname);
			goto fail;
		}
		physaddr = data->map->dm_segs[0].ds_addr;

		if (sc->sc_flags & NFE_40BIT_ADDR) {
			desc64 = &sc->rxq.desc64[i];
#if defined(__LP64__)
			desc64->physaddr[0] = htole32(physaddr >> 32);
#endif
			desc64->physaddr[1] = htole32(physaddr & 0xffffffff);
			desc64->length = htole16(sc->rxq.bufsz);
			desc64->flags = htole16(NFE_RX_READY);
		} else {
			desc32 = &sc->rxq.desc32[i];
			desc32->physaddr = htole32(physaddr);
			desc32->length = htole16(sc->rxq.bufsz);
			desc32->flags = htole16(NFE_RX_READY);
		}
	}

	bus_dmamap_sync(sc->sc_dmat, ring->map, 0, ring->map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);

	return 0;

fail:	nfe_free_rx_ring(sc, ring);
	return error;
}

void
nfe_reset_rx_ring(struct nfe_softc *sc, struct nfe_rx_ring *ring)
{
	int i;

	for (i = 0; i < NFE_RX_RING_COUNT; i++) {
		if (sc->sc_flags & NFE_40BIT_ADDR) {
			ring->desc64[i].length = htole16(ring->bufsz);
			ring->desc64[i].flags = htole16(NFE_RX_READY);
		} else {
			ring->desc32[i].length = htole16(ring->bufsz);
			ring->desc32[i].flags = htole16(NFE_RX_READY);
		}
	}

	bus_dmamap_sync(sc->sc_dmat, ring->map, 0, ring->map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);

	ring->cur = ring->next = 0;
}

void
nfe_free_rx_ring(struct nfe_softc *sc, struct nfe_rx_ring *ring)
{
	struct nfe_rx_data *data;
	void *desc;
	int i, descsize;

	if (sc->sc_flags & NFE_40BIT_ADDR) {
		desc = ring->desc64;
		descsize = sizeof (struct nfe_desc64);
	} else {
		desc = ring->desc32;
		descsize = sizeof (struct nfe_desc32);
	}

	if (desc != NULL) {
		bus_dmamap_sync(sc->sc_dmat, ring->map, 0,
		    ring->map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, ring->map);
		bus_dmamem_unmap(sc->sc_dmat, (caddr_t)desc,
		    NFE_RX_RING_COUNT * descsize);
		bus_dmamem_free(sc->sc_dmat, &ring->seg, 1);
	}

	for (i = 0; i < NFE_RX_RING_COUNT; i++) {
		data = &ring->data[i];

		if (data->map != NULL) {
			bus_dmamap_sync(sc->sc_dmat, data->map, 0,
			    data->map->dm_mapsize, BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(sc->sc_dmat, data->map);
			bus_dmamap_destroy(sc->sc_dmat, data->map);
		}
		m_freem(data->m);
	}
}

int
nfe_alloc_tx_ring(struct nfe_softc *sc, struct nfe_tx_ring *ring)
{
	int i, nsegs, error;
	void **desc;
	int descsize;

	if (sc->sc_flags & NFE_40BIT_ADDR) {
		desc = (void **)&ring->desc64;
		descsize = sizeof (struct nfe_desc64);
	} else {
		desc = (void **)&ring->desc32;
		descsize = sizeof (struct nfe_desc32);
	}

	ring->queued = 0;
	ring->cur = ring->next = 0;

	error = bus_dmamap_create(sc->sc_dmat, NFE_TX_RING_COUNT * descsize, 1,
	    NFE_TX_RING_COUNT * descsize, 0, BUS_DMA_NOWAIT, &ring->map);

	if (error != 0) {
		printf("%s: could not create desc DMA map\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamem_alloc(sc->sc_dmat, NFE_TX_RING_COUNT * descsize,
	    PAGE_SIZE, 0, &ring->seg, 1, &nsegs, BUS_DMA_NOWAIT | BUS_DMA_ZERO);
	if (error != 0) {
		printf("%s: could not allocate DMA memory\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamem_map(sc->sc_dmat, &ring->seg, nsegs,
	    NFE_TX_RING_COUNT * descsize, (caddr_t *)desc, BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: can't map desc DMA memory\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamap_load(sc->sc_dmat, ring->map, *desc,
	    NFE_TX_RING_COUNT * descsize, NULL, BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: could not load desc DMA map\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}
	ring->physaddr = ring->map->dm_segs[0].ds_addr;

	for (i = 0; i < NFE_TX_RING_COUNT; i++) {
		error = bus_dmamap_create(sc->sc_dmat, NFE_JBYTES,
		    NFE_MAX_SCATTER, NFE_JBYTES, 0, BUS_DMA_NOWAIT,
		    &ring->data[i].map);
		if (error != 0) {
			printf("%s: could not create DMA map\n",
			    sc->sc_dev.dv_xname);
			goto fail;
		}
	}

	return 0;

fail:	nfe_free_tx_ring(sc, ring);
	return error;
}

void
nfe_reset_tx_ring(struct nfe_softc *sc, struct nfe_tx_ring *ring)
{
	struct nfe_tx_data *data;
	int i;

	for (i = 0; i < NFE_TX_RING_COUNT; i++) {
		if (sc->sc_flags & NFE_40BIT_ADDR)
			ring->desc64[i].flags = 0;
		else
			ring->desc32[i].flags = 0;

		data = &ring->data[i];

		if (data->m != NULL) {
			bus_dmamap_sync(sc->sc_dmat, data->active, 0,
			    data->active->dm_mapsize, BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_dmat, data->active);
			m_freem(data->m);
			data->m = NULL;
		}
	}

	bus_dmamap_sync(sc->sc_dmat, ring->map, 0, ring->map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);

	ring->queued = 0;
	ring->cur = ring->next = 0;
}

void
nfe_free_tx_ring(struct nfe_softc *sc, struct nfe_tx_ring *ring)
{
	struct nfe_tx_data *data;
	void *desc;
	int i, descsize;

	if (sc->sc_flags & NFE_40BIT_ADDR) {
		desc = ring->desc64;
		descsize = sizeof (struct nfe_desc64);
	} else {
		desc = ring->desc32;
		descsize = sizeof (struct nfe_desc32);
	}

	if (desc != NULL) {
		bus_dmamap_sync(sc->sc_dmat, ring->map, 0,
		    ring->map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, ring->map);
		bus_dmamem_unmap(sc->sc_dmat, (caddr_t)desc,
		    NFE_TX_RING_COUNT * descsize);
		bus_dmamem_free(sc->sc_dmat, &ring->seg, 1);
	}

	for (i = 0; i < NFE_TX_RING_COUNT; i++) {
		data = &ring->data[i];

		if (data->m != NULL) {
			bus_dmamap_sync(sc->sc_dmat, data->active, 0,
			    data->active->dm_mapsize, BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_dmat, data->active);
			m_freem(data->m);
		}
	}

	/* ..and now actually destroy the DMA mappings */
	for (i = 0; i < NFE_TX_RING_COUNT; i++) {
		data = &ring->data[i];
		if (data->map == NULL)
			continue;
		bus_dmamap_destroy(sc->sc_dmat, data->map);
	}
}

int
nfe_ifmedia_upd(struct ifnet *ifp)
{
	struct nfe_softc *sc = ifp->if_softc;
	struct mii_data *mii = &sc->sc_mii;
	struct mii_softc *miisc;

	if (mii->mii_instance != 0) {
		LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
			mii_phy_reset(miisc);
	}
	return mii_mediachg(mii);
}

void
nfe_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct nfe_softc *sc = ifp->if_softc;
	struct mii_data *mii = &sc->sc_mii;

	mii_pollstat(mii);
	ifmr->ifm_status = mii->mii_media_status;
	ifmr->ifm_active = mii->mii_media_active;
}

void
nfe_iff(struct nfe_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct arpcom *ac = &sc->sc_arpcom;
	struct ether_multi *enm;
	struct ether_multistep step;
	uint8_t addr[ETHER_ADDR_LEN], mask[ETHER_ADDR_LEN];
	uint32_t filter;
	int i;

	filter = NFE_RXFILTER_MAGIC;
	ifp->if_flags &= ~IFF_ALLMULTI;

	if (ifp->if_flags & IFF_PROMISC || ac->ac_multirangecnt > 0) {
		ifp->if_flags |= IFF_ALLMULTI;
		if (ifp->if_flags & IFF_PROMISC)
			filter |= NFE_PROMISC;
		else
			filter |= NFE_U2M;
		bzero(addr, ETHER_ADDR_LEN);
		bzero(mask, ETHER_ADDR_LEN);
	} else {
		filter |= NFE_U2M;

		bcopy(etherbroadcastaddr, addr, ETHER_ADDR_LEN);
		bcopy(etherbroadcastaddr, mask, ETHER_ADDR_LEN);

		ETHER_FIRST_MULTI(step, ac, enm);
		while (enm != NULL) {
			for (i = 0; i < ETHER_ADDR_LEN; i++) {
				addr[i] &=  enm->enm_addrlo[i];
				mask[i] &= ~enm->enm_addrlo[i];
			}

			ETHER_NEXT_MULTI(step, enm);
		}

		for (i = 0; i < ETHER_ADDR_LEN; i++)
			mask[i] |= addr[i];
	}

	addr[0] |= 0x01;	/* make sure multicast bit is set */

	NFE_WRITE(sc, NFE_MULTIADDR_HI,
	    addr[3] << 24 | addr[2] << 16 | addr[1] << 8 | addr[0]);
	NFE_WRITE(sc, NFE_MULTIADDR_LO,
	    addr[5] <<  8 | addr[4]);
	NFE_WRITE(sc, NFE_MULTIMASK_HI,
	    mask[3] << 24 | mask[2] << 16 | mask[1] << 8 | mask[0]);
	NFE_WRITE(sc, NFE_MULTIMASK_LO,
	    mask[5] <<  8 | mask[4]);
	NFE_WRITE(sc, NFE_RXFILTER, filter);
}

void
nfe_get_macaddr(struct nfe_softc *sc, uint8_t *addr)
{
	uint32_t tmp;

	if (sc->sc_flags & NFE_CORRECT_MACADDR) {
		tmp = NFE_READ(sc, NFE_MACADDR_HI);
		addr[0] = (tmp & 0xff);
		addr[1] = (tmp >>  8) & 0xff;
		addr[2] = (tmp >> 16) & 0xff;
		addr[3] = (tmp >> 24) & 0xff;

		tmp = NFE_READ(sc, NFE_MACADDR_LO);
		addr[4] = (tmp & 0xff);
		addr[5] = (tmp >> 8) & 0xff;

	} else {
		tmp = NFE_READ(sc, NFE_MACADDR_LO);
		addr[0] = (tmp >> 8) & 0xff;
		addr[1] = (tmp & 0xff);

		tmp = NFE_READ(sc, NFE_MACADDR_HI);
		addr[2] = (tmp >> 24) & 0xff;
		addr[3] = (tmp >> 16) & 0xff;
		addr[4] = (tmp >>  8) & 0xff;
		addr[5] = (tmp & 0xff);
	}
}

void
nfe_set_macaddr(struct nfe_softc *sc, const uint8_t *addr)
{
	NFE_WRITE(sc, NFE_MACADDR_LO,
	    addr[5] <<  8 | addr[4]);
	NFE_WRITE(sc, NFE_MACADDR_HI,
	    addr[3] << 24 | addr[2] << 16 | addr[1] << 8 | addr[0]);
}

void
nfe_tick(void *arg)
{
	struct nfe_softc *sc = arg;
	int s;

	s = splnet();
	mii_tick(&sc->sc_mii);
	splx(s);

	timeout_add_sec(&sc->sc_tick_ch, 1);
}

#ifndef SMALL_KERNEL
int
nfe_wol(struct ifnet *ifp, int enable)
{
	struct nfe_softc *sc = ifp->if_softc;

	if (enable) {
		sc->sc_flags |= NFE_WOL;
		NFE_WRITE(sc, NFE_WOL_CTL, NFE_WOL_ENABLE);
	} else {
		sc->sc_flags &= ~NFE_WOL;
		NFE_WRITE(sc, NFE_WOL_CTL, 0);
	}

	return 0;
}
#endif
