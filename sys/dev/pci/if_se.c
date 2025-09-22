/*	$OpenBSD: if_se.c,v 1.27 2024/11/05 18:58:59 miod Exp $	*/

/*-
 * Copyright (c) 2009, 2010 Christopher Zimmermann <madroach@zakweb.de>
 * Copyright (c) 2008, 2009, 2010 Nikolay Denev <ndenev@gmail.com>
 * Copyright (c) 2007, 2008 Alexander Pohoyda <alexander.pohoyda@gmx.net>
 * Copyright (c) 1997, 1998, 1999
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
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL AUTHORS OR
 * THE VOICES IN THEIR HEADS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * SiS 190/191 PCI Ethernet NIC driver.
 *
 * Adapted to SiS 190 NIC by Alexander Pohoyda based on the original
 * SiS 900 driver by Bill Paul, using SiS 190/191 Solaris driver by
 * Masayuki Murayama and SiS 190/191 GNU/Linux driver by K.M. Liu
 * <kmliu@sis.com>.  Thanks to Pyun YongHyeon <pyunyh@gmail.com> for
 * review and very useful comments.
 *
 * Ported to OpenBSD by Christopher Zimmermann 2009/10
 *
 * Adapted to SiS 191 NIC by Nikolay Denev with further ideas from the
 * Linux and Solaris drivers.
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/ioctl.h>
#include <sys/mbuf.h>
#include <sys/timeout.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <dev/mii/miivar.h>

#include <dev/pci/pcidevs.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/pci/if_sereg.h>

#define SE_RX_RING_CNT		256 /* [8, 1024] */
#define SE_TX_RING_CNT		256 /* [8, 8192] */
#define	SE_RX_BUF_ALIGN		sizeof(uint64_t)

#define SE_RX_RING_SZ		(SE_RX_RING_CNT * sizeof(struct se_desc))
#define SE_TX_RING_SZ		(SE_TX_RING_CNT * sizeof(struct se_desc))

struct se_list_data {
	struct se_desc		*se_rx_ring;
	struct se_desc		*se_tx_ring;
	bus_dmamap_t		se_rx_dmamap;
	bus_dmamap_t		se_tx_dmamap;
};

struct se_chain_data {
	struct mbuf		*se_rx_mbuf[SE_RX_RING_CNT];
	struct mbuf		*se_tx_mbuf[SE_TX_RING_CNT];
	bus_dmamap_t		se_rx_map[SE_RX_RING_CNT];
	bus_dmamap_t		se_tx_map[SE_TX_RING_CNT];
	uint			se_rx_prod;
	uint			se_tx_prod;
	uint			se_tx_cons;
	uint			se_tx_cnt;
};

struct se_softc {
    	struct device		 sc_dev;
	void			*sc_ih;
	bus_space_tag_t		 sc_iot;
	bus_space_handle_t	 sc_ioh;
	bus_dma_tag_t		 sc_dmat;

	struct mii_data		 sc_mii;
	struct arpcom		 sc_ac;

	struct se_list_data	 se_ldata;
	struct se_chain_data	 se_cdata;

	struct timeout		 sc_tick_tmo;

	int			 sc_flags;
#define	SE_FLAG_FASTETHER	0x0001
#define	SE_FLAG_RGMII		0x0010
#define	SE_FLAG_LINK		0x8000
};

/*
 * Various supported device vendors/types and their names.
 */
const struct pci_matchid se_devices[] = {
	{ PCI_VENDOR_SIS, PCI_PRODUCT_SIS_190 },
	{ PCI_VENDOR_SIS, PCI_PRODUCT_SIS_191 }
};

int	se_match(struct device *, void *, void *);
void	se_attach(struct device *, struct device *, void *);
int	se_activate(struct device *, int);

const struct cfattach se_ca = {
	sizeof(struct se_softc),
	se_match, se_attach, NULL, se_activate
};

struct cfdriver se_cd = {
	NULL, "se", DV_IFNET
};

uint32_t
	se_miibus_cmd(struct se_softc *, uint32_t);
int	se_miibus_readreg(struct device *, int, int);
void	se_miibus_writereg(struct device *, int, int, int);
void	se_miibus_statchg(struct device *);

int	se_newbuf(struct se_softc *, uint);
void	se_discard_rxbuf(struct se_softc *, uint);
int	se_encap(struct se_softc *, struct mbuf *, uint *);
void	se_rxeof(struct se_softc *);
void	se_txeof(struct se_softc *);
int	se_intr(void *);
void	se_tick(void *);
void	se_start(struct ifnet *);
int	se_ioctl(struct ifnet *, u_long, caddr_t);
int	se_init(struct ifnet *);
void	se_stop(struct se_softc *);
void	se_watchdog(struct ifnet *);
int	se_ifmedia_upd(struct ifnet *);
void	se_ifmedia_sts(struct ifnet *, struct ifmediareq *);

int	se_pcib_match(struct pci_attach_args *);
int	se_get_mac_addr_apc(struct se_softc *, uint8_t *);
int	se_get_mac_addr_eeprom(struct se_softc *, uint8_t *);
uint16_t
	se_read_eeprom(struct se_softc *, int);

void	se_iff(struct se_softc *);
void	se_reset(struct se_softc *);
int	se_list_rx_init(struct se_softc *);
int	se_list_rx_free(struct se_softc *);
int	se_list_tx_init(struct se_softc *);
int	se_list_tx_free(struct se_softc *);

/*
 * Register space access macros.
 */

#define	CSR_WRITE_4(sc, reg, val) \
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, reg, val)
#define	CSR_WRITE_2(sc, reg, val) \
	bus_space_write_2((sc)->sc_iot, (sc)->sc_ioh, reg, val)
#define	CSR_WRITE_1(sc, reg, val) \
	bus_space_write_1((sc)->sc_iot, (sc)->sc_ioh, reg, val)

#define	CSR_READ_4(sc, reg) \
	bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, reg)
#define	CSR_READ_2(sc, reg) \
	bus_space_read_2((sc)->sc_iot, (sc)->sc_ioh, reg)
#define	CSR_READ_1(sc, reg) \
	bus_space_read_1((sc)->sc_iot, (sc)->sc_ioh, reg)

/*
 * Read a sequence of words from the EEPROM.
 */
