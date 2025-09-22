/*	$OpenBSD: if_lge.c,v 1.82 2024/09/06 10:54:08 jsg Exp $	*/
/*
 * Copyright (c) 2001 Wind River Systems
 * Copyright (c) 1997, 1998, 1999, 2000, 2001
 *	Bill Paul <william.paul@windriver.com>.  All rights reserved.
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
 * $FreeBSD: src/sys/dev/lge/if_lge.c,v 1.6 2001/06/20 19:47:55 bmilekic Exp $
 */

/*
 * Level 1 LXT1001 gigabit ethernet driver for FreeBSD. Public
 * documentation not available, but ask me nicely.
 *
 * Written by Bill Paul <william.paul@windriver.com>
 * Wind River Systems
 */

/*
 * The Level 1 chip is used on some D-Link, SMC and Addtron NICs.
 * It's a 64-bit PCI part that supports TCP/IP checksum offload,
 * VLAN tagging/insertion, GMII and TBI (1000baseX) ports. There
 * are three supported methods for data transfer between host and
 * NIC: programmed I/O, traditional scatter/gather DMA and Packet
 * Propulsion Technology (tm) DMA. The latter mechanism is a form
 * of double buffer DMA where the packet data is copied to a
 * pre-allocated DMA buffer who's physical address has been loaded
 * into a table at device initialization time. The rationale is that
 * the virtual to physical address translation needed for normal
 * scatter/gather DMA is more expensive than the data copy needed
 * for double buffering. This may be true in Windows NT and the like,
 * but it isn't true for us, at least on the x86 arch. This driver
 * uses the scatter/gather I/O method for both TX and RX.
 *
 * The LXT1001 only supports TCP/IP checksum offload on receive.
 * Also, the VLAN tagging is done using a 16-entry table which allows
 * the chip to perform hardware filtering based on VLAN tags. Sadly,
 * our vlan support doesn't currently play well with this kind of
 * hardware support.
 *
 * Special thanks to:
 * - Jeff James at Intel, for arranging to have the LXT1001 manual
 *   released (at long last)
 * - Beny Chen at D-Link, for actually sending it to me
 * - Brad Short and Keith Alexis at SMC, for sending me sample
 *   SMC9462SX and SMC9462TX adapters for testing
 * - Paul Saab at Y!, for not killing me (though it remains to be seen
 *   if in fact he did me much of a favor)
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/device.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <uvm/uvm_extern.h>              /* for vtophys */
#define	VTOPHYS(v)	vtophys((vaddr_t)(v))

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/mii/miivar.h>

#define LGE_USEIOSPACE

#include <dev/pci/if_lgereg.h>

int lge_probe(struct device *, void *, void *);
void lge_attach(struct device *, struct device *, void *);

const struct cfattach lge_ca = {
	sizeof(struct lge_softc), lge_probe, lge_attach
};

struct cfdriver lge_cd = {
	NULL, "lge", DV_IFNET
};

int lge_newbuf(struct lge_softc *, struct lge_rx_desc *,
			     struct mbuf *);
int lge_encap(struct lge_softc *, struct mbuf *, u_int32_t *);
void lge_rxeof(struct lge_softc *, int);
void lge_txeof(struct lge_softc *);
int lge_intr(void *);
void lge_tick(void *);
void lge_start(struct ifnet *);
int lge_ioctl(struct ifnet *, u_long, caddr_t);
void lge_init(void *);
void lge_stop(struct lge_softc *);
void lge_watchdog(struct ifnet *);
int lge_ifmedia_upd(struct ifnet *);
void lge_ifmedia_sts(struct ifnet *, struct ifmediareq *);

void lge_eeprom_getword(struct lge_softc *, int, u_int16_t *);
void lge_read_eeprom(struct lge_softc *, caddr_t, int, int, int);

int lge_miibus_readreg(struct device *, int, int);
void lge_miibus_writereg(struct device *, int, int, int);
void lge_miibus_statchg(struct device *);

void lge_setmulti(struct lge_softc *);
void lge_reset(struct lge_softc *);
int lge_list_rx_init(struct lge_softc *);
int lge_list_tx_init(struct lge_softc *);

#ifdef LGE_DEBUG
#define DPRINTF(x)	if (lgedebug) printf x
#define DPRINTFN(n,x)	if (lgedebug >= (n)) printf x
int	lgedebug = 0;
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

const struct pci_matchid lge_devices[] = {
	{ PCI_VENDOR_LEVEL1, PCI_PRODUCT_LEVEL1_LXT1001 }
};

#define LGE_SETBIT(sc, reg, x)				\
	CSR_WRITE_4(sc, reg,				\
		CSR_READ_4(sc, reg) | (x))

#define LGE_CLRBIT(sc, reg, x)				\
	CSR_WRITE_4(sc, reg,				\
		CSR_READ_4(sc, reg) & ~(x))

#define SIO_SET(x)					\
	CSR_WRITE_4(sc, LGE_MEAR, CSR_READ_4(sc, LGE_MEAR) | x)

#define SIO_CLR(x)					\
	CSR_WRITE_4(sc, LGE_MEAR, CSR_READ_4(sc, LGE_MEAR) & ~x)

/*
 * Read a word of data stored in the EEPROM at address 'addr.'
 */
