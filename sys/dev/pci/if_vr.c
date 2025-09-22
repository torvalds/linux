/*	$OpenBSD: if_vr.c,v 1.162 2024/08/31 16:23:09 deraadt Exp $	*/

/*
 * Copyright (c) 1997, 1998
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
 * $FreeBSD: src/sys/pci/if_vr.c,v 1.73 2003/08/22 07:13:22 imp Exp $
 */

/*
 * VIA Rhine fast ethernet PCI NIC driver
 *
 * Supports various network adapters based on the VIA Rhine
 * and Rhine II PCI controllers, including the D-Link DFE530TX.
 * Datasheets are available at ftp://ftp.vtbridge.org/Docs/LAN/.
 *
 * Written by Bill Paul <wpaul@ctr.columbia.edu>
 * Electrical Engineering Department
 * Columbia University, New York City
 */

/*
 * The VIA Rhine controllers are similar in some respects to the
 * the DEC tulip chips, except less complicated. The controller
 * uses an MII bus and an external physical layer interface. The
 * receiver has a one entry perfect filter and a 64-bit hash table
 * multicast filter. Transmit and receive descriptors are similar
 * to the tulip.
 *
 * Early Rhine has a serious flaw in its transmit DMA mechanism:
 * transmit buffers must be longword aligned. Unfortunately,
 * OpenBSD doesn't guarantee that mbufs will be filled in starting
 * at longword boundaries, so we have to do a buffer copy before
 * transmission.
 */

#include "bpfilter.h"
#include "vlan.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/timeout.h>

#include <net/if.h>
#include <sys/device.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <net/if_media.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <machine/bus.h>

#include <dev/mii/miivar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#define VR_USEIOSPACE

#include <dev/pci/if_vrreg.h>

int vr_probe(struct device *, void *, void *);
int vr_quirks(struct pci_attach_args *);
void vr_attach(struct device *, struct device *, void *);
int vr_activate(struct device *, int);

const struct cfattach vr_ca = {
	sizeof(struct vr_softc), vr_probe, vr_attach, NULL,
	vr_activate
};
struct cfdriver vr_cd = {
	NULL, "vr", DV_IFNET
};

int vr_encap(struct vr_softc *, struct vr_chain **, struct mbuf *);
void vr_rxeof(struct vr_softc *);
void vr_rxeoc(struct vr_softc *);
void vr_txeof(struct vr_softc *);
void vr_tick(void *);
void vr_rxtick(void *);
int vr_intr(void *);
int vr_dmamem_alloc(struct vr_softc *, struct vr_dmamem *,
    bus_size_t, u_int);
void vr_dmamem_free(struct vr_softc *, struct vr_dmamem *);
void vr_start(struct ifnet *);
int vr_ioctl(struct ifnet *, u_long, caddr_t);
void vr_chipinit(struct vr_softc *);
void vr_init(void *);
void vr_stop(struct vr_softc *);
void vr_watchdog(struct ifnet *);
int vr_ifmedia_upd(struct ifnet *);
void vr_ifmedia_sts(struct ifnet *, struct ifmediareq *);

int vr_mii_readreg(struct vr_softc *, struct vr_mii_frame *);
int vr_mii_writereg(struct vr_softc *, struct vr_mii_frame *);
int vr_miibus_readreg(struct device *, int, int);
void vr_miibus_writereg(struct device *, int, int, int);
void vr_miibus_statchg(struct device *);

void vr_setcfg(struct vr_softc *, uint64_t);
void vr_iff(struct vr_softc *);
void vr_reset(struct vr_softc *);
int vr_list_rx_init(struct vr_softc *);
void vr_fill_rx_ring(struct vr_softc *);
int vr_list_tx_init(struct vr_softc *);
#ifndef SMALL_KERNEL
int vr_wol(struct ifnet *, int);
#endif

int vr_alloc_mbuf(struct vr_softc *, struct vr_chain_onefrag *);

/*
 * Supported devices & quirks
 */
#define	VR_Q_NEEDALIGN		(1<<0)
#define	VR_Q_CSUM		(1<<1)
#define	VR_Q_CAM		(1<<2)
#define	VR_Q_HWTAG		(1<<3)
#define	VR_Q_INTDISABLE		(1<<4)
#define	VR_Q_BABYJUMBO		(1<<5) /* others may work too */

struct vr_type {
	pci_vendor_id_t		vr_vid;
	pci_product_id_t	vr_pid;
	int			vr_quirks;
} vr_devices[] = {
	{ PCI_VENDOR_VIATECH, PCI_PRODUCT_VIATECH_RHINE,
	    VR_Q_NEEDALIGN },
	{ PCI_VENDOR_VIATECH, PCI_PRODUCT_VIATECH_RHINEII,
	    VR_Q_NEEDALIGN },
	{ PCI_VENDOR_VIATECH, PCI_PRODUCT_VIATECH_RHINEII_2,
	    VR_Q_BABYJUMBO },
	{ PCI_VENDOR_VIATECH, PCI_PRODUCT_VIATECH_VT6105,
	    VR_Q_BABYJUMBO },
	{ PCI_VENDOR_VIATECH, PCI_PRODUCT_VIATECH_VT6105M,
	    VR_Q_CSUM | VR_Q_CAM | VR_Q_HWTAG | VR_Q_INTDISABLE |
	    VR_Q_BABYJUMBO },
	{ PCI_VENDOR_DELTA, PCI_PRODUCT_DELTA_RHINEII,
	    VR_Q_NEEDALIGN },
	{ PCI_VENDOR_ADDTRON, PCI_PRODUCT_ADDTRON_RHINEII,
	    VR_Q_NEEDALIGN }
};

#define VR_SETBIT(sc, reg, x)				\
	CSR_WRITE_1(sc, reg,				\
		CSR_READ_1(sc, reg) | (x))

#define VR_CLRBIT(sc, reg, x)				\
	CSR_WRITE_1(sc, reg,				\
		CSR_READ_1(sc, reg) & ~(x))

#define VR_SETBIT16(sc, reg, x)				\
	CSR_WRITE_2(sc, reg,				\
		CSR_READ_2(sc, reg) | (x))

#define VR_CLRBIT16(sc, reg, x)				\
	CSR_WRITE_2(sc, reg,				\
		CSR_READ_2(sc, reg) & ~(x))

#define VR_SETBIT32(sc, reg, x)				\
	CSR_WRITE_4(sc, reg,				\
		CSR_READ_4(sc, reg) | (x))

#define VR_CLRBIT32(sc, reg, x)				\
	CSR_WRITE_4(sc, reg,				\
		CSR_READ_4(sc, reg) & ~(x))

#define SIO_SET(x)					\
	CSR_WRITE_1(sc, VR_MIICMD,			\
		CSR_READ_1(sc, VR_MIICMD) | (x))

#define SIO_CLR(x)					\
	CSR_WRITE_1(sc, VR_MIICMD,			\
		CSR_READ_1(sc, VR_MIICMD) & ~(x))

/*
 * Read an PHY register through the MII.
 */
int
vr_mii_readreg(struct vr_softc *sc, struct vr_mii_frame *frame)
{
	int			s, i;

	s = splnet();

	/* Set the PHY-address */
	CSR_WRITE_1(sc, VR_PHYADDR, (CSR_READ_1(sc, VR_PHYADDR)& 0xe0)|
	    frame->mii_phyaddr);

	/* Set the register-address */
	CSR_WRITE_1(sc, VR_MIIADDR, frame->mii_regaddr);
	VR_SETBIT(sc, VR_MIICMD, VR_MIICMD_READ_ENB);

	for (i = 0; i < 10000; i++) {
		if ((CSR_READ_1(sc, VR_MIICMD) & VR_MIICMD_READ_ENB) == 0)
			break;
		DELAY(1);
	}

	frame->mii_data = CSR_READ_2(sc, VR_MIIDATA);

	splx(s);

	return(0);
}