uint16_t
se_read_eeprom(struct se_softc *sc, int offset)
{
	uint32_t val;
	int i;

	KASSERT(offset <= EI_OFFSET);

	CSR_WRITE_4(sc, ROMInterface,
	    EI_REQ | EI_OP_RD | (offset << EI_OFFSET_SHIFT));
	DELAY(500);
	for (i = 0; i < SE_TIMEOUT; i++) {
		val = CSR_READ_4(sc, ROMInterface);
		if ((val & EI_REQ) == 0)
			break;
		DELAY(100);
	}
	if (i == SE_TIMEOUT) {
		printf("%s: EEPROM read timeout: 0x%08x\n",
		    sc->sc_dev.dv_xname, val);
		return 0xffff;
	}

	return (val & EI_DATA) >> EI_DATA_SHIFT;
}

int
se_get_mac_addr_eeprom(struct se_softc *sc, uint8_t *dest)
{
	uint16_t val;
	int i;

	val = se_read_eeprom(sc, EEPROMSignature);
	if (val == 0xffff || val == 0x0000) {
		printf("%s: invalid EEPROM signature : 0x%04x\n",
		    sc->sc_dev.dv_xname, val);
		return (EINVAL);
	}

	for (i = 0; i < ETHER_ADDR_LEN; i += 2) {
		val = se_read_eeprom(sc, EEPROMMACAddr + i / 2);
		dest[i + 0] = (uint8_t)val;
		dest[i + 1] = (uint8_t)(val >> 8);
	}

	if ((se_read_eeprom(sc, EEPROMInfo) & 0x80) != 0)
		sc->sc_flags |= SE_FLAG_RGMII;
	return (0);
}

/*
 * For SiS96x, APC CMOS RAM is used to store Ethernet address.
 * APC CMOS RAM is accessed through ISA bridge.
 */
#if defined(__amd64__) || defined(__i386__)
int
se_pcib_match(struct pci_attach_args *pa)
{
	const struct pci_matchid apc_devices[] = {
		{ PCI_VENDOR_SIS, PCI_PRODUCT_SIS_965 },
		{ PCI_VENDOR_SIS, PCI_PRODUCT_SIS_966 },
		{ PCI_VENDOR_SIS, PCI_PRODUCT_SIS_968 }
	};

	return pci_matchbyid(pa, apc_devices, nitems(apc_devices));
}
#endif

int
se_get_mac_addr_apc(struct se_softc *sc, uint8_t *dest)
{
#if defined(__amd64__) || defined(__i386__)
	struct pci_attach_args pa;
	pcireg_t reg;
	bus_space_handle_t ioh;
	int rc, i;

	if (pci_find_device(&pa, se_pcib_match) == 0) {
		printf("\n%s: couldn't find PCI-ISA bridge\n",
		    sc->sc_dev.dv_xname);
		return EINVAL;
	}

	/* Enable port 0x78 and 0x79 to access APC registers. */
	reg = pci_conf_read(pa.pa_pc, pa.pa_tag, 0x48);
	pci_conf_write(pa.pa_pc, pa.pa_tag, 0x48, reg & ~0x02);
	DELAY(50);
	(void)pci_conf_read(pa.pa_pc, pa.pa_tag, 0x48);

	/* XXX this abuses bus_space implementation knowledge */
	rc = _bus_space_map(pa.pa_iot, 0x78, 2, 0, &ioh);
	if (rc == 0) {
		/* Read stored Ethernet address. */
		for (i = 0; i < ETHER_ADDR_LEN; i++) {
			bus_space_write_1(pa.pa_iot, ioh, 0, 0x09 + i);
			dest[i] = bus_space_read_1(pa.pa_iot, ioh, 1);
		}
		bus_space_write_1(pa.pa_iot, ioh, 0, 0x12);
		if ((bus_space_read_1(pa.pa_iot, ioh, 1) & 0x80) != 0)
			sc->sc_flags |= SE_FLAG_RGMII;
		_bus_space_unmap(pa.pa_iot, ioh, 2, NULL);
	} else
		rc = EINVAL;

	/* Restore access to APC registers. */
	pci_conf_write(pa.pa_pc, pa.pa_tag, 0x48, reg);

	return rc;
#endif
	return EINVAL;
}

uint32_t
se_miibus_cmd(struct se_softc *sc, uint32_t ctrl)
{
	int i;
	uint32_t val;

	CSR_WRITE_4(sc, GMIIControl, ctrl);
	DELAY(10);
	for (i = 0; i < SE_TIMEOUT; i++) {
		val = CSR_READ_4(sc, GMIIControl);
		if ((val & GMI_REQ) == 0)
			return val;
		DELAY(10);
	}

	return GMI_REQ;
}

int
se_miibus_readreg(struct device *self, int phy, int reg)
{
	struct se_softc *sc = (struct se_softc *)self;
	uint32_t ctrl, val;

	ctrl = (phy << GMI_PHY_SHIFT) | (reg << GMI_REG_SHIFT) |
	    GMI_OP_RD | GMI_REQ;
	val = se_miibus_cmd(sc, ctrl);
	if ((val & GMI_REQ) != 0) {
		printf("%s: PHY read timeout : %d\n",
		    sc->sc_dev.dv_xname, reg);
		return 0;
	}
	return (val & GMI_DATA) >> GMI_DATA_SHIFT;
}

void
se_miibus_writereg(struct device *self, int phy, int reg, int data)
{
	struct se_softc *sc = (struct se_softc *)self;
	uint32_t ctrl, val;

	ctrl = (phy << GMI_PHY_SHIFT) | (reg << GMI_REG_SHIFT) |
	    GMI_OP_WR | (data << GMI_DATA_SHIFT) | GMI_REQ;
	val = se_miibus_cmd(sc, ctrl);
	if ((val & GMI_REQ) != 0) {
		printf("%s: PHY write timeout : %d\n",
		    sc->sc_dev.dv_xname, reg);
	}
}