void
lge_eeprom_getword(struct lge_softc *sc, int addr, u_int16_t *dest)
{
	int			i;
	u_int32_t		val;

	CSR_WRITE_4(sc, LGE_EECTL, LGE_EECTL_CMD_READ|
	    LGE_EECTL_SINGLEACCESS|((addr >> 1) << 8));

	for (i = 0; i < LGE_TIMEOUT; i++)
		if (!(CSR_READ_4(sc, LGE_EECTL) & LGE_EECTL_CMD_READ))
			break;

	if (i == LGE_TIMEOUT) {
		printf("%s: EEPROM read timed out\n", sc->sc_dv.dv_xname);
		return;
	}

	val = CSR_READ_4(sc, LGE_EEDATA);

	if (addr & 1)
		*dest = (val >> 16) & 0xFFFF;
	else
		*dest = val & 0xFFFF;
}

/*
 * Read a sequence of words from the EEPROM.
 */
void
lge_read_eeprom(struct lge_softc *sc, caddr_t dest, int off,
    int cnt, int swap)
{
	int			i;
	u_int16_t		word = 0, *ptr;

	for (i = 0; i < cnt; i++) {
		lge_eeprom_getword(sc, off + i, &word);
		ptr = (u_int16_t *)(dest + (i * 2));
		if (swap)
			*ptr = ntohs(word);
		else
			*ptr = word;
	}
}

int
lge_miibus_readreg(struct device *dev, int phy, int reg)
{
	struct lge_softc	*sc = (struct lge_softc *)dev;
	int			i;

	/*
	 * If we have a non-PCS PHY, pretend that the internal
	 * autoneg stuff at PHY address 0 isn't there so that
	 * the miibus code will find only the GMII PHY.
	 */
	if (sc->lge_pcs == 0 && phy == 0)
		return (0);

	CSR_WRITE_4(sc, LGE_GMIICTL, (phy << 8) | reg | LGE_GMIICMD_READ);

	for (i = 0; i < LGE_TIMEOUT; i++)
		if (!(CSR_READ_4(sc, LGE_GMIICTL) & LGE_GMIICTL_CMDBUSY))
			break;

	if (i == LGE_TIMEOUT) {
		printf("%s: PHY read timed out\n", sc->sc_dv.dv_xname);
		return (0);
	}

	return (CSR_READ_4(sc, LGE_GMIICTL) >> 16);
}

void
lge_miibus_writereg(struct device *dev, int phy, int reg, int data)
{
	struct lge_softc	*sc = (struct lge_softc *)dev;
	int			i;

	CSR_WRITE_4(sc, LGE_GMIICTL,
	    (data << 16) | (phy << 8) | reg | LGE_GMIICMD_WRITE);

	for (i = 0; i < LGE_TIMEOUT; i++)
		if (!(CSR_READ_4(sc, LGE_GMIICTL) & LGE_GMIICTL_CMDBUSY))
			break;

	if (i == LGE_TIMEOUT) {
		printf("%s: PHY write timed out\n", sc->sc_dv.dv_xname);
	}
}

void
lge_miibus_statchg(struct device *dev)
{
	struct lge_softc	*sc = (struct lge_softc *)dev;
	struct mii_data		*mii = &sc->lge_mii;

	LGE_CLRBIT(sc, LGE_GMIIMODE, LGE_GMIIMODE_SPEED);
	switch (IFM_SUBTYPE(mii->mii_media_active)) {
	case IFM_1000_T:
	case IFM_1000_SX:
		LGE_SETBIT(sc, LGE_GMIIMODE, LGE_SPEED_1000);
		break;
	case IFM_100_TX:
		LGE_SETBIT(sc, LGE_GMIIMODE, LGE_SPEED_100);
		break;
	case IFM_10_T:
		LGE_SETBIT(sc, LGE_GMIIMODE, LGE_SPEED_10);
		break;
	default:
		/*
		 * Choose something, even if it's wrong. Clearing
		 * all the bits will hose autoneg on the internal
		 * PHY.
		 */
		LGE_SETBIT(sc, LGE_GMIIMODE, LGE_SPEED_1000);
		break;
	}

	if ((mii->mii_media_active & IFM_GMASK) == IFM_FDX) {
		LGE_SETBIT(sc, LGE_GMIIMODE, LGE_GMIIMODE_FDX);
	} else {
		LGE_CLRBIT(sc, LGE_GMIIMODE, LGE_GMIIMODE_FDX);
	}
}

void
lge_setmulti(struct lge_softc *sc)
{
	struct arpcom		*ac = &sc->arpcom;
	struct ifnet		*ifp = &ac->ac_if;
	struct ether_multi      *enm;
	struct ether_multistep  step;
	u_int32_t		h = 0, hashes[2] = { 0, 0 };

	/* Make sure multicast hash table is enabled. */
	CSR_WRITE_4(sc, LGE_MODE1, LGE_MODE1_SETRST_CTL1|LGE_MODE1_RX_MCAST);

	if (ac->ac_multirangecnt > 0)
		ifp->if_flags |= IFF_ALLMULTI;

	if (ifp->if_flags & IFF_ALLMULTI || ifp->if_flags & IFF_PROMISC) {
		CSR_WRITE_4(sc, LGE_MAR0, 0xFFFFFFFF);
		CSR_WRITE_4(sc, LGE_MAR1, 0xFFFFFFFF);
		return;
	}

	/* first, zot all the existing hash bits */
	CSR_WRITE_4(sc, LGE_MAR0, 0);
	CSR_WRITE_4(sc, LGE_MAR1, 0);

	/* now program new ones */
	ETHER_FIRST_MULTI(step, ac, enm);
	while (enm != NULL) {
		h = (ether_crc32_be(enm->enm_addrlo, ETHER_ADDR_LEN) >> 26) &
		    0x0000003F;
		if (h < 32)
			hashes[0] |= (1 << h);
		else
			hashes[1] |= (1 << (h - 32));
		ETHER_NEXT_MULTI(step, enm);
	}

	CSR_WRITE_4(sc, LGE_MAR0, hashes[0]);
	CSR_WRITE_4(sc, LGE_MAR1, hashes[1]);
}