/*
 * Write to a PHY register through the MII.
 */
int
vr_mii_writereg(struct vr_softc *sc, struct vr_mii_frame *frame)
{
	int			s, i;

	s = splnet();

	/* Set the PHY-address */
	CSR_WRITE_1(sc, VR_PHYADDR, (CSR_READ_1(sc, VR_PHYADDR)& 0xe0)|
	    frame->mii_phyaddr);

	/* Set the register-address and data to write */
	CSR_WRITE_1(sc, VR_MIIADDR, frame->mii_regaddr);
	CSR_WRITE_2(sc, VR_MIIDATA, frame->mii_data);

	VR_SETBIT(sc, VR_MIICMD, VR_MIICMD_WRITE_ENB);

	for (i = 0; i < 10000; i++) {
		if ((CSR_READ_1(sc, VR_MIICMD) & VR_MIICMD_WRITE_ENB) == 0)
			break;
		DELAY(1);
	}

	splx(s);

	return(0);
}

int
vr_miibus_readreg(struct device *dev, int phy, int reg)
{
	struct vr_softc *sc = (struct vr_softc *)dev;
	struct vr_mii_frame frame;

	switch (sc->vr_revid) {
	case REV_ID_VT6102_APOLLO:
	case REV_ID_VT6103:
		if (phy != 1)
			return 0;
	default:
		break;
	}

	bzero(&frame, sizeof(frame));

	frame.mii_phyaddr = phy;
	frame.mii_regaddr = reg;
	vr_mii_readreg(sc, &frame);

	return(frame.mii_data);
}

void
vr_miibus_writereg(struct device *dev, int phy, int reg, int data)
{
	struct vr_softc *sc = (struct vr_softc *)dev;
	struct vr_mii_frame frame;

	switch (sc->vr_revid) {
	case REV_ID_VT6102_APOLLO:
	case REV_ID_VT6103:
		if (phy != 1)
			return;
	default:
		break;
	}

	bzero(&frame, sizeof(frame));

	frame.mii_phyaddr = phy;
	frame.mii_regaddr = reg;
	frame.mii_data = data;

	vr_mii_writereg(sc, &frame);
}

void
vr_miibus_statchg(struct device *dev)
{
	struct vr_softc *sc = (struct vr_softc *)dev;

	vr_setcfg(sc, sc->sc_mii.mii_media_active);
}

void
vr_iff(struct vr_softc *sc)
{
	struct arpcom		*ac = &sc->arpcom;
	struct ifnet		*ifp = &sc->arpcom.ac_if;
	int			h = 0;
	u_int32_t		hashes[2];
	struct ether_multi	*enm;
	struct ether_multistep	step;
	u_int8_t		rxfilt;

	rxfilt = CSR_READ_1(sc, VR_RXCFG);
	rxfilt &= ~(VR_RXCFG_RX_BROAD | VR_RXCFG_RX_MULTI |
	    VR_RXCFG_RX_PROMISC);
	ifp->if_flags &= ~IFF_ALLMULTI;

	/*
	 * Always accept broadcast frames.
	 */
	rxfilt |= VR_RXCFG_RX_BROAD;

	if (ifp->if_flags & IFF_PROMISC || ac->ac_multirangecnt > 0) {
		ifp->if_flags |= IFF_ALLMULTI;
		rxfilt |= VR_RXCFG_RX_MULTI;
		if (ifp->if_flags & IFF_PROMISC)
			rxfilt |= VR_RXCFG_RX_PROMISC;
		hashes[0] = hashes[1] = 0xFFFFFFFF;
	} else {
		/* Program new filter. */
		rxfilt |= VR_RXCFG_RX_MULTI;
		bzero(hashes, sizeof(hashes));

		ETHER_FIRST_MULTI(step, ac, enm);
		while (enm != NULL) {
			h = ether_crc32_be(enm->enm_addrlo,
			    ETHER_ADDR_LEN) >> 26;

			if (h < 32)
				hashes[0] |= (1 << h);
			else
				hashes[1] |= (1 << (h - 32));

			ETHER_NEXT_MULTI(step, enm);
		}
	}

	CSR_WRITE_4(sc, VR_MAR0, hashes[0]);
	CSR_WRITE_4(sc, VR_MAR1, hashes[1]);
	CSR_WRITE_1(sc, VR_RXCFG, rxfilt);
}

/*
 * In order to fiddle with the
 * 'full-duplex' and '100Mbps' bits in the netconfig register, we
 * first have to put the transmit and/or receive logic in the idle state.
 */
void
vr_setcfg(struct vr_softc *sc, uint64_t media)
{
	int i;

	if (sc->sc_mii.mii_media_status & IFM_ACTIVE &&
	    IFM_SUBTYPE(sc->sc_mii.mii_media_active) != IFM_NONE) {
		sc->vr_link = 1;

		if (CSR_READ_2(sc, VR_COMMAND) & (VR_CMD_TX_ON|VR_CMD_RX_ON))
			VR_CLRBIT16(sc, VR_COMMAND,
			    (VR_CMD_TX_ON|VR_CMD_RX_ON));

		if ((media & IFM_GMASK) == IFM_FDX)
			VR_SETBIT16(sc, VR_COMMAND, VR_CMD_FULLDUPLEX);
		else
			VR_CLRBIT16(sc, VR_COMMAND, VR_CMD_FULLDUPLEX);

		VR_SETBIT16(sc, VR_COMMAND, VR_CMD_TX_ON|VR_CMD_RX_ON);
	} else {
		sc->vr_link = 0;
		VR_CLRBIT16(sc, VR_COMMAND, (VR_CMD_TX_ON|VR_CMD_RX_ON));
		for (i = VR_TIMEOUT; i > 0; i--) {
			DELAY(10);
			if (!(CSR_READ_2(sc, VR_COMMAND) &
			    (VR_CMD_TX_ON|VR_CMD_RX_ON)))
				break;
		}
		if (i == 0) {
#ifdef VR_DEBUG
			printf("%s: rx shutdown error!\n", sc->sc_dev.dv_xname);
#endif
			sc->vr_flags |= VR_F_RESTART;
		}
	}
}

void
vr_reset(struct vr_softc *sc)
{
	int			i;

	VR_SETBIT16(sc, VR_COMMAND, VR_CMD_RESET);

	for (i = 0; i < VR_TIMEOUT; i++) {
		DELAY(10);
		if (!(CSR_READ_2(sc, VR_COMMAND) & VR_CMD_RESET))
			break;
	}
	if (i == VR_TIMEOUT) {
		if (sc->vr_revid < REV_ID_VT3065_A)
			printf("%s: reset never completed!\n",
			    sc->sc_dev.dv_xname);
		else {
#ifdef VR_DEBUG
			/* Use newer force reset command */
			printf("%s: Using force reset command.\n",
			    sc->sc_dev.dv_xname);
#endif
			VR_SETBIT(sc, VR_MISC_CR1, VR_MISCCR1_FORSRST);
		}
	}

	/* Wait a little while for the chip to get its brains in order. */
	DELAY(1000);
}

/*
 * Probe for a VIA Rhine chip.
 */
int
vr_probe(struct device *parent, void *match, void *aux)
{
	const struct vr_type *vr;
	struct pci_attach_args *pa = (struct pci_attach_args *)aux;
	int i, nent = nitems(vr_devices);

	for (i = 0, vr = vr_devices; i < nent; i++, vr++)
		if (PCI_VENDOR(pa->pa_id) == vr->vr_vid &&
		   PCI_PRODUCT(pa->pa_id) == vr->vr_pid)
			return(1);

	return(0);
}

