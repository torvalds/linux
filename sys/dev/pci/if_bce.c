/* $OpenBSD: if_bce.c,v 1.60 2025/09/04 15:45:56 mpi Exp $ */
/* $NetBSD: if_bce.c,v 1.3 2003/09/29 01:53:02 mrg Exp $	 */

/*
 * Copyright (c) 2003 Clifford Wright. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Broadcom BCM440x 10/100 ethernet (broadcom.com)
 * SiliconBackplane is technology from Sonics, Inc.(sonicsinc.com)
 *
 * Cliff Wright cliff@snipe444.org
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/timeout.h>
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

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <dev/pci/if_bcereg.h>

#include <uvm/uvm.h>

/* ring descriptor */
struct bce_dma_slot {
	u_int32_t ctrl;
	u_int32_t addr;
};
#define CTRL_BC_MASK	0x1fff	/* buffer byte count */
#define CTRL_EOT	0x10000000	/* end of descriptor table */
#define CTRL_IOC	0x20000000	/* interrupt on completion */
#define CTRL_EOF	0x40000000	/* end of frame */
#define CTRL_SOF	0x80000000	/* start of frame */

#define BCE_RXBUF_LEN	(MCLBYTES - 4)

/* Packet status is returned in a pre-packet header */
struct rx_pph {
	u_int16_t len;
	u_int16_t flags;
	u_int16_t pad[12];
};

#define	BCE_PREPKT_HEADER_SIZE		30

/* packet status flags bits */
#define RXF_NO				0x8	/* odd number of nibbles */
#define RXF_RXER			0x4	/* receive symbol error */
#define RXF_CRC				0x2	/* crc error */
#define RXF_OV				0x1	/* fifo overflow */

/* number of descriptors used in a ring */
#define BCE_NRXDESC		64
#define BCE_NTXDESC		64

#define BCE_TIMEOUT		100	/* # 10us for mii read/write */

struct bce_softc {
	struct device		bce_dev;
	bus_space_tag_t		bce_btag;
	bus_space_handle_t	bce_bhandle;
	bus_dma_tag_t		bce_dmatag;
	struct arpcom		bce_ac;		/* interface info */
	void			*bce_intrhand;
	struct pci_attach_args	bce_pa;
	struct mii_data		bce_mii;
	u_int32_t		bce_phy;	/* eeprom indicated phy */
	struct bce_dma_slot	*bce_rx_ring;	/* receive ring */
	struct bce_dma_slot	*bce_tx_ring;	/* transmit ring */
	caddr_t			bce_data;
	bus_dmamap_t		bce_ring_map;
	bus_dmamap_t		bce_rxdata_map;
	bus_dmamap_t		bce_txdata_map;
	u_int32_t		bce_intmask;	/* current intr mask */
	u_int32_t		bce_rxin;	/* last rx descriptor seen */
	u_int32_t		bce_txin;	/* last tx descriptor seen */
	int			bce_txsfree;	/* no. tx slots available */
	int			bce_txsnext;	/* next available tx slot */
	struct timeout		bce_timeout;
};

int	bce_probe(struct device *, void *, void *);
void	bce_attach(struct device *, struct device *, void *);
int	bce_activate(struct device *, int);
int	bce_ioctl(struct ifnet *, u_long, caddr_t);
void	bce_start(struct ifnet *);
void	bce_watchdog(struct ifnet *);
int	bce_intr(void *);
void	bce_rxintr(struct bce_softc *);
void	bce_txintr(struct bce_softc *);
int	bce_init(struct ifnet *);
void	bce_add_mac(struct bce_softc *, u_int8_t *, unsigned long);
void	bce_add_rxbuf(struct bce_softc *, int);
void	bce_stop(struct ifnet *);
void	bce_reset(struct bce_softc *);
void	bce_iff(struct ifnet *);
int	bce_mii_read(struct device *, int, int);
void	bce_mii_write(struct device *, int, int, int);
void	bce_statchg(struct device *);
int	bce_mediachange(struct ifnet *);
void	bce_mediastatus(struct ifnet *, struct ifmediareq *);
void	bce_tick(void *);

#ifdef BCE_DEBUG
#define DPRINTF(x)	do {		\
	if (bcedebug)			\
		printf x;		\
} while (/* CONSTCOND */ 0)
#define DPRINTFN(n,x)	do {		\
	if (bcedebug >= (n))		\
		printf x;		\
} while (/* CONSTCOND */ 0)
int	bcedebug = 0;
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

const struct cfattach bce_ca = {
	sizeof(struct bce_softc), bce_probe, bce_attach, NULL, bce_activate
};
struct cfdriver bce_cd = {
	NULL, "bce", DV_IFNET
};

const struct pci_matchid bce_devices[] = {
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM4401 },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM4401B0 },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM4401B1 }
};

int
bce_probe(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid((struct pci_attach_args *)aux, bce_devices,
	    nitems(bce_devices)));
}