void
lge_reset(struct lge_softc *sc)
{
	int			i;

	LGE_SETBIT(sc, LGE_MODE1, LGE_MODE1_SETRST_CTL0|LGE_MODE1_SOFTRST);

	for (i = 0; i < LGE_TIMEOUT; i++) {
		if (!(CSR_READ_4(sc, LGE_MODE1) & LGE_MODE1_SOFTRST))
			break;
	}

	if (i == LGE_TIMEOUT)
		printf("%s: reset never completed\n", sc->sc_dv.dv_xname);

	/* Wait a little while for the chip to get its brains in order. */
	DELAY(1000);
}

/*
 * Probe for a Level 1 chip. Check the PCI vendor and device
 * IDs against our list and return a device name if we find a match.
 */
int
lge_probe(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid((struct pci_attach_args *)aux, lge_devices,
	    nitems(lge_devices)));
}

/*
 * Attach the interface. Allocate softc structures, do ifmedia
 * setup and ethernet/BPF attach.
 */
void
lge_attach(struct device *parent, struct device *self, void *aux)
{
	struct lge_softc	*sc = (struct lge_softc *)self;
	struct pci_attach_args	*pa = aux;
	pci_chipset_tag_t	pc = pa->pa_pc;
	pci_intr_handle_t	ih;
	const char		*intrstr = NULL;
	bus_size_t		size;
	bus_dma_segment_t	seg;
	bus_dmamap_t		dmamap;
	int			rseg;
	u_char			eaddr[ETHER_ADDR_LEN];
#ifndef LGE_USEIOSPACE
	pcireg_t		memtype;
#endif
	struct ifnet		*ifp;
	caddr_t			kva;

	pci_set_powerstate(pa->pa_pc, pa->pa_tag, PCI_PMCSR_STATE_D0);

	/*
	 * Map control/status registers.
	 */
	DPRINTFN(5, ("Map control/status regs\n"));

	DPRINTFN(5, ("pci_mapreg_map\n"));
#ifdef LGE_USEIOSPACE
	if (pci_mapreg_map(pa, LGE_PCI_LOIO, PCI_MAPREG_TYPE_IO, 0,
	    &sc->lge_btag, &sc->lge_bhandle, NULL, &size, 0)) {
		printf(": can't map i/o space\n");
		return;
	}
#else
	memtype = pci_mapreg_type(pc, pa->pa_tag, LGE_PCI_LOMEM);
	if (pci_mapreg_map(pa, LGE_PCI_LOMEM, memtype, 0, &sc->lge_btag,
	    &sc->lge_bhandle, NULL, &size, 0)) {
		printf(": can't map mem space\n");
		return;
	}
#endif

	DPRINTFN(5, ("pci_intr_map\n"));
	if (pci_intr_map(pa, &ih)) {
		printf(": couldn't map interrupt\n");
		goto fail_1;
	}

	DPRINTFN(5, ("pci_intr_string\n"));
	intrstr = pci_intr_string(pc, ih);
	DPRINTFN(5, ("pci_intr_establish\n"));
	sc->lge_intrhand = pci_intr_establish(pc, ih, IPL_NET, lge_intr, sc,
					      sc->sc_dv.dv_xname);
	if (sc->lge_intrhand == NULL) {
		printf(": couldn't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		goto fail_1;
	}
	printf(": %s", intrstr);

	/* Reset the adapter. */
	DPRINTFN(5, ("lge_reset\n"));
	lge_reset(sc);

	/*
	 * Get station address from the EEPROM.
	 */
	DPRINTFN(5, ("lge_read_eeprom\n"));
	lge_read_eeprom(sc, (caddr_t)&eaddr[0], LGE_EE_NODEADDR_0, 1, 0);
	lge_read_eeprom(sc, (caddr_t)&eaddr[2], LGE_EE_NODEADDR_1, 1, 0);
	lge_read_eeprom(sc, (caddr_t)&eaddr[4], LGE_EE_NODEADDR_2, 1, 0);

	/*
	 * A Level 1 chip was detected. Inform the world.
	 */
	printf(", address %s\n", ether_sprintf(eaddr));

	bcopy(eaddr, &sc->arpcom.ac_enaddr, ETHER_ADDR_LEN);

	sc->sc_dmatag = pa->pa_dmat;
	DPRINTFN(5, ("bus_dmamem_alloc\n"));
	if (bus_dmamem_alloc(sc->sc_dmatag, sizeof(struct lge_list_data),
	    PAGE_SIZE, 0, &seg, 1, &rseg, BUS_DMA_NOWAIT | BUS_DMA_ZERO)) {
		printf("%s: can't alloc rx buffers\n", sc->sc_dv.dv_xname);
		goto fail_2;
	}
	DPRINTFN(5, ("bus_dmamem_map\n"));
	if (bus_dmamem_map(sc->sc_dmatag, &seg, rseg,
			   sizeof(struct lge_list_data), &kva,
			   BUS_DMA_NOWAIT)) {
		printf("%s: can't map dma buffers (%zd bytes)\n",
		       sc->sc_dv.dv_xname, sizeof(struct lge_list_data));
		goto fail_3;
	}
	DPRINTFN(5, ("bus_dmamap_create\n"));
	if (bus_dmamap_create(sc->sc_dmatag, sizeof(struct lge_list_data), 1,
			      sizeof(struct lge_list_data), 0,
			      BUS_DMA_NOWAIT, &dmamap)) {
		printf("%s: can't create dma map\n", sc->sc_dv.dv_xname);
		goto fail_4;
	}
	DPRINTFN(5, ("bus_dmamap_load\n"));
	if (bus_dmamap_load(sc->sc_dmatag, dmamap, kva,
			    sizeof(struct lge_list_data), NULL,
			    BUS_DMA_NOWAIT)) {
		goto fail_5;
	}

	DPRINTFN(5, ("bzero\n"));
	sc->lge_ldata = (struct lge_list_data *)kva;

	ifp = &sc->arpcom.ac_if;
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = lge_ioctl;
	ifp->if_start = lge_start;
	ifp->if_watchdog = lge_watchdog;
	ifp->if_hardmtu = LGE_JUMBO_MTU;
	ifq_init_maxlen(&ifp->if_snd, LGE_TX_LIST_CNT - 1);
	DPRINTFN(5, ("bcopy\n"));
	bcopy(sc->sc_dv.dv_xname, ifp->if_xname, IFNAMSIZ);

	ifp->if_capabilities = IFCAP_VLAN_MTU;

	if (CSR_READ_4(sc, LGE_GMIIMODE) & LGE_GMIIMODE_PCSENH)
		sc->lge_pcs = 1;
	else
		sc->lge_pcs = 0;

	/*
	 * Do MII setup.
	 */
	DPRINTFN(5, ("mii setup\n"));
	sc->lge_mii.mii_ifp = ifp;
	sc->lge_mii.mii_readreg = lge_miibus_readreg;
	sc->lge_mii.mii_writereg = lge_miibus_writereg;
	sc->lge_mii.mii_statchg = lge_miibus_statchg;
	ifmedia_init(&sc->lge_mii.mii_media, 0, lge_ifmedia_upd,
		     lge_ifmedia_sts);
	mii_attach(&sc->sc_dv, &sc->lge_mii, 0xffffffff, MII_PHY_ANY,
		   MII_OFFSET_ANY, 0);

	if (LIST_FIRST(&sc->lge_mii.mii_phys) == NULL) {
		printf("%s: no PHY found!\n", sc->sc_dv.dv_xname);
		ifmedia_add(&sc->lge_mii.mii_media, IFM_ETHER|IFM_MANUAL,
			    0, NULL);
		ifmedia_set(&sc->lge_mii.mii_media, IFM_ETHER|IFM_MANUAL);
	} else {
		DPRINTFN(5, ("ifmedia_set\n"));
		ifmedia_set(&sc->lge_mii.mii_media, IFM_ETHER|IFM_AUTO);
	}

	/*
	 * Call MI attach routine.
	 */
	DPRINTFN(5, ("if_attach\n"));
	if_attach(ifp);
	DPRINTFN(5, ("ether_ifattach\n"));
	ether_ifattach(ifp);
	DPRINTFN(5, ("timeout_set\n"));
	timeout_set(&sc->lge_timeout, lge_tick, sc);
	timeout_add_sec(&sc->lge_timeout, 1);
	return;

fail_5:
	bus_dmamap_destroy(sc->sc_dmatag, dmamap);

fail_4:
	bus_dmamem_unmap(sc->sc_dmatag, kva,
	    sizeof(struct lge_list_data));

fail_3:
	bus_dmamem_free(sc->sc_dmatag, &seg, rseg);

fail_2:
	pci_intr_disestablish(pc, sc->lge_intrhand);

fail_1:
	bus_space_unmap(sc->lge_btag, sc->lge_bhandle, size);
}

/*
 * Initialize the transmit descriptors.
 */
int
lge_list_tx_init(struct lge_softc *sc)
{
	struct lge_list_data	*ld;
	struct lge_ring_data	*cd;
	int			i;

	cd = &sc->lge_cdata;
	ld = sc->lge_ldata;
	for (i = 0; i < LGE_TX_LIST_CNT; i++) {
		ld->lge_tx_list[i].lge_mbuf = NULL;
		ld->lge_tx_list[i].lge_ctl = 0;
	}

	cd->lge_tx_prod = cd->lge_tx_cons = 0;

	return (0);
}


/*
 * Initialize the RX descriptors and allocate mbufs for them. Note that
 * we arrange the descriptors in a closed ring, so that the last descriptor
 * points back to the first.
 */
int
lge_list_rx_init(struct lge_softc *sc)
{
	struct lge_list_data	*ld;
	struct lge_ring_data	*cd;
	int			i;

	ld = sc->lge_ldata;
	cd = &sc->lge_cdata;

	cd->lge_rx_prod = cd->lge_rx_cons = 0;

	CSR_WRITE_4(sc, LGE_RXDESC_ADDR_HI, 0);

	for (i = 0; i < LGE_RX_LIST_CNT; i++) {
		if (CSR_READ_1(sc, LGE_RXCMDFREE_8BIT) == 0)
			break;
		if (lge_newbuf(sc, &ld->lge_rx_list[i], NULL) == ENOBUFS)
			return (ENOBUFS);
	}

	/* Clear possible 'rx command queue empty' interrupt. */
	CSR_READ_4(sc, LGE_ISR);

	return (0);
}

/*
 * Initialize a RX descriptor and attach a MBUF cluster.
 */
int
lge_newbuf(struct lge_softc *sc, struct lge_rx_desc *c, struct mbuf *m)
{
	struct mbuf		*m_new = NULL;

	if (m == NULL) {
		m_new = MCLGETL(NULL, M_DONTWAIT, LGE_JLEN);
		if (m_new == NULL)
			return (ENOBUFS);
	} else {
		/*
		 * We're re-using a previously allocated mbuf;
		 * be sure to re-init pointers and lengths to
		 * default values.
		 */
		m_new = m;
		m_new->m_data = m_new->m_ext.ext_buf;
	}
	m_new->m_len = m_new->m_pkthdr.len = LGE_JLEN;

	/*
	 * Adjust alignment so packet payload begins on a
	 * longword boundary. Mandatory for Alpha, useful on
	 * x86 too.
	*/
	m_adj(m_new, ETHER_ALIGN);

	c->lge_mbuf = m_new;
	c->lge_fragptr_hi = 0;
	c->lge_fragptr_lo = VTOPHYS(mtod(m_new, caddr_t));
	c->lge_fraglen = m_new->m_len;
	c->lge_ctl = m_new->m_len | LGE_RXCTL_WANTINTR | LGE_FRAGCNT(1);
	c->lge_sts = 0;

	/*
	 * Put this buffer in the RX command FIFO. To do this,
	 * we just write the physical address of the descriptor
	 * into the RX descriptor address registers. Note that
	 * there are two registers, one high DWORD and one low
	 * DWORD, which lets us specify a 64-bit address if
	 * desired. We only use a 32-bit address for now.
	 * Writing to the low DWORD register is what actually
	 * causes the command to be issued, so we do that
	 * last.
	 */
	CSR_WRITE_4(sc, LGE_RXDESC_ADDR_LO, VTOPHYS(c));
	LGE_INC(sc->lge_cdata.lge_rx_prod, LGE_RX_LIST_CNT);

	return (0);
}

/*
 * A frame has been uploaded: pass the resulting mbuf chain up to
 * the higher level protocols.
 */
void
lge_rxeof(struct lge_softc *sc, int cnt)
{
	struct mbuf_list	ml = MBUF_LIST_INITIALIZER();
        struct mbuf		*m;
        struct ifnet		*ifp;
	struct lge_rx_desc	*cur_rx;
	int			c, i, total_len = 0;
	u_int32_t		rxsts, rxctl;

	ifp = &sc->arpcom.ac_if;

	/* Find out how many frames were processed. */
	c = cnt;
	i = sc->lge_cdata.lge_rx_cons;

	/* Suck them in. */
	while(c) {
		struct mbuf		*m0 = NULL;

		cur_rx = &sc->lge_ldata->lge_rx_list[i];
		rxctl = cur_rx->lge_ctl;
		rxsts = cur_rx->lge_sts;
		m = cur_rx->lge_mbuf;
		cur_rx->lge_mbuf = NULL;
		total_len = LGE_RXBYTES(cur_rx);
		LGE_INC(i, LGE_RX_LIST_CNT);
		c--;

		/*
		 * If an error occurs, update stats, clear the
		 * status word and leave the mbuf cluster in place:
		 * it should simply get re-used next time this descriptor
	 	 * comes up in the ring.
		 */
		if (rxctl & LGE_RXCTL_ERRMASK) {
			ifp->if_ierrors++;
			lge_newbuf(sc, &LGE_RXTAIL(sc), m);
			continue;
		}

		if (lge_newbuf(sc, &LGE_RXTAIL(sc), NULL) == ENOBUFS) {
			m0 = m_devget(mtod(m, char *), total_len, ETHER_ALIGN);
			lge_newbuf(sc, &LGE_RXTAIL(sc), m);
			if (m0 == NULL) {
				ifp->if_ierrors++;
				continue;
			}
			m = m0;
		} else {
			m->m_pkthdr.len = m->m_len = total_len;
		}

		/* Do IP checksum checking. */
		if (rxsts & LGE_RXSTS_ISIP) {
			if (!(rxsts & LGE_RXSTS_IPCSUMERR))
				m->m_pkthdr.csum_flags |= M_IPV4_CSUM_IN_OK;
		}
		if (rxsts & LGE_RXSTS_ISTCP) {
			if (!(rxsts & LGE_RXSTS_TCPCSUMERR))
				m->m_pkthdr.csum_flags |= M_TCP_CSUM_IN_OK;
		}
		if (rxsts & LGE_RXSTS_ISUDP) {
			if (!(rxsts & LGE_RXSTS_UDPCSUMERR))
				m->m_pkthdr.csum_flags |= M_UDP_CSUM_IN_OK;
		}

		ml_enqueue(&ml, m);
	}

	if_input(ifp, &ml);

	sc->lge_cdata.lge_rx_cons = i;
}

/*
 * A frame was downloaded to the chip. It's safe for us to clean up
 * the list buffers.
 */

void
lge_txeof(struct lge_softc *sc)
{
	struct lge_tx_desc	*cur_tx = NULL;
	struct ifnet		*ifp;
	u_int32_t		idx, txdone;

	ifp = &sc->arpcom.ac_if;

	/* Clear the timeout timer. */
	ifp->if_timer = 0;

	/*
	 * Go through our tx list and free mbufs for those
	 * frames that have been transmitted.
	 */
	idx = sc->lge_cdata.lge_tx_cons;
	txdone = CSR_READ_1(sc, LGE_TXDMADONE_8BIT);

	while (idx != sc->lge_cdata.lge_tx_prod && txdone) {
		cur_tx = &sc->lge_ldata->lge_tx_list[idx];

		if (cur_tx->lge_mbuf != NULL) {
			m_freem(cur_tx->lge_mbuf);
			cur_tx->lge_mbuf = NULL;
		}
		cur_tx->lge_ctl = 0;

		txdone--;
		LGE_INC(idx, LGE_TX_LIST_CNT);
		ifp->if_timer = 0;
	}

	sc->lge_cdata.lge_tx_cons = idx;

	if (cur_tx != NULL)
		ifq_clr_oactive(&ifp->if_snd);
}

void
lge_tick(void *xsc)
{
	struct lge_softc	*sc = xsc;
	struct mii_data		*mii = &sc->lge_mii;
	struct ifnet		*ifp = &sc->arpcom.ac_if;
	int			s;

	s = splnet();

	CSR_WRITE_4(sc, LGE_STATSIDX, LGE_STATS_SINGLE_COLL_PKTS);
	ifp->if_collisions += CSR_READ_4(sc, LGE_STATSVAL);
	CSR_WRITE_4(sc, LGE_STATSIDX, LGE_STATS_MULTI_COLL_PKTS);
	ifp->if_collisions += CSR_READ_4(sc, LGE_STATSVAL);

	if (!sc->lge_link) {
		mii_tick(mii);
		if (mii->mii_media_status & IFM_ACTIVE &&
		    IFM_SUBTYPE(mii->mii_media_active) != IFM_NONE) {
			sc->lge_link++;
			if (!ifq_empty(&ifp->if_snd))
				lge_start(ifp);
		}
	}

	timeout_add_sec(&sc->lge_timeout, 1);

	splx(s);
}

int
lge_intr(void *arg)
{
	struct lge_softc	*sc;
	struct ifnet		*ifp;
	u_int32_t		status;
	int			claimed = 0;

	sc = arg;
	ifp = &sc->arpcom.ac_if;

	/* Suppress unwanted interrupts */
	if (!(ifp->if_flags & IFF_UP)) {
		lge_stop(sc);
		return (0);
	}

	for (;;) {
		/*
		 * Reading the ISR register clears all interrupts, and
		 * clears the 'interrupts enabled' bit in the IMR
		 * register.
		 */
		status = CSR_READ_4(sc, LGE_ISR);

		if ((status & LGE_INTRS) == 0)
			break;

		claimed = 1;

		if ((status & (LGE_ISR_TXCMDFIFO_EMPTY|LGE_ISR_TXDMA_DONE)))
			lge_txeof(sc);

		if (status & LGE_ISR_RXDMA_DONE)
			lge_rxeof(sc, LGE_RX_DMACNT(status));

		if (status & LGE_ISR_RXCMDFIFO_EMPTY)
			lge_init(sc);

		if (status & LGE_ISR_PHY_INTR) {
			sc->lge_link = 0;
			timeout_del(&sc->lge_timeout);
			lge_tick(sc);
		}
	}

	/* Re-enable interrupts. */
	CSR_WRITE_4(sc, LGE_IMR, LGE_IMR_SETRST_CTL0|LGE_IMR_INTR_ENB);

	if (!ifq_empty(&ifp->if_snd))
		lge_start(ifp);

	return (claimed);
}

/*
 * Encapsulate an mbuf chain in a descriptor by coupling the mbuf data
 * pointers to the fragment pointers.
 */
int
lge_encap(struct lge_softc *sc, struct mbuf *m_head, u_int32_t *txidx)
{
	struct lge_frag		*f = NULL;
	struct lge_tx_desc	*cur_tx;
	struct mbuf		*m;
	int			frag = 0, tot_len = 0;

	/*
 	 * Start packing the mbufs in this chain into
	 * the fragment pointers. Stop when we run out
 	 * of fragments or hit the end of the mbuf chain.
	 */
	m = m_head;
	cur_tx = &sc->lge_ldata->lge_tx_list[*txidx];
	frag = 0;

	for (m = m_head; m != NULL; m = m->m_next) {
		if (m->m_len != 0) {
			tot_len += m->m_len;
			f = &cur_tx->lge_frags[frag];
			f->lge_fraglen = m->m_len;
			f->lge_fragptr_lo = VTOPHYS(mtod(m, vaddr_t));
			f->lge_fragptr_hi = 0;
			frag++;
		}
	}

	if (m != NULL)
		return (ENOBUFS);

	cur_tx->lge_mbuf = m_head;
	cur_tx->lge_ctl = LGE_TXCTL_WANTINTR|LGE_FRAGCNT(frag)|tot_len;
	LGE_INC((*txidx), LGE_TX_LIST_CNT);

	/* Queue for transmit */
	CSR_WRITE_4(sc, LGE_TXDESC_ADDR_LO, VTOPHYS(cur_tx));

	return (0);
}

/*
 * Main transmit routine. To avoid having to do mbuf copies, we put pointers
 * to the mbuf data regions directly in the transmit lists. We also save a
 * copy of the pointers since the transmit list fragment pointers are
 * physical addresses.
 */

void
lge_start(struct ifnet *ifp)
{
	struct lge_softc	*sc;
	struct mbuf		*m_head = NULL;
	u_int32_t		idx;
	int			pkts = 0;

	sc = ifp->if_softc;

	if (!sc->lge_link)
		return;

	idx = sc->lge_cdata.lge_tx_prod;

	if (ifq_is_oactive(&ifp->if_snd))
		return;

	while(sc->lge_ldata->lge_tx_list[idx].lge_mbuf == NULL) {
		if (CSR_READ_1(sc, LGE_TXCMDFREE_8BIT) == 0)
			break;

		m_head = ifq_deq_begin(&ifp->if_snd);
		if (m_head == NULL)
			break;

		if (lge_encap(sc, m_head, &idx)) {
			ifq_deq_rollback(&ifp->if_snd, m_head);
			ifq_set_oactive(&ifp->if_snd);
			break;
		}

		/* now we are committed to transmit the packet */
		ifq_deq_commit(&ifp->if_snd, m_head);
		pkts++;

#if NBPFILTER > 0
		/*
		 * If there's a BPF listener, bounce a copy of this frame
		 * to him.
		 */
		if (ifp->if_bpf)
			bpf_mtap(ifp->if_bpf, m_head, BPF_DIRECTION_OUT);
#endif
	}
	if (pkts == 0)
		return;

	sc->lge_cdata.lge_tx_prod = idx;

	/*
	 * Set a timeout in case the chip goes out to lunch.
	 */
	ifp->if_timer = 5;
}

void
lge_init(void *xsc)
{
	struct lge_softc	*sc = xsc;
	struct ifnet		*ifp = &sc->arpcom.ac_if;
	int			s;

	s = splnet();

	/*
	 * Cancel pending I/O and free all RX/TX buffers.
	 */
	lge_stop(sc);
	lge_reset(sc);

	/* Set MAC address */
	CSR_WRITE_4(sc, LGE_PAR0, *(u_int32_t *)(&sc->arpcom.ac_enaddr[0]));
	CSR_WRITE_4(sc, LGE_PAR1, *(u_int32_t *)(&sc->arpcom.ac_enaddr[4]));

	/* Init circular RX list. */
	if (lge_list_rx_init(sc) == ENOBUFS) {
		printf("%s: initialization failed: no "
		       "memory for rx buffers\n", sc->sc_dv.dv_xname);
		lge_stop(sc);
		splx(s);
		return;
	}

	/*
	 * Init tx descriptors.
	 */
	lge_list_tx_init(sc);

	/* Set initial value for MODE1 register. */
	CSR_WRITE_4(sc, LGE_MODE1, LGE_MODE1_RX_UCAST|
	    LGE_MODE1_TX_CRC|LGE_MODE1_TXPAD|
	    LGE_MODE1_RX_FLOWCTL|LGE_MODE1_SETRST_CTL0|
	    LGE_MODE1_SETRST_CTL1|LGE_MODE1_SETRST_CTL2);

	 /* If we want promiscuous mode, set the allframes bit. */
	if (ifp->if_flags & IFF_PROMISC) {
		CSR_WRITE_4(sc, LGE_MODE1,
		    LGE_MODE1_SETRST_CTL1|LGE_MODE1_RX_PROMISC);
	} else {
		CSR_WRITE_4(sc, LGE_MODE1, LGE_MODE1_RX_PROMISC);
	}

	/*
	 * Set the capture broadcast bit to capture broadcast frames.
	 */
	if (ifp->if_flags & IFF_BROADCAST) {
		CSR_WRITE_4(sc, LGE_MODE1,
		    LGE_MODE1_SETRST_CTL1|LGE_MODE1_RX_BCAST);
	} else {
		CSR_WRITE_4(sc, LGE_MODE1, LGE_MODE1_RX_BCAST);
	}

	/* Packet padding workaround? */
	CSR_WRITE_4(sc, LGE_MODE1, LGE_MODE1_SETRST_CTL1|LGE_MODE1_RMVPAD);

	/* No error frames */
	CSR_WRITE_4(sc, LGE_MODE1, LGE_MODE1_RX_ERRPKTS);

	/* Receive large frames */
	CSR_WRITE_4(sc, LGE_MODE1, LGE_MODE1_SETRST_CTL1|LGE_MODE1_RX_GIANTS);

	/* Workaround: disable RX/TX flow control */
	CSR_WRITE_4(sc, LGE_MODE1, LGE_MODE1_TX_FLOWCTL);
	CSR_WRITE_4(sc, LGE_MODE1, LGE_MODE1_RX_FLOWCTL);

	/* Make sure to strip CRC from received frames */
	CSR_WRITE_4(sc, LGE_MODE1, LGE_MODE1_RX_CRC);

	/* Turn off magic packet mode */
	CSR_WRITE_4(sc, LGE_MODE1, LGE_MODE1_MPACK_ENB);

	/* Turn off all VLAN stuff */
	CSR_WRITE_4(sc, LGE_MODE1, LGE_MODE1_VLAN_RX|LGE_MODE1_VLAN_TX|
	    LGE_MODE1_VLAN_STRIP|LGE_MODE1_VLAN_INSERT);

	/* Workaround: FIFO overflow */
	CSR_WRITE_2(sc, LGE_RXFIFO_HIWAT, 0x3FFF);
	CSR_WRITE_4(sc, LGE_IMR, LGE_IMR_SETRST_CTL1|LGE_IMR_RXFIFO_WAT);

	/*
	 * Load the multicast filter.
	 */
	lge_setmulti(sc);

	/*
	 * Enable hardware checksum validation for all received IPv4
	 * packets, do not reject packets with bad checksums.
	 */
	CSR_WRITE_4(sc, LGE_MODE2, LGE_MODE2_RX_IPCSUM|
	    LGE_MODE2_RX_TCPCSUM|LGE_MODE2_RX_UDPCSUM|
	    LGE_MODE2_RX_ERRCSUM);

	/*
	 * Enable the delivery of PHY interrupts based on
	 * link/speed/duplex status changes.
	 */
	CSR_WRITE_4(sc, LGE_MODE1, LGE_MODE1_SETRST_CTL0|LGE_MODE1_GMIIPOLL);

	/* Enable receiver and transmitter. */
	CSR_WRITE_4(sc, LGE_RXDESC_ADDR_HI, 0);
	CSR_WRITE_4(sc, LGE_MODE1, LGE_MODE1_SETRST_CTL1|LGE_MODE1_RX_ENB);

	CSR_WRITE_4(sc, LGE_TXDESC_ADDR_HI, 0);
	CSR_WRITE_4(sc, LGE_MODE1, LGE_MODE1_SETRST_CTL1|LGE_MODE1_TX_ENB);

	/*
	 * Enable interrupts.
	 */
	CSR_WRITE_4(sc, LGE_IMR, LGE_IMR_SETRST_CTL0|
	    LGE_IMR_SETRST_CTL1|LGE_IMR_INTR_ENB|LGE_INTRS);

	lge_ifmedia_upd(ifp);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	splx(s);

	timeout_add_sec(&sc->lge_timeout, 1);
}

/*
 * Set media options.
 */
int
lge_ifmedia_upd(struct ifnet *ifp)
{
	struct lge_softc	*sc = ifp->if_softc;
	struct mii_data		*mii = &sc->lge_mii;

	sc->lge_link = 0;
	if (mii->mii_instance) {
		struct mii_softc *miisc;
		LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
			mii_phy_reset(miisc);
	}
	mii_mediachg(mii);

	return (0);
}

/*
 * Report current media status.
 */
void
lge_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct lge_softc	*sc = ifp->if_softc;
	struct mii_data		*mii = &sc->lge_mii;

	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
}