int
vr_quirks(struct pci_attach_args *pa)
{
	const struct vr_type *vr;
	int i, nent = nitems(vr_devices);

	for (i = 0, vr = vr_devices; i < nent; i++, vr++)
		if (PCI_VENDOR(pa->pa_id) == vr->vr_vid &&
		   PCI_PRODUCT(pa->pa_id) == vr->vr_pid)
			return(vr->vr_quirks);

	return(0);
}

int
vr_dmamem_alloc(struct vr_softc *sc, struct vr_dmamem *vrm,
    bus_size_t size, u_int align)
{
	vrm->vrm_size = size;

	if (bus_dmamap_create(sc->sc_dmat, vrm->vrm_size, 1,
	    vrm->vrm_size, 0, BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW,
	    &vrm->vrm_map) != 0)
		return (1);
	if (bus_dmamem_alloc(sc->sc_dmat, vrm->vrm_size,
	    align, 0, &vrm->vrm_seg, 1, &vrm->vrm_nsegs,
	    BUS_DMA_WAITOK | BUS_DMA_ZERO) != 0)
		goto destroy;
	if (bus_dmamem_map(sc->sc_dmat, &vrm->vrm_seg, vrm->vrm_nsegs,
	    vrm->vrm_size, &vrm->vrm_kva, BUS_DMA_WAITOK) != 0)
		goto free;
	if (bus_dmamap_load(sc->sc_dmat, vrm->vrm_map, vrm->vrm_kva,
	    vrm->vrm_size, NULL, BUS_DMA_WAITOK) != 0)
		goto unmap;

	return (0);
 unmap:
	bus_dmamem_unmap(sc->sc_dmat, vrm->vrm_kva, vrm->vrm_size);
 free:
	bus_dmamem_free(sc->sc_dmat, &vrm->vrm_seg, 1);
 destroy:
	bus_dmamap_destroy(sc->sc_dmat, vrm->vrm_map);
	return (1);
}

void
vr_dmamem_free(struct vr_softc *sc, struct vr_dmamem *vrm)
{
	bus_dmamap_unload(sc->sc_dmat, vrm->vrm_map);
	bus_dmamem_unmap(sc->sc_dmat, vrm->vrm_kva, vrm->vrm_size);
	bus_dmamem_free(sc->sc_dmat, &vrm->vrm_seg, 1);
	bus_dmamap_destroy(sc->sc_dmat, vrm->vrm_map);
}

/*
 * Attach the interface. Allocate softc structures, do ifmedia
 * setup and ethernet/BPF attach.
 */
void
vr_attach(struct device *parent, struct device *self, void *aux)
{
	int			i;
	struct vr_softc		*sc = (struct vr_softc *)self;
	struct pci_attach_args	*pa = aux;
	pci_chipset_tag_t	pc = pa->pa_pc;
	pci_intr_handle_t	ih;
	const char		*intrstr = NULL;
	struct ifnet		*ifp = &sc->arpcom.ac_if;
	bus_size_t		size;

	pci_set_powerstate(pa->pa_pc, pa->pa_tag, PCI_PMCSR_STATE_D0);

	/*
	 * Map control/status registers.
	 */

#ifdef VR_USEIOSPACE
	if (pci_mapreg_map(pa, VR_PCI_LOIO, PCI_MAPREG_TYPE_IO, 0,
	    &sc->vr_btag, &sc->vr_bhandle, NULL, &size, 0)) {
		printf(": can't map i/o space\n");
		return;
	}
#else
	if (pci_mapreg_map(pa, VR_PCI_LOMEM, PCI_MAPREG_TYPE_MEM, 0,
	    &sc->vr_btag, &sc->vr_bhandle, NULL, &size, 0)) {
		printf(": can't map mem space\n");
		return;
	}
#endif

	/* Allocate interrupt */
	if (pci_intr_map(pa, &ih)) {
		printf(": can't map interrupt\n");
		goto fail;
	}
	intrstr = pci_intr_string(pc, ih);
	sc->sc_ih = pci_intr_establish(pc, ih, IPL_NET, vr_intr, sc,
				       self->dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		goto fail;
	}
	printf(": %s", intrstr);

	sc->vr_revid = PCI_REVISION(pa->pa_class);
	sc->sc_pc = pa->pa_pc;
	sc->sc_tag = pa->pa_tag;

	vr_chipinit(sc);

	/*
	 * Get station address. The way the Rhine chips work,
	 * you're not allowed to directly access the EEPROM once
	 * they've been programmed a special way. Consequently,
	 * we need to read the node address from the PAR0 and PAR1
	 * registers.
	 */
	VR_SETBIT(sc, VR_EECSR, VR_EECSR_LOAD);
	DELAY(1000);
	for (i = 0; i < ETHER_ADDR_LEN; i++)
		sc->arpcom.ac_enaddr[i] = CSR_READ_1(sc, VR_PAR0 + i);

	/*
	 * A Rhine chip was detected. Inform the world.
	 */
	printf(", address %s\n", ether_sprintf(sc->arpcom.ac_enaddr));

	sc->sc_dmat = pa->pa_dmat;
	if (vr_dmamem_alloc(sc, &sc->sc_zeromap, 64, PAGE_SIZE) != 0) {
		printf(": failed to allocate zero pad memory\n");
		return;
	}
	bzero(sc->sc_zeromap.vrm_kva, 64);
	bus_dmamap_sync(sc->sc_dmat, sc->sc_zeromap.vrm_map, 0,
	    sc->sc_zeromap.vrm_map->dm_mapsize, BUS_DMASYNC_PREREAD);
	if (vr_dmamem_alloc(sc, &sc->sc_listmap, sizeof(struct vr_list_data),
	    PAGE_SIZE) != 0) {
		printf(": failed to allocate dma map\n");
		goto free_zero;
	}

	sc->vr_ldata = (struct vr_list_data *)sc->sc_listmap.vrm_kva;
	sc->vr_quirks = vr_quirks(pa);

	ifp = &sc->arpcom.ac_if;
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = vr_ioctl;
	ifp->if_start = vr_start;
	ifp->if_watchdog = vr_watchdog;
	if (sc->vr_quirks & VR_Q_BABYJUMBO)
		ifp->if_hardmtu = VR_RXLEN_BABYJUMBO -
		    ETHER_HDR_LEN - ETHER_CRC_LEN;
	bcopy(sc->sc_dev.dv_xname, ifp->if_xname, IFNAMSIZ);

	ifp->if_capabilities = IFCAP_VLAN_MTU;

	if (sc->vr_quirks & VR_Q_CSUM)
		ifp->if_capabilities |= IFCAP_CSUM_IPv4 | IFCAP_CSUM_TCPv4 |
					IFCAP_CSUM_UDPv4;

#if NVLAN > 0
	/* if the hardware can do VLAN tagging, say so. */
	if (sc->vr_quirks & VR_Q_HWTAG)
		ifp->if_capabilities |= IFCAP_VLAN_HWTAGGING;
#endif

#ifndef SMALL_KERNEL
	if (sc->vr_revid >= REV_ID_VT3065_A) {
		ifp->if_capabilities |= IFCAP_WOL;
		ifp->if_wol = vr_wol;
		vr_wol(ifp, 0);
	}
#endif

	/*
	 * Do MII setup.
	 */
	sc->sc_mii.mii_ifp = ifp;
	sc->sc_mii.mii_readreg = vr_miibus_readreg;
	sc->sc_mii.mii_writereg = vr_miibus_writereg;
	sc->sc_mii.mii_statchg = vr_miibus_statchg;
	ifmedia_init(&sc->sc_mii.mii_media, 0, vr_ifmedia_upd, vr_ifmedia_sts);
	mii_attach(self, &sc->sc_mii, 0xffffffff, MII_PHY_ANY, MII_OFFSET_ANY,
	    0);
	if (LIST_FIRST(&sc->sc_mii.mii_phys) == NULL) {
		ifmedia_add(&sc->sc_mii.mii_media, IFM_ETHER|IFM_NONE, 0, NULL);
		ifmedia_set(&sc->sc_mii.mii_media, IFM_ETHER|IFM_NONE);
	} else
		ifmedia_set(&sc->sc_mii.mii_media, IFM_ETHER|IFM_AUTO);
	timeout_set(&sc->sc_to, vr_tick, sc);
	timeout_set(&sc->sc_rxto, vr_rxtick, sc);

	/*
	 * Call MI attach routines.
	 */
	if_attach(ifp);
	ether_ifattach(ifp);
	return;

free_zero:
	bus_dmamap_sync(sc->sc_dmat, sc->sc_zeromap.vrm_map, 0,
	    sc->sc_zeromap.vrm_map->dm_mapsize, BUS_DMASYNC_POSTREAD);
	vr_dmamem_free(sc, &sc->sc_zeromap);
fail:
	bus_space_unmap(sc->vr_btag, sc->vr_bhandle, size);
}