void
se_miibus_statchg(struct device *self)
{
	struct se_softc *sc = (struct se_softc *)self;
#ifdef SE_DEBUG
	struct ifnet *ifp = &sc->sc_ac.ac_if;
#endif
	struct mii_data *mii = &sc->sc_mii;
	uint32_t ctl, speed;

	speed = 0;
	sc->sc_flags &= ~SE_FLAG_LINK;
	if ((mii->mii_media_status & (IFM_ACTIVE | IFM_AVALID)) ==
	    (IFM_ACTIVE | IFM_AVALID)) {
		switch (IFM_SUBTYPE(mii->mii_media_active)) {
		case IFM_10_T:
#ifdef SE_DEBUG
			if (ifp->if_flags & IFF_DEBUG)
				printf("%s: 10baseT link\n", ifp->if_xname);
#endif
			sc->sc_flags |= SE_FLAG_LINK;
			speed = SC_SPEED_10;
			break;
		case IFM_100_TX:
#ifdef SE_DEBUG
			if (ifp->if_flags & IFF_DEBUG)
				printf("%s: 100baseTX link\n", ifp->if_xname);
#endif
			sc->sc_flags |= SE_FLAG_LINK;
			speed = SC_SPEED_100;
			break;
		case IFM_1000_T:
#ifdef SE_DEBUG
			if (ifp->if_flags & IFF_DEBUG)
				printf("%s: 1000baseT link\n", ifp->if_xname);
#endif
			if ((sc->sc_flags & SE_FLAG_FASTETHER) == 0) {
				sc->sc_flags |= SE_FLAG_LINK;
				speed = SC_SPEED_1000;
			}
			break;
		default:
			break;
		}
	}
	if ((sc->sc_flags & SE_FLAG_LINK) == 0) {
#ifdef SE_DEBUG
		if (ifp->if_flags & IFF_DEBUG)
			printf("%s: no link\n", ifp->if_xname);
#endif
		return;
	}
	/* Reprogram MAC to resolved speed/duplex/flow-control parameters. */
	ctl = CSR_READ_4(sc, StationControl);
	ctl &= ~(0x0f000000 | SC_FDX | SC_SPEED_MASK);
	if (speed == SC_SPEED_1000)
		ctl |= 0x07000000;
	else
		ctl |= 0x04000000;
#ifdef notyet
	if ((sc->sc_flags & SE_FLAG_GMII) != 0)
		ctl |= 0x03000000;
#endif
	ctl |= speed;
	if ((IFM_OPTIONS(mii->mii_media_active) & IFM_FDX) != 0)
		ctl |= SC_FDX;
	CSR_WRITE_4(sc, StationControl, ctl);
	if ((sc->sc_flags & SE_FLAG_RGMII) != 0) {
		CSR_WRITE_4(sc, RGMIIDelay, 0x0441);
		CSR_WRITE_4(sc, RGMIIDelay, 0x0440);
	}
}

void
se_iff(struct se_softc *sc)
{
	struct arpcom *ac = &sc->sc_ac;
	struct ifnet *ifp = &ac->ac_if;
	struct ether_multi *enm;
	struct ether_multistep step;
	uint32_t crc, hashes[2];
	uint16_t rxfilt;

	rxfilt = CSR_READ_2(sc, RxMacControl);
	rxfilt &= ~(AcceptAllPhys | AcceptBroadcast | AcceptMulticast);
	ifp->if_flags &= ~IFF_ALLMULTI;

	/*
	 * Always accept broadcast frames.
	 * Always accept frames destined to our station address.
	 */
	rxfilt |= AcceptBroadcast | AcceptMyPhys;

	if (ifp->if_flags & IFF_PROMISC || ac->ac_multirangecnt > 0) {
		ifp->if_flags |= IFF_ALLMULTI;
		if (ifp->if_flags & IFF_PROMISC)
			rxfilt |= AcceptAllPhys;
		rxfilt |= AcceptMulticast;
		hashes[0] = hashes[1] = 0xffffffff;
	} else {
		rxfilt |= AcceptMulticast;
		hashes[0] = hashes[1] = 0;

		ETHER_FIRST_MULTI(step, ac, enm);
		while (enm != NULL) {
			crc = ether_crc32_be(enm->enm_addrlo, ETHER_ADDR_LEN);

			hashes[crc >> 31] |= 1 << ((crc >> 26) & 0x1f);

			ETHER_NEXT_MULTI(step, enm);
		}
	}

	CSR_WRITE_2(sc, RxMacControl, rxfilt);
	CSR_WRITE_4(sc, RxHashTable, hashes[0]);
	CSR_WRITE_4(sc, RxHashTable2, hashes[1]);
}

void
se_reset(struct se_softc *sc)
{
	CSR_WRITE_4(sc, IntrMask, 0);
	CSR_WRITE_4(sc, IntrStatus, 0xffffffff);

	/* Soft reset. */
	CSR_WRITE_4(sc, IntrControl, 0x8000);
	CSR_READ_4(sc, IntrControl);
	DELAY(100);
	CSR_WRITE_4(sc, IntrControl, 0);
	/* Stop MAC. */
	CSR_WRITE_4(sc, TX_CTL, 0x1a00);
	CSR_WRITE_4(sc, RX_CTL, 0x1a00);

	CSR_WRITE_4(sc, IntrMask, 0);
	CSR_WRITE_4(sc, IntrStatus, 0xffffffff);

	CSR_WRITE_4(sc, GMIIControl, 0);
}

/*
 * Probe for an SiS chip. Check the PCI vendor and device
 * IDs against our list and return a device name if we find a match.
 */
int
se_match(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = (struct pci_attach_args *)aux;

	return pci_matchbyid(pa, se_devices, nitems(se_devices));
}

/*
 * Attach the interface. Do ifmedia setup and ethernet/BPF attach.
 */