int
lge_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct lge_softc	*sc = ifp->if_softc;
	struct ifreq		*ifr = (struct ifreq *) data;
	struct mii_data		*mii;
	int			s, error = 0;

	s = splnet();

	switch(command) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		if (!(ifp->if_flags & IFF_RUNNING))
			lge_init(sc);
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING &&
			    ifp->if_flags & IFF_PROMISC &&
			    !(sc->lge_if_flags & IFF_PROMISC)) {
				CSR_WRITE_4(sc, LGE_MODE1,
				    LGE_MODE1_SETRST_CTL1|
				    LGE_MODE1_RX_PROMISC);
				lge_setmulti(sc);
			} else if (ifp->if_flags & IFF_RUNNING &&
			    !(ifp->if_flags & IFF_PROMISC) &&
			    sc->lge_if_flags & IFF_PROMISC) {
				CSR_WRITE_4(sc, LGE_MODE1,
				    LGE_MODE1_RX_PROMISC);
				lge_setmulti(sc);
			} else if (ifp->if_flags & IFF_RUNNING &&
			    (ifp->if_flags ^ sc->lge_if_flags) & IFF_ALLMULTI) {
				lge_setmulti(sc);
			} else {
				if (!(ifp->if_flags & IFF_RUNNING))
					lge_init(sc);
			}
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				lge_stop(sc);
		}
		sc->lge_if_flags = ifp->if_flags;
		break;

	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		mii = &sc->lge_mii;
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, command);
		break;

	default:
		error = ether_ioctl(ifp, &sc->arpcom, command, data);
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			lge_setmulti(sc);
		error = 0;
	}

	splx(s);
	return (error);
}