int
vr_activate(struct device *self, int act)
{
	struct vr_softc *sc = (struct vr_softc *)self;
	struct ifnet *ifp = &sc->arpcom.ac_if;

	switch (act) {
	case DVACT_SUSPEND:
		if (ifp->if_flags & IFF_RUNNING)
			vr_stop(sc);
		break;
	case DVACT_RESUME:
		if (ifp->if_flags & IFF_UP)
			vr_init(sc);
		break;
	}
	return (0);
}

/*
 * Initialize the transmit descriptors.
 */
int
vr_list_tx_init(struct vr_softc *sc)
{
	struct vr_chain_data	*cd;
	struct vr_list_data	*ld;
	int			i;

	cd = &sc->vr_cdata;
	ld = sc->vr_ldata;

	cd->vr_tx_cnt = cd->vr_tx_pkts = 0;

	for (i = 0; i < VR_TX_LIST_CNT; i++) {
		cd->vr_tx_chain[i].vr_ptr = &ld->vr_tx_list[i];
		cd->vr_tx_chain[i].vr_paddr =
		    sc->sc_listmap.vrm_map->dm_segs[0].ds_addr +
		    offsetof(struct vr_list_data, vr_tx_list[i]);

		if (bus_dmamap_create(sc->sc_dmat, MCLBYTES, VR_MAXFRAGS,
		    MCLBYTES, 0, BUS_DMA_NOWAIT, &cd->vr_tx_chain[i].vr_map))
			return (ENOBUFS);

		if (i == (VR_TX_LIST_CNT - 1))
			cd->vr_tx_chain[i].vr_nextdesc =
				&cd->vr_tx_chain[0];
		else
			cd->vr_tx_chain[i].vr_nextdesc =
				&cd->vr_tx_chain[i + 1];
	}

	cd->vr_tx_cons = cd->vr_tx_prod = &cd->vr_tx_chain[0];

	return (0);
}


/*
 * Initialize the RX descriptors and allocate mbufs for them. Note that
 * we arrange the descriptors in a closed ring, so that the last descriptor
 * points back to the first.
 */
int
vr_list_rx_init(struct vr_softc *sc)
{
	struct vr_chain_data	*cd;
	struct vr_list_data	*ld;
	struct vr_desc		*d;
	int			 i, nexti;

	cd = &sc->vr_cdata;
	ld = sc->vr_ldata;

	for (i = 0; i < VR_RX_LIST_CNT; i++) {
		if (bus_dmamap_create(sc->sc_dmat, MCLBYTES, 1, MCLBYTES,
		    0, BUS_DMA_NOWAIT | BUS_DMA_READ,
		    &cd->vr_rx_chain[i].vr_map))
			return (ENOBUFS);

		d = (struct vr_desc *)&ld->vr_rx_list[i];
		cd->vr_rx_chain[i].vr_ptr = d;
		cd->vr_rx_chain[i].vr_paddr =
		    sc->sc_listmap.vrm_map->dm_segs[0].ds_addr +
		    offsetof(struct vr_list_data, vr_rx_list[i]);

		if (i == (VR_RX_LIST_CNT - 1))
			nexti = 0;
		else
			nexti = i + 1;

		cd->vr_rx_chain[i].vr_nextdesc = &cd->vr_rx_chain[nexti];
		ld->vr_rx_list[i].vr_next =
		    htole32(sc->sc_listmap.vrm_map->dm_segs[0].ds_addr +
		    offsetof(struct vr_list_data, vr_rx_list[nexti]));
	}

	cd->vr_rx_prod = cd->vr_rx_cons = &cd->vr_rx_chain[0];
	if_rxr_init(&sc->sc_rxring, 2, VR_RX_LIST_CNT - 1);
	vr_fill_rx_ring(sc);

	return (0);
}

void
vr_fill_rx_ring(struct vr_softc *sc)
{
	struct vr_chain_data	*cd;
	struct vr_list_data	*ld;
	u_int			slots;

	cd = &sc->vr_cdata;
	ld = sc->vr_ldata;

	for (slots = if_rxr_get(&sc->sc_rxring, VR_RX_LIST_CNT);
	    slots > 0; slots--) {
		if (vr_alloc_mbuf(sc, cd->vr_rx_prod))
			break;

		cd->vr_rx_prod = cd->vr_rx_prod->vr_nextdesc;
	}

	if_rxr_put(&sc->sc_rxring, slots);
	if (if_rxr_inuse(&sc->sc_rxring) == 0)
		timeout_add(&sc->sc_rxto, 0);
}

/*
 * A frame has been uploaded: pass the resulting mbuf chain up to
 * the higher level protocols.
 */