void
bce_attach(struct device *parent, struct device *self, void *aux)
{
	struct bce_softc *sc = (struct bce_softc *) self;
	struct pci_attach_args *pa = aux;
	pci_chipset_tag_t pc = pa->pa_pc;
	pci_intr_handle_t ih;
	const char *intrstr = NULL;
	caddr_t kva;
	bus_dma_segment_t seg, dseg;
	int rseg, drseg;
	struct ifnet *ifp;
	pcireg_t memtype;
	bus_addr_t memaddr;
	bus_size_t memsize;
	int pmreg;
	pcireg_t pmode;
	int error;

	sc->bce_pa = *pa;
	sc->bce_dmatag = pa->pa_dmat;

	/*
	 * Map control/status registers.
	 */
	memtype = pci_mapreg_type(pa->pa_pc, pa->pa_tag, BCE_PCI_BAR0);
	if (pci_mapreg_map(pa, BCE_PCI_BAR0, memtype, 0, &sc->bce_btag,
	    &sc->bce_bhandle, &memaddr, &memsize, 0)) {
		printf(": unable to find mem space\n");
		return;
	}

	/* Get it out of power save mode if needed. */
	if (pci_get_capability(pc, pa->pa_tag, PCI_CAP_PWRMGMT, &pmreg, 0)) {
		pmode = pci_conf_read(pc, pa->pa_tag, pmreg + 4) & 0x3;
		if (pmode == 3) {
			/*
			 * The card has lost all configuration data in
			 * this state, so punt.
			 */
			printf(": unable to wake up from power state D3\n");
			return;
		}
		if (pmode != 0) {
			printf(": waking up from power state D%d\n",
			    pmode);
			pci_conf_write(pc, pa->pa_tag, pmreg + 4, 0);
		}
	}

	if (pci_intr_map(pa, &ih)) {
		printf(": couldn't map interrupt\n");
		return;
	}

	intrstr = pci_intr_string(pc, ih);
	sc->bce_intrhand = pci_intr_establish(pc, ih, IPL_NET, bce_intr, sc,
	    self->dv_xname);
	if (sc->bce_intrhand == NULL) {
		printf(": couldn't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		return;
	}

	/* reset the chip */
	bce_reset(sc);

	/* Create the data DMA region and maps. */
	if ((error = bus_dmamem_alloc_range(sc->bce_dmatag,
	    (BCE_NTXDESC + BCE_NRXDESC) * MCLBYTES, 0, 0, &dseg, 1, &drseg,
	    BUS_DMA_NOWAIT, (bus_addr_t)0,
	    (bus_addr_t)(0x40000000 - 1))) != 0) {
		printf(": unable to alloc space for data, error = %d", error);
		return;
	}

	if ((error = bus_dmamem_map(sc->bce_dmatag, &dseg, drseg,
	    (BCE_NTXDESC + BCE_NRXDESC) * MCLBYTES, &sc->bce_data,
	    BUS_DMA_NOWAIT))) {
		printf(": unable to map data, error = %d\n", error);
		bus_dmamem_free(sc->bce_dmatag, &dseg, drseg);
		return;
	}

	/* create a dma map for the RX ring */
	if ((error = bus_dmamap_create(sc->bce_dmatag, BCE_NRXDESC * MCLBYTES,
	    1, BCE_NRXDESC * MCLBYTES, 0, BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW,
	    &sc->bce_rxdata_map))) {
		printf(": unable to create ring DMA map, error = %d\n", error);
		bus_dmamem_free(sc->bce_dmatag, &dseg, drseg);
		return;
	}

	/* connect the ring space to the dma map */
	if (bus_dmamap_load(sc->bce_dmatag, sc->bce_rxdata_map, sc->bce_data,
	    BCE_NRXDESC * MCLBYTES, NULL, BUS_DMA_READ | BUS_DMA_NOWAIT)) {
		printf(": unable to load rx ring DMA map\n");
		bus_dmamap_destroy(sc->bce_dmatag, sc->bce_rxdata_map);
		bus_dmamem_free(sc->bce_dmatag, &dseg, drseg);
		return;
	}

	/* create a dma map for the TX ring */
	if ((error = bus_dmamap_create(sc->bce_dmatag, BCE_NTXDESC * MCLBYTES,
	    1, BCE_NTXDESC * MCLBYTES, 0, BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW,
	    &sc->bce_txdata_map))) {
		printf(": unable to create ring DMA map, error = %d\n", error);
		bus_dmamap_destroy(sc->bce_dmatag, sc->bce_rxdata_map);
		bus_dmamem_free(sc->bce_dmatag, &dseg, drseg);
		return;
	}

	/* connect the ring space to the dma map */
	if (bus_dmamap_load(sc->bce_dmatag, sc->bce_txdata_map,
	    sc->bce_data + BCE_NRXDESC * MCLBYTES,
	    BCE_NTXDESC * MCLBYTES, NULL, BUS_DMA_WRITE | BUS_DMA_NOWAIT)) {
		printf(": unable to load tx ring DMA map\n");
		bus_dmamap_destroy(sc->bce_dmatag, sc->bce_rxdata_map);
		bus_dmamap_destroy(sc->bce_dmatag, sc->bce_txdata_map);
		bus_dmamem_free(sc->bce_dmatag, &dseg, drseg);
		return;
	}


	/*
	 * Allocate DMA-safe memory for ring descriptors.
	 * The receive, and transmit rings can not share the same
	 * 4k space, however both are allocated at once here.
	 */
	/*
	 * XXX PAGE_SIZE is wasteful; we only need 1KB + 1KB, but
	 * due to the limitation above. ??
	 */
	if ((error = bus_dmamem_alloc_range(sc->bce_dmatag, 2 * PAGE_SIZE,
	    PAGE_SIZE, 2 * PAGE_SIZE, &seg, 1, &rseg, BUS_DMA_NOWAIT,
	    (bus_addr_t)0, (bus_addr_t)0x3fffffff))) {
		printf(": unable to alloc space for ring descriptors, "
		    "error = %d\n", error);
		bus_dmamap_destroy(sc->bce_dmatag, sc->bce_rxdata_map);
		bus_dmamap_destroy(sc->bce_dmatag, sc->bce_txdata_map);
		bus_dmamem_free(sc->bce_dmatag, &dseg, drseg);
		return;
	}

	/* map ring space to kernel */
	if ((error = bus_dmamem_map(sc->bce_dmatag, &seg, rseg,
	    2 * PAGE_SIZE, &kva, BUS_DMA_NOWAIT))) {
		printf(": unable to map DMA buffers, error = %d\n", error);
		bus_dmamap_destroy(sc->bce_dmatag, sc->bce_rxdata_map);
		bus_dmamap_destroy(sc->bce_dmatag, sc->bce_txdata_map);
		bus_dmamem_free(sc->bce_dmatag, &seg, rseg);
		bus_dmamem_free(sc->bce_dmatag, &dseg, drseg);
		return;
	}

	/* create a dma map for the ring */
	if ((error = bus_dmamap_create(sc->bce_dmatag, 2 * PAGE_SIZE, 1,
	    2 * PAGE_SIZE, 0, BUS_DMA_NOWAIT, &sc->bce_ring_map))) {
		printf(": unable to create ring DMA map, error = %d\n", error);
		bus_dmamap_destroy(sc->bce_dmatag, sc->bce_rxdata_map);
		bus_dmamap_destroy(sc->bce_dmatag, sc->bce_txdata_map);
		bus_dmamem_free(sc->bce_dmatag, &seg, rseg);
		bus_dmamem_free(sc->bce_dmatag, &dseg, drseg);
		return;
	}

	/* connect the ring space to the dma map */
	if (bus_dmamap_load(sc->bce_dmatag, sc->bce_ring_map, kva,
	    2 * PAGE_SIZE, NULL, BUS_DMA_NOWAIT)) {
		printf(": unable to load ring DMA map\n");
		bus_dmamap_destroy(sc->bce_dmatag, sc->bce_rxdata_map);
		bus_dmamap_destroy(sc->bce_dmatag, sc->bce_txdata_map);
		bus_dmamap_destroy(sc->bce_dmatag, sc->bce_ring_map);
		bus_dmamem_free(sc->bce_dmatag, &seg, rseg);
		bus_dmamem_free(sc->bce_dmatag, &dseg, drseg);
		return;
	}

	/* save the ring space in softc */
	sc->bce_rx_ring = (struct bce_dma_slot *)kva;
	sc->bce_tx_ring = (struct bce_dma_slot *)(kva + PAGE_SIZE);

	/* Set up ifnet structure */
	ifp = &sc->bce_ac.ac_if;
	strlcpy(ifp->if_xname, sc->bce_dev.dv_xname, IF_NAMESIZE);
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = bce_ioctl;
	ifp->if_start = bce_start;
	ifp->if_watchdog = bce_watchdog;

	ifp->if_capabilities = IFCAP_VLAN_MTU;

	/* MAC address */
	sc->bce_ac.ac_enaddr[0] =
	    bus_space_read_1(sc->bce_btag, sc->bce_bhandle, BCE_ENET0);
	sc->bce_ac.ac_enaddr[1] =
	    bus_space_read_1(sc->bce_btag, sc->bce_bhandle, BCE_ENET1);
	sc->bce_ac.ac_enaddr[2] =
	    bus_space_read_1(sc->bce_btag, sc->bce_bhandle, BCE_ENET2);
	sc->bce_ac.ac_enaddr[3] =
	    bus_space_read_1(sc->bce_btag, sc->bce_bhandle, BCE_ENET3);
	sc->bce_ac.ac_enaddr[4] =
	    bus_space_read_1(sc->bce_btag, sc->bce_bhandle, BCE_ENET4);
	sc->bce_ac.ac_enaddr[5] =
	    bus_space_read_1(sc->bce_btag, sc->bce_bhandle, BCE_ENET5);

	printf(": %s, address %s\n", intrstr,
	    ether_sprintf(sc->bce_ac.ac_enaddr));

	/* Initialize our media structures and probe the MII. */
	sc->bce_mii.mii_ifp = ifp;
	sc->bce_mii.mii_readreg = bce_mii_read;
	sc->bce_mii.mii_writereg = bce_mii_write;
	sc->bce_mii.mii_statchg = bce_statchg;
	ifmedia_init(&sc->bce_mii.mii_media, 0, bce_mediachange,
	    bce_mediastatus);
	mii_attach(&sc->bce_dev, &sc->bce_mii, 0xffffffff, MII_PHY_ANY,
	    MII_OFFSET_ANY, 0);
	if (LIST_FIRST(&sc->bce_mii.mii_phys) == NULL) {
		ifmedia_add(&sc->bce_mii.mii_media, IFM_ETHER | IFM_NONE, 0, NULL);
		ifmedia_set(&sc->bce_mii.mii_media, IFM_ETHER | IFM_NONE);
	} else
		ifmedia_set(&sc->bce_mii.mii_media, IFM_ETHER | IFM_AUTO);

	/* get the phy */
	sc->bce_phy = bus_space_read_1(sc->bce_btag, sc->bce_bhandle,
	    BCE_PHY) & 0x1f;

	/*
	 * Enable activity led.
	 * XXX This should be in a phy driver, but not currently.
	 */
	bce_mii_write((struct device *) sc, 1, 26,	 /* MAGIC */
	    bce_mii_read((struct device *) sc, 1, 26) & 0x7fff);	 /* MAGIC */

	/* enable traffic meter led mode */
	bce_mii_write((struct device *) sc, 1, 27,	 /* MAGIC */
	    bce_mii_read((struct device *) sc, 1, 27) | (1 << 6));	 /* MAGIC */

	/* Attach the interface */
	if_attach(ifp);
	ether_ifattach(ifp);

	timeout_set(&sc->bce_timeout, bce_tick, sc);
}

int
bce_activate(struct device *self, int act)
{
	struct bce_softc *sc = (struct bce_softc *)self;
	struct ifnet *ifp = &sc->bce_ac.ac_if;

	switch (act) {
	case DVACT_SUSPEND:
		if (ifp->if_flags & IFF_RUNNING)
			bce_stop(ifp);
		break;
	case DVACT_RESUME:
		bce_reset(sc);
		if (ifp->if_flags & IFF_UP) {
			bce_init(ifp);
			bce_start(ifp);
		}
		break;
	}
	return (0);
}

/* handle media, and ethernet requests */
int
bce_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct bce_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *) data;
	int s, error = 0;

	s = splnet();

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		if (!(ifp->if_flags & IFF_RUNNING))
			bce_init(ifp);
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				error = ENETRESET;
			else
				bce_init(ifp);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				bce_stop(ifp);
		}
		break;

	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->bce_mii.mii_media, cmd);
		break;

	default:
		error = ether_ioctl(ifp, &sc->bce_ac, cmd, data);
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			bce_iff(ifp);
		error = 0;
	}

	splx(s);
	return error;
}