void
lge_watchdog(struct ifnet *ifp)
{
	struct lge_softc	*sc;

	sc = ifp->if_softc;

	ifp->if_oerrors++;
	printf("%s: watchdog timeout\n", sc->sc_dv.dv_xname);

	lge_stop(sc);
	lge_reset(sc);
	lge_init(sc);

	if (!ifq_empty(&ifp->if_snd))
		lge_start(ifp);
}

/*
 * Stop the adapter and free any mbufs allocated to the
 * RX and TX lists.
 */
void
lge_stop(struct lge_softc *sc)
{
	int			i;
	struct ifnet		*ifp;

	ifp = &sc->arpcom.ac_if;
	ifp->if_timer = 0;
	timeout_del(&sc->lge_timeout);

	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	CSR_WRITE_4(sc, LGE_IMR, LGE_IMR_INTR_ENB);

	/* Disable receiver and transmitter. */
	CSR_WRITE_4(sc, LGE_MODE1, LGE_MODE1_RX_ENB|LGE_MODE1_TX_ENB);
	sc->lge_link = 0;

	/*
	 * Free data in the RX lists.
	 */
	for (i = 0; i < LGE_RX_LIST_CNT; i++) {
		if (sc->lge_ldata->lge_rx_list[i].lge_mbuf != NULL) {
			m_freem(sc->lge_ldata->lge_rx_list[i].lge_mbuf);
			sc->lge_ldata->lge_rx_list[i].lge_mbuf = NULL;
		}
	}
	bzero(&sc->lge_ldata->lge_rx_list, sizeof(sc->lge_ldata->lge_rx_list));

	/*
	 * Free the TX list buffers.
	 */
	for (i = 0; i < LGE_TX_LIST_CNT; i++) {
		if (sc->lge_ldata->lge_tx_list[i].lge_mbuf != NULL) {
			m_freem(sc->lge_ldata->lge_tx_list[i].lge_mbuf);
			sc->lge_ldata->lge_tx_list[i].lge_mbuf = NULL;
		}
	}

	bzero(&sc->lge_ldata->lge_tx_list, sizeof(sc->lge_ldata->lge_tx_list));
}