void
vr_rxeof(struct vr_softc *sc)
{
	struct mbuf		*m;
	struct mbuf_list 	ml = MBUF_LIST_INITIALIZER();
	struct ifnet		*ifp;
	struct vr_chain_onefrag	*cur_rx;
	int			total_len = 0;
	u_int32_t		rxstat, rxctl;

	ifp = &sc->arpcom.ac_if;

	while (if_rxr_inuse(&sc->sc_rxring) > 0) {
		bus_dmamap_sync(sc->sc_dmat, sc->sc_listmap.vrm_map,
		    0, sc->sc_listmap.vrm_map->dm_mapsize,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
		rxstat = letoh32(sc->vr_cdata.vr_rx_cons->vr_ptr->vr_status);
		if (rxstat & VR_RXSTAT_OWN)
			break;

		rxctl = letoh32(sc->vr_cdata.vr_rx_cons->vr_ptr->vr_ctl);

		cur_rx = sc->vr_cdata.vr_rx_cons;
		m = cur_rx->vr_mbuf;
		cur_rx->vr_mbuf = NULL;
		sc->vr_cdata.vr_rx_cons = cur_rx->vr_nextdesc;
		if_rxr_put(&sc->sc_rxring, 1);

		/*
		 * If an error occurs, update stats, clear the
		 * status word and leave the mbuf cluster in place:
		 * it should simply get re-used next time this descriptor
		 * comes up in the ring.
		 */
		if ((rxstat & VR_RXSTAT_RX_OK) == 0) {
			ifp->if_ierrors++;
#ifdef VR_DEBUG
			printf("%s: rx error (%02x):",
			    sc->sc_dev.dv_xname, rxstat & 0x000000ff);
			if (rxstat & VR_RXSTAT_CRCERR)
				printf(" crc error");
			if (rxstat & VR_RXSTAT_FRAMEALIGNERR)
				printf(" frame alignment error");
			if (rxstat & VR_RXSTAT_FIFOOFLOW)
				printf(" FIFO overflow");
			if (rxstat & VR_RXSTAT_GIANT)
				printf(" received giant packet");
			if (rxstat & VR_RXSTAT_RUNT)
				printf(" received runt packet");
			if (rxstat & VR_RXSTAT_BUSERR)
				printf(" system bus error");
			if (rxstat & VR_RXSTAT_BUFFERR)
				printf(" rx buffer error");
			printf("\n");
#endif

			m_freem(m);
			continue;
		}

		/* No errors; receive the packet. */
		total_len = VR_RXBYTES(letoh32(cur_rx->vr_ptr->vr_status));

		bus_dmamap_sync(sc->sc_dmat, cur_rx->vr_map, 0,
		    cur_rx->vr_map->dm_mapsize, BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->sc_dmat, cur_rx->vr_map);

		/*
		 * The VIA Rhine chip includes the CRC with every
		 * received frame, and there's no way to turn this
		 * behavior off so trim the CRC manually.
		 */
		total_len -= ETHER_CRC_LEN;

#ifdef __STRICT_ALIGNMENT
		{
			struct mbuf *m0;
			m0 = m_devget(mtod(m, caddr_t), total_len, ETHER_ALIGN);
			m_freem(m);
			if (m0 == NULL) {
				ifp->if_ierrors++;
				continue;
			}
			m = m0;
		} 
#else
		m->m_pkthdr.len = m->m_len = total_len;
#endif

		if (sc->vr_quirks & VR_Q_CSUM &&
		    (rxstat & VR_RXSTAT_FRAG) == 0 &&
		    (rxctl & VR_RXCTL_IP) != 0) {
			/* Checksum is valid for non-fragmented IP packets. */
			if ((rxctl & VR_RXCTL_IPOK) == VR_RXCTL_IPOK)
				m->m_pkthdr.csum_flags |= M_IPV4_CSUM_IN_OK;
			if (rxctl & (VR_RXCTL_TCP | VR_RXCTL_UDP) &&
			    ((rxctl & VR_RXCTL_TCPUDPOK) != 0))
				m->m_pkthdr.csum_flags |= M_TCP_CSUM_IN_OK |
				    M_UDP_CSUM_IN_OK;
		}

#if NVLAN > 0
		/*
		 * If there's a tagged packet, the 802.1q header will be at the
		 * 4-byte boundary following the CRC.  There will be 2 bytes
		 * TPID (0x8100) and 2 bytes TCI (including VLAN ID).
		 * This isn't in the data sheet.
		 */
		if (rxctl & VR_RXCTL_TAG) {
			int offset = ((total_len + 3) & ~3) + ETHER_CRC_LEN + 2;
			m->m_pkthdr.ether_vtag = htons(*(u_int16_t *)
			    ((u_int8_t *)m->m_data + offset));
			m->m_flags |= M_VLANTAG;
		}
#endif

		ml_enqueue(&ml, m);
	}

	if (ifiq_input(&ifp->if_rcv, &ml))
		if_rxr_livelocked(&sc->sc_rxring);

	vr_fill_rx_ring(sc);

	bus_dmamap_sync(sc->sc_dmat, sc->sc_listmap.vrm_map,
	    0, sc->sc_listmap.vrm_map->dm_mapsize,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

void
vr_rxeoc(struct vr_softc *sc)
{
	struct ifnet		*ifp;
	int			i;

	ifp = &sc->arpcom.ac_if;

	ifp->if_ierrors++;

	VR_CLRBIT16(sc, VR_COMMAND, VR_CMD_RX_ON);
	DELAY(10000);

	for (i = 0x400;
	    i && (CSR_READ_2(sc, VR_COMMAND) & VR_CMD_RX_ON);
	    i--)
		;       /* Wait for receiver to stop */

	if (!i) {
		printf("%s: rx shutdown error!\n", sc->sc_dev.dv_xname);
		sc->vr_flags |= VR_F_RESTART;
		return;
	}

	vr_rxeof(sc);

	CSR_WRITE_4(sc, VR_RXADDR, sc->vr_cdata.vr_rx_cons->vr_paddr);
	VR_SETBIT16(sc, VR_COMMAND, VR_CMD_RX_ON);
	VR_SETBIT16(sc, VR_COMMAND, VR_CMD_RX_GO);
}

/*
 * A frame was downloaded to the chip. It's safe for us to clean up
 * the list buffers.
 */

void
vr_txeof(struct vr_softc *sc)
{
	struct vr_chain		*cur_tx;
	struct ifnet		*ifp;

	ifp = &sc->arpcom.ac_if;

	/*
	 * Go through our tx list and free mbufs for those
	 * frames that have been transmitted.
	 */
	cur_tx = sc->vr_cdata.vr_tx_cons;
	while (cur_tx != sc->vr_cdata.vr_tx_prod) {
		u_int32_t		txstat, txctl;
		int			i;

		txstat = letoh32(cur_tx->vr_ptr->vr_status);
		txctl = letoh32(cur_tx->vr_ptr->vr_ctl);

		if ((txstat & VR_TXSTAT_ABRT) ||
		    (txstat & VR_TXSTAT_UDF)) {
			for (i = 0x400;
			    i && (CSR_READ_2(sc, VR_COMMAND) & VR_CMD_TX_ON);
			    i--)
				;	/* Wait for chip to shutdown */
			if (!i) {
				printf("%s: tx shutdown timeout\n",
				    sc->sc_dev.dv_xname);
				sc->vr_flags |= VR_F_RESTART;
				break;
			}
			cur_tx->vr_ptr->vr_status = htole32(VR_TXSTAT_OWN);
			CSR_WRITE_4(sc, VR_TXADDR, cur_tx->vr_paddr);
			break;
		}

		if (txstat & VR_TXSTAT_OWN)
			break;

		sc->vr_cdata.vr_tx_cnt--;
		/* Only the first descriptor in the chain is valid. */
		if ((txctl & VR_TXCTL_FIRSTFRAG) == 0)
			goto next;

		if (txstat & VR_TXSTAT_ERRSUM) {
			ifp->if_oerrors++;
			if (txstat & VR_TXSTAT_DEFER)
				ifp->if_collisions++;
			if (txstat & VR_TXSTAT_LATECOLL)
				ifp->if_collisions++;
		}

		ifp->if_collisions +=(txstat & VR_TXSTAT_COLLCNT) >> 3;

		if (cur_tx->vr_map != NULL && cur_tx->vr_map->dm_nsegs > 0)
			bus_dmamap_unload(sc->sc_dmat, cur_tx->vr_map);

		m_freem(cur_tx->vr_mbuf);
		cur_tx->vr_mbuf = NULL;
		ifq_clr_oactive(&ifp->if_snd);

next:
		cur_tx = cur_tx->vr_nextdesc;
	}

	sc->vr_cdata.vr_tx_cons = cur_tx;
	if (sc->vr_cdata.vr_tx_cnt == 0)
		ifp->if_timer = 0;
}

void
vr_tick(void *xsc)
{
	struct vr_softc *sc = xsc;
	int s;

	s = splnet();
	if (sc->vr_flags & VR_F_RESTART) {
		printf("%s: restarting\n", sc->sc_dev.dv_xname);
		vr_init(sc);
		sc->vr_flags &= ~VR_F_RESTART;
	}

	mii_tick(&sc->sc_mii);
	timeout_add_sec(&sc->sc_to, 1);
	splx(s);
}

void
vr_rxtick(void *xsc)
{
	struct vr_softc *sc = xsc;
	int s;

	s = splnet();
	if (if_rxr_inuse(&sc->sc_rxring) == 0) {
		vr_fill_rx_ring(sc);
		if (if_rxr_inuse(&sc->sc_rxring) == 0)
			timeout_add(&sc->sc_rxto, 1);
	}
	splx(s);
}

int
vr_intr(void *arg)
{
	struct vr_softc		*sc;
	struct ifnet		*ifp;
	u_int16_t		status;
	int claimed = 0;

	sc = arg;
	ifp = &sc->arpcom.ac_if;

	/* Suppress unwanted interrupts. */
	if (!(ifp->if_flags & IFF_UP)) {
		vr_stop(sc);
		return 0;
	}

	status = CSR_READ_2(sc, VR_ISR);
	if (status)
		CSR_WRITE_2(sc, VR_ISR, status);

	if (status & VR_INTRS) {
		claimed = 1;

		if (status & VR_ISR_RX_OK)
			vr_rxeof(sc);

		if (status & VR_ISR_RX_DROPPED) {
#ifdef VR_DEBUG
			printf("%s: rx packet lost\n", sc->sc_dev.dv_xname);
#endif
			ifp->if_ierrors++;
		}

		if ((status & VR_ISR_RX_ERR) || (status & VR_ISR_RX_NOBUF) ||
		    (status & VR_ISR_RX_OFLOW)) {
#ifdef VR_DEBUG
			printf("%s: receive error (%04x)",
			    sc->sc_dev.dv_xname, status);
			if (status & VR_ISR_RX_NOBUF)
				printf(" no buffers");
			if (status & VR_ISR_RX_OFLOW)
				printf(" overflow");
			printf("\n");
#endif
			vr_rxeoc(sc);
		}

		if ((status & VR_ISR_BUSERR) || (status & VR_ISR_TX_UNDERRUN)) {
			if (status & VR_ISR_BUSERR)
				printf("%s: PCI bus error\n",
				    sc->sc_dev.dv_xname);
			if (status & VR_ISR_TX_UNDERRUN)
				printf("%s: transmit underrun\n",
				    sc->sc_dev.dv_xname);
			vr_init(sc);
			status = 0;
		}

		if ((status & VR_ISR_TX_OK) || (status & VR_ISR_TX_ABRT) ||
		    (status & VR_ISR_TX_ABRT2) || (status & VR_ISR_UDFI)) {
			vr_txeof(sc);
			if ((status & VR_ISR_UDFI) ||
			    (status & VR_ISR_TX_ABRT2) ||
			    (status & VR_ISR_TX_ABRT)) {
#ifdef VR_DEBUG
				if (status & (VR_ISR_TX_ABRT | VR_ISR_TX_ABRT2))
					printf("%s: transmit aborted\n",
					    sc->sc_dev.dv_xname);
				if (status & VR_ISR_UDFI)
					printf("%s: transmit underflow\n",
					    sc->sc_dev.dv_xname);
#endif
				ifp->if_oerrors++;
				if (sc->vr_cdata.vr_tx_cons->vr_mbuf != NULL) {
					VR_SETBIT16(sc, VR_COMMAND,
					    VR_CMD_TX_ON);
					VR_SETBIT16(sc, VR_COMMAND,
					    VR_CMD_TX_GO);
				}
			}
		}
	}

	if (!ifq_empty(&ifp->if_snd))
		vr_start(ifp);

	return (claimed);
}

/*
 * Encapsulate an mbuf chain in a descriptor by coupling the mbuf data
 * pointers to the fragment pointers.
 */
int
vr_encap(struct vr_softc *sc, struct vr_chain **cp, struct mbuf *m)
{
	struct vr_chain		*c = *cp;
	struct vr_desc		*f = NULL;
	u_int32_t		vr_ctl = 0, vr_status = 0, intdisable = 0;
	bus_dmamap_t		txmap;
	int			i, runt = 0;
	int			error;

	if (sc->vr_quirks & VR_Q_CSUM) {
		if (m->m_pkthdr.csum_flags & M_IPV4_CSUM_OUT)
			vr_ctl |= VR_TXCTL_IPCSUM;
		if (m->m_pkthdr.csum_flags & M_TCP_CSUM_OUT)
			vr_ctl |= VR_TXCTL_TCPCSUM;
		if (m->m_pkthdr.csum_flags & M_UDP_CSUM_OUT)
			vr_ctl |= VR_TXCTL_UDPCSUM;
	}

	if (sc->vr_quirks & VR_Q_NEEDALIGN) {
		/* Deep copy for chips that need alignment */
		error = EFBIG;
	} else {
		error = bus_dmamap_load_mbuf(sc->sc_dmat, c->vr_map, m,
		    BUS_DMA_NOWAIT | BUS_DMA_WRITE);
	}

	switch (error) {
	case 0:
		break;
	case EFBIG:
		if (m_defrag(m, M_DONTWAIT) == 0 &&
                    bus_dmamap_load_mbuf(sc->sc_dmat, c->vr_map, m,
                     BUS_DMA_NOWAIT) == 0)
                        break;

		/* FALLTHROUGH */
        default:
		return (ENOBUFS);
        }

	bus_dmamap_sync(sc->sc_dmat, c->vr_map, 0, c->vr_map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);
	if (c->vr_map->dm_mapsize < VR_MIN_FRAMELEN)
		runt = 1;

#if NVLAN > 0
	/*
	 * Tell chip to insert VLAN tag if needed.
	 * This chip expects the VLAN ID (0x0FFF) and the PCP (0xE000)
	 * in only 15 bits without the gap at 0x1000 (reserved for DEI).
	 * Therefore we need to de- / re-construct the VLAN header.
	 */
	if (m->m_flags & M_VLANTAG) {
		u_int32_t vtag = m->m_pkthdr.ether_vtag;
		vtag = EVL_VLANOFTAG(vtag) | EVL_PRIOFTAG(vtag) << 12;
		vr_status |= vtag << VR_TXSTAT_PQSHIFT;
		vr_ctl |= htole32(VR_TXCTL_INSERTTAG);
	}
#endif

	/*
	 * We only want TX completion interrupts on every Nth packet.
	 * We need to set VR_TXNEXT_INTDISABLE on every descriptor except
	 * for the last descriptor of every Nth packet, where we set
	 * VR_TXCTL_FINT.  The former is in the specs for only some chips.
	 * present: VT6102 VT6105M VT8235M
	 * not present: VT86C100 6105LOM
	 */
	if (++sc->vr_cdata.vr_tx_pkts % VR_TX_INTR_THRESH != 0 &&
	    sc->vr_quirks & VR_Q_INTDISABLE)
		intdisable = VR_TXNEXT_INTDISABLE;

	c->vr_mbuf = m;
	txmap = c->vr_map;
	for (i = 0; i < txmap->dm_nsegs; i++) {
		if (i != 0)
			*cp = c = c->vr_nextdesc;
		f = c->vr_ptr;
		f->vr_ctl = htole32(txmap->dm_segs[i].ds_len | VR_TXCTL_TLINK |
		    vr_ctl);
		if (i == 0)
			f->vr_ctl |= htole32(VR_TXCTL_FIRSTFRAG);
		f->vr_status = htole32(vr_status);
		f->vr_data = htole32(txmap->dm_segs[i].ds_addr);
		f->vr_next = htole32(c->vr_nextdesc->vr_paddr | intdisable);
		sc->vr_cdata.vr_tx_cnt++;
	}

	/* Pad runt frames */
	if (runt) {
		*cp = c = c->vr_nextdesc;
		f = c->vr_ptr;
		f->vr_ctl = htole32((VR_MIN_FRAMELEN - txmap->dm_mapsize) |
		    VR_TXCTL_TLINK | vr_ctl);
		f->vr_status = htole32(vr_status);
		f->vr_data = htole32(sc->sc_zeromap.vrm_map->dm_segs[0].ds_addr);
		f->vr_next = htole32(c->vr_nextdesc->vr_paddr | intdisable);
		sc->vr_cdata.vr_tx_cnt++;
	}

	/* Set EOP on the last descriptor */
	f->vr_ctl |= htole32(VR_TXCTL_LASTFRAG);

	if (sc->vr_cdata.vr_tx_pkts % VR_TX_INTR_THRESH == 0)
		f->vr_ctl |= htole32(VR_TXCTL_FINT);

	return (0);
}

/*
 * Main transmit routine. To avoid having to do mbuf copies, we put pointers
 * to the mbuf data regions directly in the transmit lists. We also save a
 * copy of the pointers since the transmit list fragment pointers are
 * physical addresses.
 */

void
vr_start(struct ifnet *ifp)
{
	struct vr_softc		*sc;
	struct mbuf		*m;
	struct vr_chain		*cur_tx, *head_tx;
	unsigned int		 queued = 0;

	sc = ifp->if_softc;

	if (!(ifp->if_flags & IFF_RUNNING) || ifq_is_oactive(&ifp->if_snd))
		return;

	if (sc->vr_link == 0)
		return;

	cur_tx = sc->vr_cdata.vr_tx_prod;
	for (;;) {
		if (sc->vr_cdata.vr_tx_cnt + VR_MAXFRAGS >=
		    VR_TX_LIST_CNT - 1) {
			ifq_set_oactive(&ifp->if_snd);
			break;
		}

		m = ifq_dequeue(&ifp->if_snd);
		if (m == NULL)
			break;

		/* Pack the data into the descriptor. */
		head_tx = cur_tx;
		if (vr_encap(sc, &cur_tx, m)) {
			m_freem(m);
			ifp->if_oerrors++;
			continue;
		}
		queued++;

		/* Only set ownership bit on first descriptor */
		head_tx->vr_ptr->vr_status |= htole32(VR_TXSTAT_OWN);

#if NBPFILTER > 0
		/*
		 * If there's a BPF listener, bounce a copy of this frame
		 * to him.
		 */
		if (ifp->if_bpf)
			bpf_mtap_ether(ifp->if_bpf, m, BPF_DIRECTION_OUT);
#endif
		cur_tx = cur_tx->vr_nextdesc;
	}
	if (queued > 0) {
		sc->vr_cdata.vr_tx_prod = cur_tx;

		bus_dmamap_sync(sc->sc_dmat, sc->sc_listmap.vrm_map, 0,
		    sc->sc_listmap.vrm_map->dm_mapsize,
		    BUS_DMASYNC_PREWRITE|BUS_DMASYNC_PREREAD);

		/* Tell the chip to start transmitting. */
		VR_SETBIT16(sc, VR_COMMAND, /*VR_CMD_TX_ON|*/VR_CMD_TX_GO);

		/* Set a timeout in case the chip goes out to lunch. */
		ifp->if_timer = 5;
	}
}

void
vr_chipinit(struct vr_softc *sc)
{
	/*
	 * Make sure it isn't suspended.
	 */
	if (pci_get_capability(sc->sc_pc, sc->sc_tag,
	    PCI_CAP_PWRMGMT, NULL, NULL))
		VR_CLRBIT(sc, VR_STICKHW, (VR_STICKHW_DS0|VR_STICKHW_DS1));

	/* Reset the adapter. */
	vr_reset(sc);

	/*
	 * Turn on bit2 (MIION) in PCI configuration register 0x53 during
	 * initialization and disable AUTOPOLL.
	 */
	pci_conf_write(sc->sc_pc, sc->sc_tag, VR_PCI_MODE,
	    pci_conf_read(sc->sc_pc, sc->sc_tag, VR_PCI_MODE) |
	    (VR_MODE3_MIION << 24));
	VR_CLRBIT(sc, VR_MIICMD, VR_MIICMD_AUTOPOLL);
}

void
vr_init(void *xsc)
{
	struct vr_softc		*sc = xsc;
	struct ifnet		*ifp = &sc->arpcom.ac_if;
	struct mii_data		*mii = &sc->sc_mii;
	int			s, i;

	s = splnet();

	/*
	 * Cancel pending I/O and free all RX/TX buffers.
	 */
	vr_stop(sc);
	vr_chipinit(sc);

	/*
	 * Set our station address.
	 */
	for (i = 0; i < ETHER_ADDR_LEN; i++)
		CSR_WRITE_1(sc, VR_PAR0 + i, sc->arpcom.ac_enaddr[i]);

	/* Set DMA size */
	VR_CLRBIT(sc, VR_BCR0, VR_BCR0_DMA_LENGTH);
	VR_SETBIT(sc, VR_BCR0, VR_BCR0_DMA_STORENFWD);

	/*
	 * BCR0 and BCR1 can override the RXCFG and TXCFG registers,
	 * so we must set both.
	 */
	VR_CLRBIT(sc, VR_BCR0, VR_BCR0_RX_THRESH);
	VR_SETBIT(sc, VR_BCR0, VR_BCR0_RXTHRESH128BYTES);

	VR_CLRBIT(sc, VR_BCR1, VR_BCR1_TX_THRESH);
	VR_SETBIT(sc, VR_BCR1, VR_BCR1_TXTHRESHSTORENFWD);

	VR_CLRBIT(sc, VR_RXCFG, VR_RXCFG_RX_THRESH);
	VR_SETBIT(sc, VR_RXCFG, VR_RXTHRESH_128BYTES);

	VR_CLRBIT(sc, VR_TXCFG, VR_TXCFG_TX_THRESH);
	VR_SETBIT(sc, VR_TXCFG, VR_TXTHRESH_STORENFWD);

	if (ifp->if_capabilities & IFCAP_VLAN_HWTAGGING)
		VR_SETBIT(sc, VR_TXCFG, VR_TXCFG_TXTAGEN);

	/* Init circular RX list. */
	if (vr_list_rx_init(sc) == ENOBUFS) {
		printf("%s: initialization failed: no memory for rx buffers\n",
		    sc->sc_dev.dv_xname);
		vr_stop(sc);
		splx(s);
		return;
	}

	/*
	 * Init tx descriptors.
	 */
	if (vr_list_tx_init(sc) == ENOBUFS) {
		printf("%s: initialization failed: no memory for tx buffers\n",
		    sc->sc_dev.dv_xname);
		vr_stop(sc);
		splx(s);
		return;
	}

	/*
	 * Program promiscuous mode and multicast filters.
	 */
	vr_iff(sc);

	/*
	 * Load the address of the RX list.
	 */
	CSR_WRITE_4(sc, VR_RXADDR, sc->vr_cdata.vr_rx_cons->vr_paddr);

	/* Enable receiver and transmitter. */
	CSR_WRITE_2(sc, VR_COMMAND, VR_CMD_TX_NOPOLL|VR_CMD_START|
				    VR_CMD_TX_ON|VR_CMD_RX_ON|
				    VR_CMD_RX_GO);

	CSR_WRITE_4(sc, VR_TXADDR, sc->sc_listmap.vrm_map->dm_segs[0].ds_addr +
	    offsetof(struct vr_list_data, vr_tx_list[0]));

	/*
	 * Enable interrupts.
	 */
	CSR_WRITE_2(sc, VR_ISR, 0xFFFF);
	CSR_WRITE_2(sc, VR_IMR, VR_INTRS);

	/* Restore state of BMCR */
	sc->vr_link = 1;
	mii_mediachg(mii);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	if (!timeout_pending(&sc->sc_to))
		timeout_add_sec(&sc->sc_to, 1);

	splx(s);
}

/*
 * Set media options.
 */
int
vr_ifmedia_upd(struct ifnet *ifp)
{
	struct vr_softc		*sc = ifp->if_softc;

	if (ifp->if_flags & IFF_UP)
		vr_init(sc);

	return (0);
}

/*
 * Report current media status.
 */
void
vr_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct vr_softc		*sc = ifp->if_softc;
	struct mii_data		*mii = &sc->sc_mii;

	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
}

int
vr_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct vr_softc		*sc = ifp->if_softc;
	struct ifreq		*ifr = (struct ifreq *) data;
	int			s, error = 0;

	s = splnet();

	switch(command) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		if (!(ifp->if_flags & IFF_RUNNING))
			vr_init(sc);
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				error = ENETRESET;
			else
				vr_init(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				vr_stop(sc);
		}
		break;

	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_mii.mii_media, command);
		break;

	case SIOCGIFRXR:
		error = if_rxr_ioctl((struct if_rxrinfo *)ifr->ifr_data,
		    NULL, MCLBYTES, &sc->sc_rxring);
 		break;

	default:
		error = ether_ioctl(ifp, &sc->arpcom, command, data);
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			vr_iff(sc);
		error = 0;
	}

	splx(s);
	return(error);
}