void
se_attach(struct device *parent, struct device *self, void *aux)
{
	struct se_softc *sc = (struct se_softc *)self;
	struct arpcom *ac = &sc->sc_ac;
	struct ifnet *ifp = &ac->ac_if;
	struct pci_attach_args *pa = (struct pci_attach_args *)aux;
	uint8_t eaddr[ETHER_ADDR_LEN];
	const char *intrstr;
	pci_intr_handle_t ih;
	bus_size_t iosize;
	bus_dma_segment_t seg;
	struct se_list_data *ld;
	struct se_chain_data *cd;
	int nseg;
	uint i;
	int rc;

	printf(": ");

	/*
	 * Map control/status registers.
	 */

	rc = pci_mapreg_map(pa, PCI_MAPREG_START, PCI_MAPREG_TYPE_MEM, 0,
	    &sc->sc_iot, &sc->sc_ioh, NULL, &iosize, 0);
	if (rc != 0) {
		printf("can't map i/o space\n");
		return;
	}

	if (pci_intr_map(pa, &ih)) {
		printf("can't map interrupt\n");
		goto fail1;
	}
	intrstr = pci_intr_string(pa->pa_pc, ih);
	sc->sc_ih = pci_intr_establish(pa->pa_pc, ih, IPL_NET, se_intr, sc,
	    self->dv_xname);
	if (sc->sc_ih == NULL) {
		printf("can't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		goto fail1;
	}

	printf("%s", intrstr);

	if (pa->pa_id == PCI_ID_CODE(PCI_VENDOR_SIS, PCI_PRODUCT_SIS_190))
		sc->sc_flags |= SE_FLAG_FASTETHER;

	/* Reset the adapter. */
	se_reset(sc);

	/* Get MAC address from the EEPROM. */
	if ((pci_conf_read(pa->pa_pc, pa->pa_tag, 0x70) & (0x01 << 24)) != 0)
		se_get_mac_addr_apc(sc, eaddr);
	else
		se_get_mac_addr_eeprom(sc, eaddr);
	printf(", address %s\n", ether_sprintf(eaddr));
	bcopy(eaddr, ac->ac_enaddr, ETHER_ADDR_LEN);

	/*
	 * Now do all the DMA mapping stuff
	 */

	sc->sc_dmat = pa->pa_dmat;
	ld = &sc->se_ldata;
	cd = &sc->se_cdata;

	/* First create TX/RX busdma maps. */
	for (i = 0; i < SE_RX_RING_CNT; i++) {
		rc = bus_dmamap_create(sc->sc_dmat, MCLBYTES, 1, MCLBYTES,
		    0, BUS_DMA_NOWAIT, &cd->se_rx_map[i]);
		if (rc != 0) {
			printf("%s: cannot init the RX map array\n",
			    self->dv_xname);
			goto fail2;
		}
	}

	for (i = 0; i < SE_TX_RING_CNT; i++) {
		rc = bus_dmamap_create(sc->sc_dmat, MCLBYTES, 1, MCLBYTES,
		    0, BUS_DMA_NOWAIT, &cd->se_tx_map[i]);
		if (rc != 0) {
			printf("%s: cannot init the TX map array\n",
			    self->dv_xname);
			goto fail2;
		}
	}

	/*
	 * Now allocate a chunk of DMA-able memory for RX and TX ring
	 * descriptors, as a contiguous block of memory.
	 * XXX fix deallocation upon error
	 */

	/* RX */
	rc = bus_dmamem_alloc(sc->sc_dmat, SE_RX_RING_SZ, PAGE_SIZE, 0,
	    &seg, 1, &nseg, BUS_DMA_NOWAIT);
	if (rc != 0) {
		printf("%s: no memory for RX descriptors\n", self->dv_xname);
		goto fail2;
	}

	rc = bus_dmamem_map(sc->sc_dmat, &seg, nseg, SE_RX_RING_SZ,
	    (caddr_t *)&ld->se_rx_ring, BUS_DMA_NOWAIT);
	if (rc != 0) {
		printf("%s: can't map RX descriptors\n", self->dv_xname);
		goto fail2;
	}

	rc = bus_dmamap_create(sc->sc_dmat, SE_RX_RING_SZ, 1,
	    SE_RX_RING_SZ, 0, BUS_DMA_NOWAIT, &ld->se_rx_dmamap);
	if (rc != 0) {
		printf("%s: can't alloc RX DMA map\n", self->dv_xname);
		goto fail2;
	}

	rc = bus_dmamap_load(sc->sc_dmat, ld->se_rx_dmamap,
	    (caddr_t)ld->se_rx_ring, SE_RX_RING_SZ, NULL, BUS_DMA_NOWAIT);
	if (rc != 0) {
		printf("%s: can't load RX DMA map\n", self->dv_xname);
		bus_dmamem_unmap(sc->sc_dmat,
		    (caddr_t)ld->se_rx_ring, SE_RX_RING_SZ);
		bus_dmamap_destroy(sc->sc_dmat, ld->se_rx_dmamap);
		bus_dmamem_free(sc->sc_dmat, &seg, nseg);
		goto fail2;
	}

	/* TX */
	rc = bus_dmamem_alloc(sc->sc_dmat, SE_TX_RING_SZ, PAGE_SIZE, 0,
	    &seg, 1, &nseg, BUS_DMA_NOWAIT);
	if (rc != 0) {
		printf("%s: no memory for TX descriptors\n", self->dv_xname);
		goto fail2;
	}

	rc = bus_dmamem_map(sc->sc_dmat, &seg, nseg, SE_TX_RING_SZ,
	    (caddr_t *)&ld->se_tx_ring, BUS_DMA_NOWAIT);
	if (rc != 0) {
		printf("%s: can't map TX descriptors\n", self->dv_xname);
		goto fail2;
	}

	rc = bus_dmamap_create(sc->sc_dmat, SE_TX_RING_SZ, 1,
	    SE_TX_RING_SZ, 0, BUS_DMA_NOWAIT, &ld->se_tx_dmamap);
	if (rc != 0) {
		printf("%s: can't alloc TX DMA map\n", self->dv_xname);
		goto fail2;
	}

	rc = bus_dmamap_load(sc->sc_dmat, ld->se_tx_dmamap,
	    (caddr_t)ld->se_tx_ring, SE_TX_RING_SZ, NULL, BUS_DMA_NOWAIT);
	if (rc != 0) {
		printf("%s: can't load TX DMA map\n", self->dv_xname);
		bus_dmamem_unmap(sc->sc_dmat,
		    (caddr_t)ld->se_tx_ring, SE_TX_RING_SZ);
		bus_dmamap_destroy(sc->sc_dmat, ld->se_tx_dmamap);
		bus_dmamem_free(sc->sc_dmat, &seg, nseg);
		goto fail2;
	}

	timeout_set(&sc->sc_tick_tmo, se_tick, sc);

	ifp = &sc->sc_ac.ac_if;
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = se_ioctl;
	ifp->if_start = se_start;
	ifp->if_watchdog = se_watchdog;
	ifq_init_maxlen(&ifp->if_snd, SE_TX_RING_CNT - 1);
	bcopy(sc->sc_dev.dv_xname, ifp->if_xname, IFNAMSIZ);

	ifp->if_capabilities = IFCAP_VLAN_MTU;

	/*
	 * Do MII setup.
	 */

	sc->sc_mii.mii_ifp = ifp;
	sc->sc_mii.mii_readreg = se_miibus_readreg;
	sc->sc_mii.mii_writereg = se_miibus_writereg;
	sc->sc_mii.mii_statchg = se_miibus_statchg;
	ifmedia_init(&sc->sc_mii.mii_media, 0, se_ifmedia_upd,
	    se_ifmedia_sts);
	mii_attach(&sc->sc_dev, &sc->sc_mii, 0xffffffff, MII_PHY_ANY,
	    MII_OFFSET_ANY, 0);

	if (LIST_FIRST(&sc->sc_mii.mii_phys) == NULL) {
		/* No PHY attached */
		ifmedia_add(&sc->sc_mii.mii_media, IFM_ETHER | IFM_MANUAL,
		    0, NULL);
		ifmedia_set(&sc->sc_mii.mii_media, IFM_ETHER | IFM_MANUAL);
	} else
		ifmedia_set(&sc->sc_mii.mii_media, IFM_ETHER | IFM_AUTO);

	/*
	 * Call MI attach routine.
	 */
	if_attach(ifp);
	ether_ifattach(ifp);

	return;

fail2:
	pci_intr_disestablish(pa->pa_pc, sc->sc_ih);
fail1:
	bus_space_unmap(sc->sc_iot, sc->sc_ioh, iosize);
}

int
se_activate(struct device *self, int act)
{
	struct se_softc *sc = (struct se_softc *)self;
	struct ifnet *ifp = &sc->sc_ac.ac_if;

	switch (act) {
	case DVACT_SUSPEND:
		if (ifp->if_flags & IFF_RUNNING)
			se_stop(sc);
		break;
	case DVACT_RESUME:
		if (ifp->if_flags & IFF_UP)
			(void)se_init(ifp);
		break;
	}
	return (0);
}

/*
 * Initialize the TX descriptors.
 */
int
se_list_tx_init(struct se_softc *sc)
{
	struct se_list_data *ld = &sc->se_ldata;
	struct se_chain_data *cd = &sc->se_cdata;

	bzero(ld->se_tx_ring, SE_TX_RING_SZ);
	ld->se_tx_ring[SE_TX_RING_CNT - 1].se_flags = htole32(RING_END);
	bus_dmamap_sync(sc->sc_dmat, ld->se_tx_dmamap, 0, SE_TX_RING_SZ,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	cd->se_tx_prod = 0;
	cd->se_tx_cons = 0;
	cd->se_tx_cnt = 0;

	return 0;
}

int
se_list_tx_free(struct se_softc *sc)
{
	struct se_chain_data *cd = &sc->se_cdata;
	uint i;

	for (i = 0; i < SE_TX_RING_CNT; i++) {
		if (cd->se_tx_mbuf[i] != NULL) {
			bus_dmamap_unload(sc->sc_dmat, cd->se_tx_map[i]);
			m_free(cd->se_tx_mbuf[i]);
			cd->se_tx_mbuf[i] = NULL;
		}
	}

	return 0;
}

/*
 * Initialize the RX descriptors and allocate mbufs for them.
 */
int
se_list_rx_init(struct se_softc *sc)
{
	struct se_list_data *ld = &sc->se_ldata;
	struct se_chain_data *cd = &sc->se_cdata;
	uint i;

	bzero(ld->se_rx_ring, SE_RX_RING_SZ);
	bus_dmamap_sync(sc->sc_dmat, ld->se_rx_dmamap, 0, SE_RX_RING_SZ,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	for (i = 0; i < SE_RX_RING_CNT; i++) {
		if (se_newbuf(sc, i) != 0)
			return ENOBUFS;
	}

	cd->se_rx_prod = 0;

	return 0;
}

int
se_list_rx_free(struct se_softc *sc)
{
	struct se_chain_data *cd = &sc->se_cdata;
	uint i;

	for (i = 0; i < SE_RX_RING_CNT; i++) {
		if (cd->se_rx_mbuf[i] != NULL) {
			bus_dmamap_unload(sc->sc_dmat, cd->se_rx_map[i]);
			m_free(cd->se_rx_mbuf[i]);
			cd->se_rx_mbuf[i] = NULL;
		}
	}

	return 0;
}

/*
 * Initialize an RX descriptor and attach an MBUF cluster.
 */
int
se_newbuf(struct se_softc *sc, uint i)
{
#ifdef SE_DEBUG
	struct ifnet *ifp = &sc->sc_ac.ac_if;
#endif
	struct se_list_data *ld = &sc->se_ldata;
	struct se_chain_data *cd = &sc->se_cdata;
	struct se_desc *desc;
	struct mbuf *m;
	int rc;

	m = MCLGETL(NULL, M_DONTWAIT, MCLBYTES);
	if (m == NULL) {
#ifdef SE_DEBUG
		if (ifp->if_flags & IFF_DEBUG)
			printf("%s: MCLGETL failed\n", ifp->if_xname);
#endif
		return ENOBUFS;
	}
	m->m_len = m->m_pkthdr.len = MCLBYTES;
	m_adj(m, SE_RX_BUF_ALIGN);

	rc = bus_dmamap_load_mbuf(sc->sc_dmat, cd->se_rx_map[i],
	    m, BUS_DMA_NOWAIT);
	KASSERT(cd->se_rx_map[i]->dm_nsegs == 1);
	if (rc != 0) {
		m_freem(m);
		return ENOBUFS;
	}
	bus_dmamap_sync(sc->sc_dmat, cd->se_rx_map[i], 0,
	    cd->se_rx_map[i]->dm_mapsize, BUS_DMASYNC_PREREAD);

	cd->se_rx_mbuf[i] = m;
	desc = &ld->se_rx_ring[i];
	desc->se_sts_size = 0;
	desc->se_cmdsts = htole32(RDC_OWN | RDC_INTR);
	desc->se_ptr = htole32((uint32_t)cd->se_rx_map[i]->dm_segs[0].ds_addr);
	desc->se_flags = htole32(cd->se_rx_map[i]->dm_segs[0].ds_len);
	if (i == SE_RX_RING_CNT - 1)
		desc->se_flags |= htole32(RING_END);
	bus_dmamap_sync(sc->sc_dmat, ld->se_rx_dmamap, i * sizeof(*desc),
	    sizeof(*desc), BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	return 0;
}

void
se_discard_rxbuf(struct se_softc *sc, uint i)
{
	struct se_list_data *ld = &sc->se_ldata;
	struct se_desc *desc;

	desc = &ld->se_rx_ring[i];
	desc->se_sts_size = 0;
	desc->se_cmdsts = htole32(RDC_OWN | RDC_INTR);
	desc->se_flags = htole32(MCLBYTES - SE_RX_BUF_ALIGN);
	if (i == SE_RX_RING_CNT - 1)
		desc->se_flags |= htole32(RING_END);
	bus_dmamap_sync(sc->sc_dmat, ld->se_rx_dmamap, i * sizeof(*desc),
	    sizeof(*desc), BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

/*
 * A frame has been uploaded: pass the resulting mbuf chain up to
 * the higher level protocols.
 */
void
se_rxeof(struct se_softc *sc)
{
	struct mbuf *m;
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct se_list_data *ld = &sc->se_ldata;
	struct se_chain_data *cd = &sc->se_cdata;
	struct se_desc *cur_rx;
	uint32_t rxinfo, rxstat;
	uint i;

	bus_dmamap_sync(sc->sc_dmat, ld->se_rx_dmamap, 0, SE_RX_RING_SZ,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	for (i = cd->se_rx_prod; ; SE_INC(i, SE_RX_RING_CNT)) {
		cur_rx = &ld->se_rx_ring[i];
		rxinfo = letoh32(cur_rx->se_cmdsts);
		if ((rxinfo & RDC_OWN) != 0)
			break;
		rxstat = letoh32(cur_rx->se_sts_size);

		/*
		 * If an error occurs, update stats, clear the
		 * status word and leave the mbuf cluster in place:
		 * it should simply get re-used next time this descriptor
		 * comes up in the ring.
		 */
		if ((rxstat & RDS_CRCOK) == 0 || SE_RX_ERROR(rxstat) != 0 ||
		    SE_RX_NSEGS(rxstat) != 1) {
			/* XXX We don't support multi-segment frames yet. */
			if (ifp->if_flags & IFF_DEBUG)
				printf("%s: rx error %b\n",
				    ifp->if_xname, rxstat, RX_ERR_BITS);
			se_discard_rxbuf(sc, i);
			ifp->if_ierrors++;
			continue;
		}

		/* No errors; receive the packet. */
		bus_dmamap_sync(sc->sc_dmat, cd->se_rx_map[i], 0,
		    cd->se_rx_map[i]->dm_mapsize, BUS_DMASYNC_POSTREAD);
		m = cd->se_rx_mbuf[i];
		if (se_newbuf(sc, i) != 0) {
			se_discard_rxbuf(sc, i);
			ifp->if_iqdrops++;
			continue;
		}
		/*
		 * Account for 10 bytes auto padding which is used
		 * to align IP header on a 32bit boundary.  Also note,
		 * CRC bytes are automatically removed by the hardware.
		 */
		m->m_data += SE_RX_PAD_BYTES;
		m->m_pkthdr.len = m->m_len =
		    SE_RX_BYTES(rxstat) - SE_RX_PAD_BYTES;

		ml_enqueue(&ml, m);
	}

	if_input(ifp, &ml);

	cd->se_rx_prod = i;
}

/*
 * A frame was downloaded to the chip. It's safe for us to clean up
 * the list buffers.
 */

void
se_txeof(struct se_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct se_list_data *ld = &sc->se_ldata;
	struct se_chain_data *cd = &sc->se_cdata;
	struct se_desc *cur_tx;
	uint32_t txstat;
	uint i;

	/*
	 * Go through our tx list and free mbufs for those
	 * frames that have been transmitted.
	 */
	bus_dmamap_sync(sc->sc_dmat, ld->se_tx_dmamap, 0, SE_TX_RING_SZ,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	for (i = cd->se_tx_cons; cd->se_tx_cnt > 0;
	    cd->se_tx_cnt--, SE_INC(i, SE_TX_RING_CNT)) {
		cur_tx = &ld->se_tx_ring[i];
		txstat = letoh32(cur_tx->se_cmdsts);
		if ((txstat & TDC_OWN) != 0)
			break;

		ifq_clr_oactive(&ifp->if_snd);

		if (SE_TX_ERROR(txstat) != 0) {
			if (ifp->if_flags & IFF_DEBUG)
				printf("%s: tx error %b\n",
				    ifp->if_xname, txstat, TX_ERR_BITS);
			ifp->if_oerrors++;
			/* TODO: better error differentiation */
		}

		if (cd->se_tx_mbuf[i] != NULL) {
			bus_dmamap_sync(sc->sc_dmat, cd->se_tx_map[i], 0,
			    cd->se_tx_map[i]->dm_mapsize,
			    BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_dmat, cd->se_tx_map[i]);
			m_free(cd->se_tx_mbuf[i]);
			cd->se_tx_mbuf[i] = NULL;
		}

		cur_tx->se_sts_size = 0;
		cur_tx->se_cmdsts = 0;
		cur_tx->se_ptr = 0;
		cur_tx->se_flags &= htole32(RING_END);
		bus_dmamap_sync(sc->sc_dmat, ld->se_tx_dmamap,
		    i * sizeof(*cur_tx), sizeof(*cur_tx),
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	}

	cd->se_tx_cons = i;
	if (cd->se_tx_cnt == 0)
		ifp->if_timer = 0;
}

void
se_tick(void *xsc)
{
	struct se_softc *sc = xsc;
	struct mii_data *mii;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	int s;

	s = splnet();
	mii = &sc->sc_mii;
	mii_tick(mii);
	if ((sc->sc_flags & SE_FLAG_LINK) == 0) {
		se_miibus_statchg(&sc->sc_dev);
		if ((sc->sc_flags & SE_FLAG_LINK) != 0 &&
		    !ifq_empty(&ifp->if_snd))
			se_start(ifp);
	}
	splx(s);

	timeout_add_sec(&sc->sc_tick_tmo, 1);
}

int
se_intr(void *arg)
{
	struct se_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	uint32_t status;

	status = CSR_READ_4(sc, IntrStatus);
	if (status == 0xffffffff || (status & SE_INTRS) == 0) {
		/* Not ours. */
		return 0;
	}
	/* Ack interrupts/ */
	CSR_WRITE_4(sc, IntrStatus, status);
	/* Disable further interrupts. */
	CSR_WRITE_4(sc, IntrMask, 0);

	for (;;) {
		if ((ifp->if_flags & IFF_RUNNING) == 0)
			break;
		if ((status & (INTR_RX_DONE | INTR_RX_IDLE)) != 0) {
			se_rxeof(sc);
			/* Wakeup Rx MAC. */
			if ((status & INTR_RX_IDLE) != 0)
				CSR_WRITE_4(sc, RX_CTL,
				    0x1a00 | 0x000c | RX_CTL_POLL | RX_CTL_ENB);
		}
		if ((status & (INTR_TX_DONE | INTR_TX_IDLE)) != 0)
			se_txeof(sc);
		status = CSR_READ_4(sc, IntrStatus);
		if ((status & SE_INTRS) == 0)
			break;
		/* Ack interrupts. */
		CSR_WRITE_4(sc, IntrStatus, status);
	}

	if ((ifp->if_flags & IFF_RUNNING) != 0) {
		/* Re-enable interrupts */
		CSR_WRITE_4(sc, IntrMask, SE_INTRS);
		if (!ifq_empty(&ifp->if_snd))
			se_start(ifp);
	}

	return 1;
}

/*
 * Encapsulate an mbuf chain in a descriptor by coupling the mbuf data
 * pointers to the fragment pointers.
 */
int
se_encap(struct se_softc *sc, struct mbuf *m_head, uint32_t *txidx)
{
#ifdef SE_DEBUG
	struct ifnet *ifp = &sc->sc_ac.ac_if;
#endif
	struct mbuf *m;
	struct se_list_data *ld = &sc->se_ldata;
	struct se_chain_data *cd = &sc->se_cdata;
	struct se_desc *desc;
	uint i, cnt = 0;
	int rc;

	/*
	 * If there's no way we can send any packets, return now.
	 */
	if (SE_TX_RING_CNT - cd->se_tx_cnt < 2) {
#ifdef SE_DEBUG
		if (ifp->if_flags & IFF_DEBUG)
			printf("%s: encap failed, not enough TX desc\n",
			    ifp->if_xname);
#endif
		return ENOBUFS;
	}

	if (m_defrag(m_head, M_DONTWAIT) != 0) {
#ifdef SE_DEBUG
		if (ifp->if_flags & IFF_DEBUG)
			printf("%s: m_defrag failed\n", ifp->if_xname);
#endif
		return ENOBUFS;	/* XXX should not be fatal */
	}

	/*
	 * Start packing the mbufs in this chain into
	 * the fragment pointers. Stop when we run out
	 * of fragments or hit the end of the mbuf chain.
	 */
	i = *txidx;

	for (m = m_head; m != NULL; m = m->m_next) {
		if (m->m_len == 0)
			continue;
		if ((SE_TX_RING_CNT - (cd->se_tx_cnt + cnt)) < 2) {
#ifdef SE_DEBUG
			if (ifp->if_flags & IFF_DEBUG)
				printf("%s: encap failed, not enough TX desc\n",
				    ifp->if_xname);
#endif
			return ENOBUFS;
		}
		cd->se_tx_mbuf[i] = m;
		rc = bus_dmamap_load_mbuf(sc->sc_dmat, cd->se_tx_map[i],
		    m, BUS_DMA_NOWAIT);
		if (rc != 0)
			return ENOBUFS;
		KASSERT(cd->se_tx_map[i]->dm_nsegs == 1);
		bus_dmamap_sync(sc->sc_dmat, cd->se_tx_map[i], 0,
		    cd->se_tx_map[i]->dm_mapsize, BUS_DMASYNC_PREWRITE);

		desc = &ld->se_tx_ring[i];
		desc->se_sts_size = htole32(cd->se_tx_map[i]->dm_segs->ds_len);
		desc->se_ptr =
		    htole32((uint32_t)cd->se_tx_map[i]->dm_segs->ds_addr);
		desc->se_flags = htole32(cd->se_tx_map[i]->dm_segs->ds_len);
		if (i == SE_TX_RING_CNT - 1)
			desc->se_flags |= htole32(RING_END);
		desc->se_cmdsts = htole32(TDC_OWN | TDC_INTR | TDC_DEF |
		    TDC_CRC | TDC_PAD | TDC_BST);
		bus_dmamap_sync(sc->sc_dmat, ld->se_tx_dmamap,
		    i * sizeof(*desc), sizeof(*desc),
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

		SE_INC(i, SE_TX_RING_CNT);
		cnt++;
	}

	/* can't happen */
	if (m != NULL)
		return ENOBUFS;

	cd->se_tx_cnt += cnt;
	*txidx = i;

	return 0;
}

/*
 * Main transmit routine. To avoid having to do mbuf copies, we put pointers
 * to the mbuf data regions directly in the transmit lists. We also save a
 * copy of the pointers since the transmit list fragment pointers are
 * physical addresses.
 */
void
se_start(struct ifnet *ifp)
{
	struct se_softc *sc = ifp->if_softc;
	struct mbuf *m_head = NULL;
	struct se_chain_data *cd = &sc->se_cdata;
	uint i, queued = 0;

	if ((sc->sc_flags & SE_FLAG_LINK) == 0 ||
	    !(ifp->if_flags & IFF_RUNNING) || ifq_is_oactive(&ifp->if_snd)) {
#ifdef SE_DEBUG
		if (ifp->if_flags & IFF_DEBUG)
			printf("%s: can't tx, flags 0x%x 0x%04x\n",
			    ifp->if_xname, sc->sc_flags, (uint)ifp->if_flags);
#endif
		return;
	}

	i = cd->se_tx_prod;

	while (cd->se_tx_mbuf[i] == NULL) {
		m_head = ifq_deq_begin(&ifp->if_snd);
		if (m_head == NULL)
			break;

		if (se_encap(sc, m_head, &i) != 0) {
			ifq_deq_rollback(&ifp->if_snd, m_head);
			ifq_set_oactive(&ifp->if_snd);
			break;
		}

		/* now we are committed to transmit the packet */
		ifq_deq_commit(&ifp->if_snd, m_head);
		queued++;

		/*
		 * If there's a BPF listener, bounce a copy of this frame
		 * to him.
		 */
#if NBPFILTER > 0
		if (ifp->if_bpf)
			bpf_mtap(ifp->if_bpf, m_head, BPF_DIRECTION_OUT);
#endif
	}

	if (queued > 0) {
		/* Transmit */
		cd->se_tx_prod = i;
		CSR_WRITE_4(sc, TX_CTL, 0x1a00 | TX_CTL_ENB | TX_CTL_POLL);
		ifp->if_timer = 5;
	}
}

int
se_init(struct ifnet *ifp)
{
	struct se_softc *sc = ifp->if_softc;
	uint16_t rxfilt;
	int i;

	splassert(IPL_NET);

	/*
	 * Cancel pending I/O and free all RX/TX buffers.
	 */
	se_stop(sc);
	se_reset(sc);

	/* Init circular RX list. */
	if (se_list_rx_init(sc) == ENOBUFS) {
		se_stop(sc);	/* XXX necessary? */
		return ENOBUFS;
	}

	/* Init TX descriptors. */
	se_list_tx_init(sc);

	/*
	 * Load the address of the RX and TX lists.
	 */
	CSR_WRITE_4(sc, TX_DESC,
	    (uint32_t)sc->se_ldata.se_tx_dmamap->dm_segs[0].ds_addr);
	CSR_WRITE_4(sc, RX_DESC,
	    (uint32_t)sc->se_ldata.se_rx_dmamap->dm_segs[0].ds_addr);

	CSR_WRITE_4(sc, TxMacControl, 0x60);
	CSR_WRITE_4(sc, RxWakeOnLan, 0);
	CSR_WRITE_4(sc, RxWakeOnLanData, 0);
	CSR_WRITE_2(sc, RxMPSControl, ETHER_MAX_LEN + ETHER_VLAN_ENCAP_LEN +
	    SE_RX_PAD_BYTES);

	for (i = 0; i < ETHER_ADDR_LEN; i++)
		CSR_WRITE_1(sc, RxMacAddr + i, sc->sc_ac.ac_enaddr[i]);
	/* Configure RX MAC. */
	rxfilt = RXMAC_STRIP_FCS | RXMAC_PAD_ENB | RXMAC_CSUM_ENB;
	CSR_WRITE_2(sc, RxMacControl, rxfilt);

	/* Program promiscuous mode and multicast filters. */
	se_iff(sc);

	/*
	 * Clear and enable interrupts.
	 */
	CSR_WRITE_4(sc, IntrStatus, 0xFFFFFFFF);
	CSR_WRITE_4(sc, IntrMask, SE_INTRS);

	/* Enable receiver and transmitter. */
	CSR_WRITE_4(sc, TX_CTL, 0x1a00 | TX_CTL_ENB);
	CSR_WRITE_4(sc, RX_CTL, 0x1a00 | 0x000c | RX_CTL_POLL | RX_CTL_ENB);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	sc->sc_flags &= ~SE_FLAG_LINK;
	mii_mediachg(&sc->sc_mii);
	timeout_add_sec(&sc->sc_tick_tmo, 1);

	return 0;
}

/*
 * Set media options.
 */
int
se_ifmedia_upd(struct ifnet *ifp)
{
	struct se_softc *sc = ifp->if_softc;
	struct mii_data *mii;

	mii = &sc->sc_mii;
	sc->sc_flags &= ~SE_FLAG_LINK;
	if (mii->mii_instance) {
		struct mii_softc *miisc;
		LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
			mii_phy_reset(miisc);
	}
	return mii_mediachg(mii);
}

/*
 * Report current media status.
 */
void
se_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct se_softc *sc = ifp->if_softc;
	struct mii_data *mii;

	mii = &sc->sc_mii;
	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
}

int
se_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct se_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *) data;
	int s, rc = 0;

	s = splnet();

	switch (command) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		if ((ifp->if_flags & IFF_RUNNING) == 0)
			rc = se_init(ifp);
		break;
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				rc = ENETRESET;
			else
				rc = se_init(ifp);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				se_stop(sc);
		}
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		rc = ifmedia_ioctl(ifp, ifr, &sc->sc_mii.mii_media, command);
		break;
	default:
		rc = ether_ioctl(ifp, &sc->sc_ac, command, data);
		break;
	}

	if (rc == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			se_iff(sc);
		rc = 0;
	}

	splx(s);
	return rc;
}

void
se_watchdog(struct ifnet *ifp)
{
	struct se_softc *sc = ifp->if_softc;
	int s;

	printf("%s: watchdog timeout\n", sc->sc_dev.dv_xname);
	ifp->if_oerrors++;

	s = splnet();
	se_init(ifp);
	if (!ifq_empty(&ifp->if_snd))
		se_start(ifp);
	splx(s);
}

/*
 * Stop the adapter and free any mbufs allocated to the
 * RX and TX lists.
 */
void
se_stop(struct se_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;

	ifp->if_timer = 0;
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
	timeout_del(&sc->sc_tick_tmo);
	mii_down(&sc->sc_mii);

	CSR_WRITE_4(sc, IntrMask, 0);
	CSR_READ_4(sc, IntrMask);
	CSR_WRITE_4(sc, IntrStatus, 0xffffffff);
	/* Stop TX/RX MAC. */
	CSR_WRITE_4(sc, TX_CTL, 0x1a00);
	CSR_WRITE_4(sc, RX_CTL, 0x1a00);
	/* XXX Can we assume active DMA cycles gone? */
	DELAY(2000);
	CSR_WRITE_4(sc, IntrMask, 0);
	CSR_WRITE_4(sc, IntrStatus, 0xffffffff);

	sc->sc_flags &= ~SE_FLAG_LINK;
	se_list_rx_free(sc);
	se_list_tx_free(sc);
}