/* Start packet transmission on the interface. */
void
bce_start(struct ifnet *ifp)
{
	struct bce_softc *sc = ifp->if_softc;
	struct mbuf *m0;
	u_int32_t ctrl;
	int txstart;
	int txsfree;
	int newpkts = 0;

	/*
	 * do not start another if currently transmitting, and more
	 * descriptors(tx slots) are needed for next packet.
	 */
	if (!(ifp->if_flags & IFF_RUNNING) || ifq_is_oactive(&ifp->if_snd))
		return;

	/* determine number of descriptors available */
	if (sc->bce_txsnext >= sc->bce_txin)
		txsfree = BCE_NTXDESC - 1 + sc->bce_txin - sc->bce_txsnext;
	else
		txsfree = sc->bce_txin - sc->bce_txsnext - 1;

	/*
	 * Loop through the send queue, setting up transmit descriptors
	 * until we drain the queue, or use up all available transmit
	 * descriptors.
	 */
	while (txsfree > 0) {

		/* Grab a packet off the queue. */
		m0 = ifq_dequeue(&ifp->if_snd);
		if (m0 == NULL)
			break;

		/*
		 * copy mbuf chain into DMA memory buffer.
		 */
		m_copydata(m0, 0, m0->m_pkthdr.len, sc->bce_data +
		    (sc->bce_txsnext + BCE_NRXDESC) * MCLBYTES);
		ctrl = m0->m_pkthdr.len & CTRL_BC_MASK;
		ctrl |= CTRL_SOF | CTRL_EOF | CTRL_IOC;

#if NBPFILTER > 0
		/* Pass the packet to any BPF listeners. */
		if (ifp->if_bpf)
			bpf_mtap(ifp->if_bpf, m0, BPF_DIRECTION_OUT);
#endif
		/* mbuf no longer needed */
		m_freem(m0);

		/* Sync the data DMA map. */
		bus_dmamap_sync(sc->bce_dmatag, sc->bce_txdata_map,
		    sc->bce_txsnext * MCLBYTES, MCLBYTES, BUS_DMASYNC_PREWRITE);

		/* Initialize the transmit descriptor(s). */
		txstart = sc->bce_txsnext;

		if (sc->bce_txsnext == BCE_NTXDESC - 1)
			ctrl |= CTRL_EOT;
		sc->bce_tx_ring[sc->bce_txsnext].ctrl = htole32(ctrl);
		sc->bce_tx_ring[sc->bce_txsnext].addr =
		    htole32(sc->bce_txdata_map->dm_segs[0].ds_addr +
		    sc->bce_txsnext * MCLBYTES + 0x40000000);	/* MAGIC */
		if (sc->bce_txsnext + 1 > BCE_NTXDESC - 1)
			sc->bce_txsnext = 0;
		else
			sc->bce_txsnext++;
		txsfree--;

		/* sync descriptors being used */
		bus_dmamap_sync(sc->bce_dmatag, sc->bce_ring_map,
		    sizeof(struct bce_dma_slot) * txstart + PAGE_SIZE,
		    sizeof(struct bce_dma_slot),
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

		/* Give the packet to the chip. */
		bus_space_write_4(sc->bce_btag, sc->bce_bhandle, BCE_DMA_DPTR,
		    sc->bce_txsnext * sizeof(struct bce_dma_slot));

		newpkts++;
	}
	if (txsfree == 0) {
		/* No more slots left; notify upper layer. */
		ifq_set_oactive(&ifp->if_snd);
	}
	if (newpkts) {
		/* Set a watchdog timer in case the chip flakes out. */
		ifp->if_timer = 5;
	}
}

/* Watchdog timer handler. */
void
bce_watchdog(struct ifnet *ifp)
{
	struct bce_softc *sc = ifp->if_softc;

	printf("%s: device timeout\n", sc->bce_dev.dv_xname);
	ifp->if_oerrors++;

	(void) bce_init(ifp);

	/* Try to get more packets going. */
	bce_start(ifp);
}

int
bce_intr(void *xsc)
{
	struct bce_softc *sc;
	struct ifnet *ifp;
	u_int32_t intstatus;
	int wantinit;
	int handled = 0;

	sc = xsc;
	ifp = &sc->bce_ac.ac_if;


	for (wantinit = 0; wantinit == 0;) {
		intstatus = bus_space_read_4(sc->bce_btag, sc->bce_bhandle,
		    BCE_INT_STS);

		/* ignore if not ours, or unsolicited interrupts */
		intstatus &= sc->bce_intmask;
		if (intstatus == 0)
			break;

		handled = 1;

		/* Ack interrupt */
		bus_space_write_4(sc->bce_btag, sc->bce_bhandle, BCE_INT_STS,
		    intstatus);

		/* Receive interrupts. */
		if (intstatus & I_RI)
			bce_rxintr(sc);
		/* Transmit interrupts. */
		if (intstatus & I_XI)
			bce_txintr(sc);
		/* Error interrupts */
		if (intstatus & ~(I_RI | I_XI)) {
			if (intstatus & I_XU)
				printf("%s: transmit fifo underflow\n",
				    sc->bce_dev.dv_xname);
			if (intstatus & I_RO) {
				printf("%s: receive fifo overflow\n",
				    sc->bce_dev.dv_xname);
				ifp->if_ierrors++;
			}
			if (intstatus & I_RU)
				printf("%s: receive descriptor underflow\n",
				    sc->bce_dev.dv_xname);
			if (intstatus & I_DE)
				printf("%s: descriptor protocol error\n",
				    sc->bce_dev.dv_xname);
			if (intstatus & I_PD)
				printf("%s: data error\n",
				    sc->bce_dev.dv_xname);
			if (intstatus & I_PC)
				printf("%s: descriptor error\n",
				    sc->bce_dev.dv_xname);
			if (intstatus & I_TO)
				printf("%s: general purpose timeout\n",
				    sc->bce_dev.dv_xname);
			wantinit = 1;
		}
	}

	if (handled) {
		if (wantinit)
			bce_init(ifp);
		/* Try to get more packets going. */
		bce_start(ifp);
	}
	return (handled);
}

/* Receive interrupt handler */
void
bce_rxintr(struct bce_softc *sc)
{
	struct ifnet *ifp = &sc->bce_ac.ac_if;
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct rx_pph *pph;
	struct mbuf *m;
	int curr;
	int len;
	int i;

	/* get pointer to active receive slot */
	curr = bus_space_read_4(sc->bce_btag, sc->bce_bhandle, BCE_DMA_RXSTATUS)
	    & RS_CD_MASK;
	curr = curr / sizeof(struct bce_dma_slot);
	if (curr >= BCE_NRXDESC)
		curr = BCE_NRXDESC - 1;

	/* process packets up to but not current packet being worked on */
	for (i = sc->bce_rxin; i != curr; i = (i + 1) % BCE_NRXDESC) {
		/* complete any post dma memory ops on packet */
		bus_dmamap_sync(sc->bce_dmatag, sc->bce_rxdata_map,
		    i * MCLBYTES, MCLBYTES, BUS_DMASYNC_POSTREAD);

		/*
		 * If the packet had an error, simply recycle the buffer,
		 * resetting the len, and flags.
		 */
		pph = (struct rx_pph *)(sc->bce_data + i * MCLBYTES);
		if (pph->flags & (RXF_NO | RXF_RXER | RXF_CRC | RXF_OV)) {
			ifp->if_ierrors++;
			pph->len = 0;
			pph->flags = 0;
			continue;
		}
		/* receive the packet */
		len = pph->len;
		if (len == 0)
			continue;	/* no packet if empty */
		pph->len = 0;
		pph->flags = 0;

 		/*
		 * The chip includes the CRC with every packet.  Trim
		 * it off here.
		 */
		len -= ETHER_CRC_LEN;

		m = m_devget(sc->bce_data + i * MCLBYTES +
		    BCE_PREPKT_HEADER_SIZE, len, ETHER_ALIGN);

		ml_enqueue(&ml, m);

		/* re-check current in case it changed */
		curr = (bus_space_read_4(sc->bce_btag, sc->bce_bhandle,
		    BCE_DMA_RXSTATUS) & RS_CD_MASK) /
		    sizeof(struct bce_dma_slot);
		if (curr >= BCE_NRXDESC)
			curr = BCE_NRXDESC - 1;
	}

	if_input(ifp, &ml);

	sc->bce_rxin = curr;
}

/* Transmit interrupt handler */
void
bce_txintr(struct bce_softc *sc)
{
	struct ifnet   *ifp = &sc->bce_ac.ac_if;
	int curr;
	int i;

	ifq_clr_oactive(&ifp->if_snd);

	/*
	 * Go through the Tx list and free mbufs for those
	 * frames which have been transmitted.
	 */
	curr = bus_space_read_4(sc->bce_btag, sc->bce_bhandle,
	    BCE_DMA_TXSTATUS) & RS_CD_MASK;
	curr = curr / sizeof(struct bce_dma_slot);
	if (curr >= BCE_NTXDESC)
		curr = BCE_NTXDESC - 1;
	for (i = sc->bce_txin; i != curr; i = (i + 1) % BCE_NTXDESC) {
		/* do any post dma memory ops on transmit data */
		bus_dmamap_sync(sc->bce_dmatag, sc->bce_txdata_map,
		    i * MCLBYTES, MCLBYTES, BUS_DMASYNC_POSTWRITE);
	}
	sc->bce_txin = curr;

	/*
	 * If there are no more pending transmissions, cancel the watchdog
	 * timer
	 */
	if (sc->bce_txsnext == sc->bce_txin)
		ifp->if_timer = 0;
}

/* initialize the interface */
int
bce_init(struct ifnet *ifp)
{
	struct bce_softc *sc = ifp->if_softc;
	u_int32_t reg_win;
	int i;

	/* Cancel any pending I/O. */
	bce_stop(ifp);

	/* enable pci interrupts, bursts, and prefetch */

	/* remap the pci registers to the Sonics config registers */

	/* save the current map, so it can be restored */
	reg_win = pci_conf_read(sc->bce_pa.pa_pc, sc->bce_pa.pa_tag,
	    BCE_REG_WIN);

	/* set register window to Sonics registers */
	pci_conf_write(sc->bce_pa.pa_pc, sc->bce_pa.pa_tag, BCE_REG_WIN,
	    BCE_SONICS_WIN);

	/* enable SB to PCI interrupt */
	bus_space_write_4(sc->bce_btag, sc->bce_bhandle, BCE_SBINTVEC,
	    bus_space_read_4(sc->bce_btag, sc->bce_bhandle, BCE_SBINTVEC) |
	    SBIV_ENET0);

	/* enable prefetch and bursts for sonics-to-pci translation 2 */
	bus_space_write_4(sc->bce_btag, sc->bce_bhandle, BCE_SPCI_TR2,
	    bus_space_read_4(sc->bce_btag, sc->bce_bhandle, BCE_SPCI_TR2) |
	    SBTOPCI_PREF | SBTOPCI_BURST);

	/* restore to ethernet register space */
	pci_conf_write(sc->bce_pa.pa_pc, sc->bce_pa.pa_tag, BCE_REG_WIN,
	    reg_win);

	/* Reset the chip to a known state. */
	bce_reset(sc);

	/* Initialize transmit descriptors */
	memset(sc->bce_tx_ring, 0, BCE_NTXDESC * sizeof(struct bce_dma_slot));
	sc->bce_txsnext = 0;
	sc->bce_txin = 0;

	/* enable crc32 generation and set proper LED modes */
	bus_space_write_4(sc->bce_btag, sc->bce_bhandle, BCE_MACCTL,
	    bus_space_read_4(sc->bce_btag, sc->bce_bhandle, BCE_MACCTL) |
	    BCE_EMC_CRC32_ENAB | BCE_EMC_LED);

	/* reset or clear powerdown control bit  */
	bus_space_write_4(sc->bce_btag, sc->bce_bhandle, BCE_MACCTL,
	    bus_space_read_4(sc->bce_btag, sc->bce_bhandle, BCE_MACCTL) &
	    ~BCE_EMC_PDOWN);

	/* setup DMA interrupt control */
	bus_space_write_4(sc->bce_btag, sc->bce_bhandle, BCE_DMAI_CTL, 1 << 24);	/* MAGIC */

	/* program promiscuous mode and multicast filters */
	bce_iff(ifp);

	/* set max frame length, account for possible VLAN tag */
	bus_space_write_4(sc->bce_btag, sc->bce_bhandle, BCE_RX_MAX,
	    ETHER_MAX_LEN + ETHER_VLAN_ENCAP_LEN);
	bus_space_write_4(sc->bce_btag, sc->bce_bhandle, BCE_TX_MAX,
	    ETHER_MAX_LEN + ETHER_VLAN_ENCAP_LEN);

	/* set tx watermark */
	bus_space_write_4(sc->bce_btag, sc->bce_bhandle, BCE_TX_WATER, 56);

	/* enable transmit */
	bus_space_write_4(sc->bce_btag, sc->bce_bhandle, BCE_DMA_TXCTL, XC_XE);
	bus_space_write_4(sc->bce_btag, sc->bce_bhandle, BCE_DMA_TXADDR,
	    sc->bce_ring_map->dm_segs[0].ds_addr + PAGE_SIZE + 0x40000000);	/* MAGIC */

	/*
	 * Give the receive ring to the chip, and
	 * start the receive DMA engine.
	 */
	sc->bce_rxin = 0;

	/* clear the rx descriptor ring */
	memset(sc->bce_rx_ring, 0, BCE_NRXDESC * sizeof(struct bce_dma_slot));
	/* enable receive */
	bus_space_write_4(sc->bce_btag, sc->bce_bhandle, BCE_DMA_RXCTL,
	    BCE_PREPKT_HEADER_SIZE << 1 | XC_XE);
	bus_space_write_4(sc->bce_btag, sc->bce_bhandle, BCE_DMA_RXADDR,
	    sc->bce_ring_map->dm_segs[0].ds_addr + 0x40000000);		/* MAGIC */

	/* Initialize receive descriptors */
	for (i = 0; i < BCE_NRXDESC; i++)
		bce_add_rxbuf(sc, i);

	/* Enable interrupts */
	sc->bce_intmask =
	    I_XI | I_RI | I_XU | I_RO | I_RU | I_DE | I_PD | I_PC | I_TO;
	bus_space_write_4(sc->bce_btag, sc->bce_bhandle, BCE_INT_MASK,
	    sc->bce_intmask);

	/* start the receive dma */
	bus_space_write_4(sc->bce_btag, sc->bce_bhandle, BCE_DMA_RXDPTR,
	    BCE_NRXDESC * sizeof(struct bce_dma_slot));

	/* set media */
	mii_mediachg(&sc->bce_mii);

	/* turn on the ethernet mac */
	bus_space_write_4(sc->bce_btag, sc->bce_bhandle, BCE_ENET_CTL,
	    bus_space_read_4(sc->bce_btag, sc->bce_bhandle,
	    BCE_ENET_CTL) | EC_EE);

	/* start timer */
	timeout_add_sec(&sc->bce_timeout, 1);

	/* mark as running, and no outputs active */
	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	return 0;
}

/* add a mac address to packet filter */
void
bce_add_mac(struct bce_softc *sc, u_int8_t *mac, unsigned long idx)
{
	int i;
	u_int32_t rval;

	bus_space_write_4(sc->bce_btag, sc->bce_bhandle, BCE_FILT_LOW,
	    mac[2] << 24 | mac[3] << 16 | mac[4] << 8 | mac[5]);
	bus_space_write_4(sc->bce_btag, sc->bce_bhandle, BCE_FILT_HI,
	    mac[0] << 8 | mac[1] | 0x10000);	/* MAGIC */
	bus_space_write_4(sc->bce_btag, sc->bce_bhandle, BCE_FILT_CTL,
	    idx << 16 | 8);	/* MAGIC */
	/* wait for write to complete */
	for (i = 0; i < 100; i++) {
		rval = bus_space_read_4(sc->bce_btag, sc->bce_bhandle,
		    BCE_FILT_CTL);
		if (!(rval & 0x80000000))	/* MAGIC */
			break;
		delay(10);
	}
	if (i == 100) {
		printf("%s: timed out writing pkt filter ctl\n",
		   sc->bce_dev.dv_xname);
	}
}

/* Add a receive buffer to the indicated descriptor. */
void
bce_add_rxbuf(struct bce_softc *sc, int idx)
{
	struct bce_dma_slot *bced = &sc->bce_rx_ring[idx];

	bus_dmamap_sync(sc->bce_dmatag, sc->bce_rxdata_map, idx * MCLBYTES,
	    MCLBYTES, BUS_DMASYNC_PREREAD);

	*(u_int32_t *)(sc->bce_data + idx * MCLBYTES) = 0;
	bced->addr = htole32(sc->bce_rxdata_map->dm_segs[0].ds_addr +
	    idx * MCLBYTES + 0x40000000);
	if (idx != (BCE_NRXDESC - 1))
		bced->ctrl = htole32(BCE_RXBUF_LEN);
	else
		bced->ctrl = htole32(BCE_RXBUF_LEN | CTRL_EOT);

	bus_dmamap_sync(sc->bce_dmatag, sc->bce_ring_map,
	    sizeof(struct bce_dma_slot) * idx,
	    sizeof(struct bce_dma_slot),
	    BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);

}

/* Stop transmission on the interface */
void
bce_stop(struct ifnet *ifp)
{
	struct bce_softc *sc = ifp->if_softc;
	int i;
	u_int32_t val;

	/* Stop the 1 second timer */
	timeout_del(&sc->bce_timeout);

	/* Mark the interface down and cancel the watchdog timer. */
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
	ifp->if_timer = 0;

	/* Down the MII. */
	mii_down(&sc->bce_mii);

	/* Disable interrupts. */
	bus_space_write_4(sc->bce_btag, sc->bce_bhandle, BCE_INT_MASK, 0);
	sc->bce_intmask = 0;
	delay(10);

	/* Disable emac */
	bus_space_write_4(sc->bce_btag, sc->bce_bhandle, BCE_ENET_CTL, EC_ED);
	for (i = 0; i < 200; i++) {
		val = bus_space_read_4(sc->bce_btag, sc->bce_bhandle,
		    BCE_ENET_CTL);
		if (!(val & EC_ED))
			break;
		delay(10);
	}

	/* Stop the DMA */
	bus_space_write_4(sc->bce_btag, sc->bce_bhandle, BCE_DMA_RXCTL, 0);
	bus_space_write_4(sc->bce_btag, sc->bce_bhandle, BCE_DMA_TXCTL, 0);
	delay(10);
}

/* reset the chip */
void
bce_reset(struct bce_softc *sc)
{
	u_int32_t val;
	u_int32_t sbval;
	int i;

	/* if SB core is up */
	sbval = bus_space_read_4(sc->bce_btag, sc->bce_bhandle,
	    BCE_SBTMSTATELOW);
	if ((sbval & (SBTML_RESET | SBTML_REJ | SBTML_CLK)) == SBTML_CLK) {
		bus_space_write_4(sc->bce_btag, sc->bce_bhandle, BCE_DMAI_CTL,
		    0);

		/* disable emac */
		bus_space_write_4(sc->bce_btag, sc->bce_bhandle, BCE_ENET_CTL,
		    EC_ED);
		for (i = 0; i < 200; i++) {
			val = bus_space_read_4(sc->bce_btag, sc->bce_bhandle,
			    BCE_ENET_CTL);
			if (!(val & EC_ED))
				break;
			delay(10);
		}
		if (i == 200)
			printf("%s: timed out disabling ethernet mac\n",
			    sc->bce_dev.dv_xname);

		/* reset the dma engines */
		bus_space_write_4(sc->bce_btag, sc->bce_bhandle, BCE_DMA_TXCTL,
		    0);
		val = bus_space_read_4(sc->bce_btag, sc->bce_bhandle,
		    BCE_DMA_RXSTATUS);
		/* if error on receive, wait to go idle */
		if (val & RS_ERROR) {
			for (i = 0; i < 100; i++) {
				val = bus_space_read_4(sc->bce_btag,
				    sc->bce_bhandle, BCE_DMA_RXSTATUS);
				if (val & RS_DMA_IDLE)
					break;
				delay(10);
			}
			if (i == 100)
				printf("%s: receive dma did not go idle after"
				    " error\n", sc->bce_dev.dv_xname);
		}
		bus_space_write_4(sc->bce_btag, sc->bce_bhandle,
		   BCE_DMA_RXSTATUS, 0);

		/* reset ethernet mac */
		bus_space_write_4(sc->bce_btag, sc->bce_bhandle, BCE_ENET_CTL,
		    EC_ES);
		for (i = 0; i < 200; i++) {
			val = bus_space_read_4(sc->bce_btag, sc->bce_bhandle,
			    BCE_ENET_CTL);
			if (!(val & EC_ES))
				break;
			delay(10);
		}
		if (i == 200)
			printf("%s: timed out resetting ethernet mac\n",
			    sc->bce_dev.dv_xname);
	} else {
		u_int32_t reg_win;

		/* remap the pci registers to the Sonics config registers */

		/* save the current map, so it can be restored */
		reg_win = pci_conf_read(sc->bce_pa.pa_pc, sc->bce_pa.pa_tag,
		    BCE_REG_WIN);
		/* set register window to Sonics registers */
		pci_conf_write(sc->bce_pa.pa_pc, sc->bce_pa.pa_tag,
		    BCE_REG_WIN, BCE_SONICS_WIN);

		/* enable SB to PCI interrupt */
		bus_space_write_4(sc->bce_btag, sc->bce_bhandle, BCE_SBINTVEC,
		    bus_space_read_4(sc->bce_btag, sc->bce_bhandle,
		    BCE_SBINTVEC) | SBIV_ENET0);

		/* enable prefetch and bursts for sonics-to-pci translation 2 */
		bus_space_write_4(sc->bce_btag, sc->bce_bhandle, BCE_SPCI_TR2,
		    bus_space_read_4(sc->bce_btag, sc->bce_bhandle,
		    BCE_SPCI_TR2) | SBTOPCI_PREF | SBTOPCI_BURST);

		/* restore to ethernet register space */
		pci_conf_write(sc->bce_pa.pa_pc, sc->bce_pa.pa_tag, BCE_REG_WIN,
		    reg_win);
	}

	/* disable SB core if not in reset */
	if (!(sbval & SBTML_RESET)) {

		/* set the reject bit */
		bus_space_write_4(sc->bce_btag, sc->bce_bhandle,
		    BCE_SBTMSTATELOW, SBTML_REJ | SBTML_CLK);
		for (i = 0; i < 200; i++) {
			val = bus_space_read_4(sc->bce_btag, sc->bce_bhandle,
			    BCE_SBTMSTATELOW);
			if (val & SBTML_REJ)
				break;
			delay(1);
		}
		if (i == 200)
			printf("%s: while resetting core, reject did not set\n",
			    sc->bce_dev.dv_xname);
		/* wait until busy is clear */
		for (i = 0; i < 200; i++) {
			val = bus_space_read_4(sc->bce_btag, sc->bce_bhandle,
			    BCE_SBTMSTATEHI);
			if (!(val & 0x4))
				break;
			delay(1);
		}
		if (i == 200)
			printf("%s: while resetting core, busy did not clear\n",
			    sc->bce_dev.dv_xname);
		/* set reset and reject while enabling the clocks */
		bus_space_write_4(sc->bce_btag, sc->bce_bhandle,
		    BCE_SBTMSTATELOW,
		    SBTML_FGC | SBTML_CLK | SBTML_REJ | SBTML_RESET);
		val = bus_space_read_4(sc->bce_btag, sc->bce_bhandle,
		    BCE_SBTMSTATELOW);
		delay(10);
		bus_space_write_4(sc->bce_btag, sc->bce_bhandle,
		    BCE_SBTMSTATELOW, SBTML_REJ | SBTML_RESET);
		delay(1);
	}
	/* enable clock */
	bus_space_write_4(sc->bce_btag, sc->bce_bhandle, BCE_SBTMSTATELOW,
	    SBTML_FGC | SBTML_CLK | SBTML_RESET);
	val = bus_space_read_4(sc->bce_btag, sc->bce_bhandle, BCE_SBTMSTATELOW);
	delay(1);

	/* clear any error bits that may be on */
	val = bus_space_read_4(sc->bce_btag, sc->bce_bhandle, BCE_SBTMSTATEHI);
	if (val & 1)
		bus_space_write_4(sc->bce_btag, sc->bce_bhandle, BCE_SBTMSTATEHI,
		    0);
	val = bus_space_read_4(sc->bce_btag, sc->bce_bhandle, BCE_SBIMSTATE);
	if (val & SBIM_ERRORBITS)
		bus_space_write_4(sc->bce_btag, sc->bce_bhandle, BCE_SBIMSTATE,
		    val & ~SBIM_ERRORBITS);

	/* clear reset and allow it to propagate throughout the core */
	bus_space_write_4(sc->bce_btag, sc->bce_bhandle, BCE_SBTMSTATELOW,
	    SBTML_FGC | SBTML_CLK);
	val = bus_space_read_4(sc->bce_btag, sc->bce_bhandle, BCE_SBTMSTATELOW);
	delay(1);

	/* leave clock enabled */
	bus_space_write_4(sc->bce_btag, sc->bce_bhandle, BCE_SBTMSTATELOW,
	    SBTML_CLK);
	val = bus_space_read_4(sc->bce_btag, sc->bce_bhandle, BCE_SBTMSTATELOW);
	delay(1);

	/* initialize MDC preamble, frequency */
	bus_space_write_4(sc->bce_btag, sc->bce_bhandle, BCE_MI_CTL, 0x8d);	/* MAGIC */

	/* enable phy, differs for internal, and external */
	val = bus_space_read_4(sc->bce_btag, sc->bce_bhandle, BCE_DEVCTL);
	if (!(val & BCE_DC_IP)) {
		/* select external phy */
		bus_space_write_4(sc->bce_btag, sc->bce_bhandle, BCE_ENET_CTL,
		    EC_EP);
	} else if (val & BCE_DC_ER) {	/* internal, clear reset bit if on */
		bus_space_write_4(sc->bce_btag, sc->bce_bhandle, BCE_DEVCTL,
		    val & ~BCE_DC_ER);
		delay(100);
	}
}

/* Set up the receive filter. */
void
bce_iff(struct ifnet *ifp)
{
	struct bce_softc *sc = ifp->if_softc;
	struct arpcom *ac = &sc->bce_ac;
	u_int32_t rxctl;

	rxctl = bus_space_read_4(sc->bce_btag, sc->bce_bhandle, BCE_RX_CTL);
	rxctl &= ~(ERC_AM | ERC_DB | ERC_PE);
	ifp->if_flags |= IFF_ALLMULTI;

	/* disable the filter */
	bus_space_write_4(sc->bce_btag, sc->bce_bhandle, BCE_FILT_CTL, 0);

	/* add our own address */
	bce_add_mac(sc, ac->ac_enaddr, 0);

	if (ifp->if_flags & IFF_PROMISC || ac->ac_multicnt > 0) {
		ifp->if_flags |= IFF_ALLMULTI;
		if (ifp->if_flags & IFF_PROMISC)
			rxctl |= ERC_PE;
		else
			rxctl |= ERC_AM;
	}

	bus_space_write_4(sc->bce_btag, sc->bce_bhandle, BCE_RX_CTL, rxctl);

	/* enable the filter */
	bus_space_write_4(sc->bce_btag, sc->bce_bhandle, BCE_FILT_CTL,
	    bus_space_read_4(sc->bce_btag, sc->bce_bhandle, BCE_FILT_CTL) | 1);
}

/* Read a PHY register on the MII. */
int
bce_mii_read(struct device *self, int phy, int reg)
{
	struct bce_softc *sc = (struct bce_softc *) self;
	int i;
	u_int32_t val;

	/* clear mii_int */
	bus_space_write_4(sc->bce_btag, sc->bce_bhandle, BCE_MI_STS,
	    BCE_MIINTR);

	/* Read the PHY register */
	bus_space_write_4(sc->bce_btag, sc->bce_bhandle, BCE_MI_COMM,
	    (MII_COMMAND_READ << 28) | (MII_COMMAND_START << 30) |	/* MAGIC */
	    (MII_COMMAND_ACK << 16) | BCE_MIPHY(phy) | BCE_MIREG(reg));	/* MAGIC */

	for (i = 0; i < BCE_TIMEOUT; i++) {
		val = bus_space_read_4(sc->bce_btag, sc->bce_bhandle,
		    BCE_MI_STS);
		if (val & BCE_MIINTR)
			break;
		delay(10);
	}
	val = bus_space_read_4(sc->bce_btag, sc->bce_bhandle, BCE_MI_COMM);
	if (i == BCE_TIMEOUT) {
		printf("%s: PHY read timed out reading phy %d, reg %d, val = "
		    "0x%08x\n", sc->bce_dev.dv_xname, phy, reg, val);
		return (0);
	}
	return (val & BCE_MICOMM_DATA);
}

/* Write a PHY register on the MII */
void
bce_mii_write(struct device *self, int phy, int reg, int val)
{
	struct bce_softc *sc = (struct bce_softc *) self;
	int i;
	u_int32_t rval;

	/* clear mii_int */
	bus_space_write_4(sc->bce_btag, sc->bce_bhandle, BCE_MI_STS,
	    BCE_MIINTR);

	/* Write the PHY register */
	bus_space_write_4(sc->bce_btag, sc->bce_bhandle, BCE_MI_COMM,
	    (MII_COMMAND_WRITE << 28) | (MII_COMMAND_START << 30) |	/* MAGIC */
	    (MII_COMMAND_ACK << 16) | (val & BCE_MICOMM_DATA) |	/* MAGIC */
	    BCE_MIPHY(phy) | BCE_MIREG(reg));

	/* wait for write to complete */
	for (i = 0; i < BCE_TIMEOUT; i++) {
		rval = bus_space_read_4(sc->bce_btag, sc->bce_bhandle,
		    BCE_MI_STS);
		if (rval & BCE_MIINTR)
			break;
		delay(10);
	}
	rval = bus_space_read_4(sc->bce_btag, sc->bce_bhandle, BCE_MI_COMM);
	if (i == BCE_TIMEOUT) {
		printf("%s: PHY timed out writing phy %d, reg %d, val "
		    "= 0x%08x\n", sc->bce_dev.dv_xname, phy, reg, val);
	}
}

/* sync hardware duplex mode to software state */
void
bce_statchg(struct device *self)
{
	struct bce_softc *sc = (struct bce_softc *) self;
	u_int32_t reg;

	/* if needed, change register to match duplex mode */
	reg = bus_space_read_4(sc->bce_btag, sc->bce_bhandle, BCE_TX_CTL);
	if (sc->bce_mii.mii_media_active & IFM_FDX && !(reg & EXC_FD))
		bus_space_write_4(sc->bce_btag, sc->bce_bhandle, BCE_TX_CTL,
		    reg | EXC_FD);
	else if (!(sc->bce_mii.mii_media_active & IFM_FDX) && reg & EXC_FD)
		bus_space_write_4(sc->bce_btag, sc->bce_bhandle, BCE_TX_CTL,
		    reg & ~EXC_FD);

	/*
	 * Enable activity led.
	 * XXX This should be in a phy driver, but not currently.
	 */
	bce_mii_write((struct device *) sc, 1, 26,	/* MAGIC */
	    bce_mii_read((struct device *) sc, 1, 26) & 0x7fff);	/* MAGIC */
	/* enable traffic meter led mode */
	bce_mii_write((struct device *) sc, 1, 26,	/* MAGIC */
	    bce_mii_read((struct device *) sc, 1, 27) | (1 << 6));	/* MAGIC */
}

/* Set hardware to newly-selected media */
int
bce_mediachange(struct ifnet *ifp)
{
	struct bce_softc *sc = ifp->if_softc;

	if (ifp->if_flags & IFF_UP)
		mii_mediachg(&sc->bce_mii);
	return (0);
}

/* Get the current interface media status */
void
bce_mediastatus(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct bce_softc *sc = ifp->if_softc;

	mii_pollstat(&sc->bce_mii);
	ifmr->ifm_active = sc->bce_mii.mii_media_active;
	ifmr->ifm_status = sc->bce_mii.mii_media_status;
}

/* One second timer, checks link status */
void
bce_tick(void *v)
{
	struct bce_softc *sc = v;
	int s;

	s = splnet();
	mii_tick(&sc->bce_mii);
	splx(s);

	timeout_add_sec(&sc->bce_timeout, 1);
}