void
vr_watchdog(struct ifnet *ifp)
{
	struct vr_softc		*sc;

	sc = ifp->if_softc;

	/*
	 * Since we're only asking for completion interrupts only every
	 * few packets, occasionally the watchdog will fire when we have
	 * some TX descriptors to reclaim, so check for that first.
	 */
	vr_txeof(sc);
	if (sc->vr_cdata.vr_tx_cnt == 0)
		return;

	ifp->if_oerrors++;
	printf("%s: watchdog timeout\n", sc->sc_dev.dv_xname);
	vr_init(sc);

	if (!ifq_empty(&ifp->if_snd))
		vr_start(ifp);
}

/*
 * Stop the adapter and free any mbufs allocated to the
 * RX and TX lists.
 */
void
vr_stop(struct vr_softc *sc)
{
	int		i;
	struct ifnet	*ifp;
	bus_dmamap_t	map;

	ifp = &sc->arpcom.ac_if;
	ifp->if_timer = 0;

	timeout_del(&sc->sc_to);
	timeout_del(&sc->sc_rxto);

	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	VR_SETBIT16(sc, VR_COMMAND, VR_CMD_STOP);
	VR_CLRBIT16(sc, VR_COMMAND, (VR_CMD_RX_ON|VR_CMD_TX_ON));

	/* wait for xfers to shutdown */
	for (i = VR_TIMEOUT; i > 0; i--) {
		DELAY(10);
		if (!(CSR_READ_2(sc, VR_COMMAND) & (VR_CMD_TX_ON|VR_CMD_RX_ON)))
			break;
	}
#ifdef VR_DEBUG
	if (i == 0)
		printf("%s: rx shutdown error!\n", sc->sc_dev.dv_xname);
#endif
	CSR_WRITE_2(sc, VR_IMR, 0x0000);
	CSR_WRITE_4(sc, VR_TXADDR, 0x00000000);
	CSR_WRITE_4(sc, VR_RXADDR, 0x00000000);

	/*
	 * Free data in the RX lists.
	 */
	for (i = 0; i < VR_RX_LIST_CNT; i++) {
		if (sc->vr_cdata.vr_rx_chain[i].vr_mbuf != NULL) {
			m_freem(sc->vr_cdata.vr_rx_chain[i].vr_mbuf);
			sc->vr_cdata.vr_rx_chain[i].vr_mbuf = NULL;
		}
		map = sc->vr_cdata.vr_rx_chain[i].vr_map;
		if (map != NULL) {
			if (map->dm_nsegs > 0)
				bus_dmamap_unload(sc->sc_dmat, map);
			bus_dmamap_destroy(sc->sc_dmat, map);
			sc->vr_cdata.vr_rx_chain[i].vr_map = NULL;
		}
	}
	bzero(&sc->vr_ldata->vr_rx_list, sizeof(sc->vr_ldata->vr_rx_list));

	/*
	 * Free the TX list buffers.
	 */
	for (i = 0; i < VR_TX_LIST_CNT; i++) {
		if (sc->vr_cdata.vr_tx_chain[i].vr_mbuf != NULL) {
			m_freem(sc->vr_cdata.vr_tx_chain[i].vr_mbuf);
			sc->vr_cdata.vr_tx_chain[i].vr_mbuf = NULL;
			ifp->if_oerrors++;
		}
		map = sc->vr_cdata.vr_tx_chain[i].vr_map;
		if (map != NULL) {
			if (map->dm_nsegs > 0)
				bus_dmamap_unload(sc->sc_dmat, map);
			bus_dmamap_destroy(sc->sc_dmat, map);
			sc->vr_cdata.vr_tx_chain[i].vr_map = NULL;
		}
	}
	bzero(&sc->vr_ldata->vr_tx_list, sizeof(sc->vr_ldata->vr_tx_list));
}

#ifndef SMALL_KERNEL
int
vr_wol(struct ifnet *ifp, int enable)
{
	struct vr_softc *sc = ifp->if_softc;

	/* Clear WOL configuration */
	CSR_WRITE_1(sc, VR_WOLCRCLR, 0xFF);

	/* Clear event status bits. */
	CSR_WRITE_1(sc, VR_PWRCSRCLR, 0xFF);

	/* Disable PME# assertion upon wake event. */
	VR_CLRBIT(sc, VR_STICKHW, VR_STICKHW_WOL_ENB);
	VR_SETBIT(sc, VR_WOLCFGCLR, VR_WOLCFG_PMEOVR);

	if (enable) {
		VR_SETBIT(sc, VR_WOLCRSET, VR_WOLCR_MAGIC);

		/* Enable PME# assertion upon wake event. */
		VR_SETBIT(sc, VR_STICKHW, VR_STICKHW_WOL_ENB);
		VR_SETBIT(sc, VR_WOLCFGSET, VR_WOLCFG_PMEOVR);
	}

	return (0);
}
#endif

int
vr_alloc_mbuf(struct vr_softc *sc, struct vr_chain_onefrag *r)
{
	struct vr_desc	*d;
	struct mbuf	*m;

	if (r == NULL)
		return (EINVAL);

	m = MCLGETL(NULL, M_DONTWAIT, MCLBYTES);
	if (!m)
		return (ENOBUFS);

	m->m_len = m->m_pkthdr.len = MCLBYTES;
	m_adj(m, sizeof(u_int64_t));

	if (bus_dmamap_load_mbuf(sc->sc_dmat, r->vr_map, m, BUS_DMA_NOWAIT)) {
		m_free(m);
		return (ENOBUFS);
	}

	bus_dmamap_sync(sc->sc_dmat, r->vr_map, 0, r->vr_map->dm_mapsize,
	    BUS_DMASYNC_PREREAD);

	/* Reinitialize the RX descriptor */
	r->vr_mbuf = m;
	d = r->vr_ptr;
	d->vr_data = htole32(r->vr_map->dm_segs[0].ds_addr);
	if (sc->vr_quirks & VR_Q_BABYJUMBO)
		d->vr_ctl = htole32(VR_RXCTL | VR_RXLEN_BABYJUMBO);
	else
		d->vr_ctl = htole32(VR_RXCTL | VR_RXLEN);

	bus_dmamap_sync(sc->sc_dmat, sc->sc_listmap.vrm_map, 0,
	    sc->sc_listmap.vrm_map->dm_mapsize, BUS_DMASYNC_PREWRITE);

	d->vr_status = htole32(VR_RXSTAT);

	bus_dmamap_sync(sc->sc_dmat, sc->sc_listmap.vrm_map, 0,
	    sc->sc_listmap.vrm_map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);

	return (0);
}
