/*	$OpenBSD: if_vic.c,v 1.107 2025/06/13 10:26:30 jsg Exp $	*/

/*
 * Copyright (c) 2006 Reyk Floeter <reyk@openbsd.org>
 * Copyright (c) 2006 David Gwynne <dlg@openbsd.org>
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
 * Driver for the VMware Virtual NIC ("vmxnet")
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/malloc.h>
#include <sys/timeout.h>
#include <sys/device.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <net/if.h>
#include <net/if_media.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#define VIC_PCI_BAR		PCI_MAPREG_START /* Base Address Register */

#define VIC_LANCE_SIZE		0x20
#define VIC_MORPH_SIZE		0x04
#define  VIC_MORPH_MASK			0xffff
#define  VIC_MORPH_LANCE		0x2934
#define  VIC_MORPH_VMXNET		0x4392
#define VIC_VMXNET_SIZE		0x40
#define VIC_LANCE_MINLEN	(VIC_LANCE_SIZE + VIC_MORPH_SIZE + \
				    VIC_VMXNET_SIZE)

#define VIC_MAGIC		0xbabe864f

/* Register address offsets */
#define VIC_DATA_ADDR		0x0000		/* Shared data address */
#define VIC_DATA_LENGTH		0x0004		/* Shared data length */
#define VIC_Tx_ADDR		0x0008		/* Tx pointer address */

/* Command register */
#define VIC_CMD			0x000c		/* Command register */
#define  VIC_CMD_INTR_ACK	0x0001	/* Acknowledge interrupt */
#define  VIC_CMD_MCASTFIL	0x0002	/* Multicast address filter */
#define   VIC_CMD_MCASTFIL_LENGTH	2
#define  VIC_CMD_IFF		0x0004	/* Interface flags */
#define   VIC_CMD_IFF_PROMISC	0x0001		/* Promiscuous enabled */
#define   VIC_CMD_IFF_BROADCAST	0x0002		/* Broadcast enabled */
#define   VIC_CMD_IFF_MULTICAST	0x0004		/* Multicast enabled */
#define  VIC_CMD_INTR_DISABLE	0x0020	/* Disable interrupts */
#define  VIC_CMD_INTR_ENABLE	0x0040	/* Enable interrupts */
#define  VIC_CMD_Tx_DONE	0x0100	/* Tx done register */
#define  VIC_CMD_NUM_Rx_BUF	0x0200	/* Number of Rx buffers */
#define  VIC_CMD_NUM_Tx_BUF	0x0400	/* Number of Tx buffers */
#define  VIC_CMD_NUM_PINNED_BUF	0x0800	/* Number of pinned buffers */
#define  VIC_CMD_HWCAP		0x1000	/* Capability register */
#define   VIC_CMD_HWCAP_SG		(1<<0) /* Scatter-gather transmits */
#define   VIC_CMD_HWCAP_CSUM_IPv4	(1<<1) /* TCP/UDP cksum */
#define   VIC_CMD_HWCAP_CSUM_ALL	(1<<3) /* Hardware cksum */
#define   VIC_CMD_HWCAP_CSUM \
	(VIC_CMD_HWCAP_CSUM_IPv4 | VIC_CMD_HWCAP_CSUM_ALL)
#define   VIC_CMD_HWCAP_DMA_HIGH		(1<<4) /* High DMA mapping */
#define   VIC_CMD_HWCAP_TOE		(1<<5) /* TCP offload engine */
#define   VIC_CMD_HWCAP_TSO		(1<<6) /* TCP segmentation offload */
#define   VIC_CMD_HWCAP_TSO_SW		(1<<7) /* Software TCP segmentation */
#define   VIC_CMD_HWCAP_VPROM		(1<<8) /* Virtual PROM available */
#define   VIC_CMD_HWCAP_VLAN_Tx		(1<<9) /* Hardware VLAN MTU Rx */
#define   VIC_CMD_HWCAP_VLAN_Rx		(1<<10) /* Hardware VLAN MTU Tx */
#define   VIC_CMD_HWCAP_VLAN_SW		(1<<11)	/* Software VLAN MTU */
#define   VIC_CMD_HWCAP_VLAN \
	(VIC_CMD_HWCAP_VLAN_Tx | VIC_CMD_HWCAP_VLAN_Rx | \
	VIC_CMD_HWCAP_VLAN_SW)
#define  VIC_CMD_HWCAP_BITS \
	"\20\01SG\02CSUM4\03CSUM\04HDMA\05TOE\06TSO" \
	"\07TSOSW\10VPROM\13VLANTx\14VLANRx\15VLANSW"
#define  VIC_CMD_FEATURE	0x2000	/* Additional feature register */
#define   VIC_CMD_FEATURE_0_Tx		(1<<0)
#define   VIC_CMD_FEATURE_TSO		(1<<1)

#define VIC_LLADDR		0x0010		/* MAC address register */
#define VIC_VERSION_MINOR	0x0018		/* Minor version register */
#define VIC_VERSION_MAJOR	0x001c		/* Major version register */
#define VIC_VERSION_MAJOR_M	0xffff0000

/* Status register */
#define VIC_STATUS		0x0020
#define  VIC_STATUS_CONNECTED		(1<<0)
#define  VIC_STATUS_ENABLED		(1<<1)

#define VIC_TOE_ADDR		0x0024		/* TCP offload address */

/* Virtual PROM address */
#define VIC_VPROM		0x0028
#define VIC_VPROM_LENGTH	6

/* Shared DMA data structures */

struct vic_sg {
	u_int32_t	sg_addr_low;
	u_int16_t	sg_addr_high;
	u_int16_t	sg_length;
} __packed;

#define VIC_SG_MAX		6
#define VIC_SG_ADDR_MACH	0
#define VIC_SG_ADDR_PHYS	1
#define VIC_SG_ADDR_VIRT	3

struct vic_sgarray {
	u_int16_t	sa_addr_type;
	u_int16_t	sa_length;
	struct vic_sg	sa_sg[VIC_SG_MAX];
} __packed;

struct vic_rxdesc {
	u_int64_t	rx_physaddr;
	u_int32_t	rx_buflength;
	u_int32_t	rx_length;
	u_int16_t	rx_owner;
	u_int16_t	rx_flags;
	u_int32_t	rx_priv;
} __packed;

#define VIC_RX_FLAGS_CSUMHW_OK	0x0001

struct vic_txdesc {
	u_int16_t		tx_flags;
	u_int16_t		tx_owner;
	u_int32_t		tx_priv;
	u_int32_t		tx_tsomss;
	struct vic_sgarray	tx_sa;
} __packed;

#define VIC_TX_FLAGS_KEEP	0x0001
#define VIC_TX_FLAGS_TXURN	0x0002
#define VIC_TX_FLAGS_CSUMHW	0x0004
#define VIC_TX_FLAGS_TSO	0x0008
#define VIC_TX_FLAGS_PINNED	0x0010
#define VIC_TX_FLAGS_QRETRY	0x1000

struct vic_stats {
	u_int32_t		vs_tx_count;
	u_int32_t		vs_tx_packets;
	u_int32_t		vs_tx_0copy;
	u_int32_t		vs_tx_copy;
	u_int32_t		vs_tx_maxpending;
	u_int32_t		vs_tx_stopped;
	u_int32_t		vs_tx_overrun;
	u_int32_t		vs_intr;
	u_int32_t		vs_rx_packets;
	u_int32_t		vs_rx_underrun;
} __packed;

#define VIC_NRXRINGS		2

struct vic_data {
	u_int32_t		vd_magic;

	struct {
		u_int32_t		length;
		u_int32_t		nextidx;
	}			vd_rx[VIC_NRXRINGS];

	u_int32_t		vd_irq;
	u_int32_t		vd_iff;

	u_int32_t		vd_mcastfil[VIC_CMD_MCASTFIL_LENGTH];

	u_int32_t		vd_reserved1[1];

	u_int32_t		vd_tx_length;
	u_int32_t		vd_tx_curidx;
	u_int32_t		vd_tx_nextidx;
	u_int32_t		vd_tx_stopped;
	u_int32_t		vd_tx_triggerlvl;
	u_int32_t		vd_tx_queued;
	u_int32_t		vd_tx_minlength;

	u_int32_t		vd_reserved2[6];

	u_int32_t		vd_rx_saved_nextidx[VIC_NRXRINGS];
	u_int32_t		vd_tx_saved_nextidx;

	u_int32_t		vd_length;
	u_int32_t		vd_rx_offset[VIC_NRXRINGS];
	u_int32_t		vd_tx_offset;
	u_int32_t		vd_debug;
	u_int32_t		vd_tx_physaddr;
	u_int32_t		vd_tx_physaddr_length;
	u_int32_t		vd_tx_maxlength;

	struct vic_stats	vd_stats;
} __packed;

#define VIC_OWNER_DRIVER	0
#define VIC_OWNER_DRIVER_PEND	1
#define VIC_OWNER_NIC		2
#define VIC_OWNER_NIC_PEND	3

#define VIC_JUMBO_FRAMELEN	9018
#define VIC_JUMBO_MTU		(VIC_JUMBO_FRAMELEN - ETHER_HDR_LEN - ETHER_CRC_LEN)

#define VIC_NBUF		100
#define VIC_NBUF_MAX		128
#define VIC_MAX_SCATTER		1	/* 8? */
#define VIC_QUEUE_SIZE		VIC_NBUF_MAX
#define VIC_INC(_x, _y)		(_x) = ((_x) + 1) % (_y)
#define VIC_TX_TIMEOUT		5

#define VIC_MIN_FRAMELEN	(ETHER_MIN_LEN - ETHER_CRC_LEN)

#define VIC_TXURN_WARN(_sc)	((_sc)->sc_txpending >= ((_sc)->sc_ntxbuf - 5))
#define VIC_TXURN(_sc)		((_sc)->sc_txpending >= (_sc)->sc_ntxbuf)

struct vic_rxbuf {
	bus_dmamap_t		rxb_dmamap;
	struct mbuf		*rxb_m;
};

struct vic_txbuf {
	bus_dmamap_t		txb_dmamap;
	struct mbuf		*txb_m;
};

struct vic_softc {
	struct device		sc_dev;

	pci_chipset_tag_t	sc_pc;
	pcitag_t		sc_tag;

	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	bus_size_t		sc_ios;
	bus_dma_tag_t		sc_dmat;

	void			*sc_ih;

	struct timeout		sc_tick;

	struct arpcom		sc_ac;
	struct ifmedia		sc_media;

	u_int32_t		sc_nrxbuf;
	u_int32_t		sc_ntxbuf;
	u_int32_t		sc_cap;
	u_int32_t		sc_feature;
	u_int8_t		sc_lladdr[ETHER_ADDR_LEN];

	bus_dmamap_t		sc_dma_map;
	bus_dma_segment_t	sc_dma_seg;
	size_t			sc_dma_size;
	caddr_t			sc_dma_kva;
#define VIC_DMA_DVA(_sc)	((_sc)->sc_dma_map->dm_segs[0].ds_addr)
#define VIC_DMA_KVA(_sc)	((void *)(_sc)->sc_dma_kva)

	struct vic_data		*sc_data;

	struct {
		struct if_rxring	ring;
		struct vic_rxbuf	*bufs;
		struct vic_rxdesc	*slots;
		int			end;
		u_int			pktlen;
	}			sc_rxq[VIC_NRXRINGS];

	struct vic_txbuf	*sc_txbuf;
	struct vic_txdesc	*sc_txq;
	volatile u_int		sc_txpending;
};

struct cfdriver vic_cd = {
	NULL, "vic", DV_IFNET
};

int		vic_match(struct device *, void *, void *);
void		vic_attach(struct device *, struct device *, void *);

const struct cfattach vic_ca = {
	sizeof(struct vic_softc), vic_match, vic_attach
};

int		vic_intr(void *);

int		vic_query(struct vic_softc *);
int		vic_alloc_data(struct vic_softc *);
int		vic_init_data(struct vic_softc *sc);
int		vic_uninit_data(struct vic_softc *sc);

u_int32_t	vic_read(struct vic_softc *, bus_size_t);
void		vic_write(struct vic_softc *, bus_size_t, u_int32_t);

u_int32_t	vic_read_cmd(struct vic_softc *, u_int32_t);

int		vic_alloc_dmamem(struct vic_softc *);
void		vic_free_dmamem(struct vic_softc *);

void		vic_link_state(struct vic_softc *);
void		vic_rx_fill(struct vic_softc *, int);
void		vic_rx_proc(struct vic_softc *, int);
void		vic_tx_proc(struct vic_softc *);
void		vic_iff(struct vic_softc *);
void		vic_getlladdr(struct vic_softc *);
void		vic_setlladdr(struct vic_softc *);
int		vic_media_change(struct ifnet *);
void		vic_media_status(struct ifnet *, struct ifmediareq *);
void		vic_start(struct ifnet *);
int		vic_load_txb(struct vic_softc *, struct vic_txbuf *,
		    struct mbuf *);
void		vic_watchdog(struct ifnet *);
int		vic_ioctl(struct ifnet *, u_long, caddr_t);
int		vic_rxrinfo(struct vic_softc *, struct if_rxrinfo *);
void		vic_init(struct ifnet *);
void		vic_stop(struct ifnet *);
void		vic_tick(void *);

#define DEVNAME(_s)	((_s)->sc_dev.dv_xname)

struct mbuf *vic_alloc_mbuf(struct vic_softc *, bus_dmamap_t, u_int);

int
vic_match(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args		*pa = aux;
	pcireg_t			memtype;
	bus_size_t			pcisize;
	bus_addr_t			pciaddr;

	switch (pa->pa_id) {
	case PCI_ID_CODE(PCI_VENDOR_VMWARE, PCI_PRODUCT_VMWARE_NET):
		return (1);

	case PCI_ID_CODE(PCI_VENDOR_AMD, PCI_PRODUCT_AMD_PCNET_PCI):
		memtype = pci_mapreg_type(pa->pa_pc, pa->pa_tag, VIC_PCI_BAR);
		if (pci_mapreg_info(pa->pa_pc, pa->pa_tag, VIC_PCI_BAR,
		    memtype, &pciaddr, &pcisize, NULL) != 0)
			break;

		if (pcisize > VIC_LANCE_MINLEN)
			return (2);

		break;
	}

	return (0);
}

void
vic_attach(struct device *parent, struct device *self, void *aux)
{
	struct vic_softc		*sc = (struct vic_softc *)self;
	struct pci_attach_args		*pa = aux;
	bus_space_handle_t		ioh;
	pcireg_t			r;
	pci_intr_handle_t		ih;
	struct ifnet			*ifp;

	sc->sc_pc = pa->pa_pc;
	sc->sc_tag = pa->pa_tag;
	sc->sc_dmat = pa->pa_dmat;

	r = pci_mapreg_type(sc->sc_pc, sc->sc_tag, VIC_PCI_BAR);
	if (pci_mapreg_map(pa, VIC_PCI_BAR, r, 0, &sc->sc_iot,
	    &ioh, NULL, &sc->sc_ios, 0) != 0) {
		printf(": unable to map system interface register\n");
		return;
	}

	switch (pa->pa_id) {
	case PCI_ID_CODE(PCI_VENDOR_VMWARE, PCI_PRODUCT_VMWARE_NET):
		if (bus_space_subregion(sc->sc_iot, ioh, 0, sc->sc_ios,
		    &sc->sc_ioh) != 0) {
			printf(": unable to map register window\n");
			goto unmap;
		}
		break;

	case PCI_ID_CODE(PCI_VENDOR_AMD, PCI_PRODUCT_AMD_PCNET_PCI):
		if (bus_space_subregion(sc->sc_iot, ioh, 
		    VIC_LANCE_SIZE + VIC_MORPH_SIZE, VIC_VMXNET_SIZE,
		    &sc->sc_ioh) != 0) {
			printf(": unable to map register window\n");
			goto unmap;
		}

		bus_space_barrier(sc->sc_iot, ioh, VIC_LANCE_SIZE, 4,
		    BUS_SPACE_BARRIER_READ);
		r = bus_space_read_4(sc->sc_iot, ioh, VIC_LANCE_SIZE);

		if ((r & VIC_MORPH_MASK) == VIC_MORPH_VMXNET)
			break;
		if ((r & VIC_MORPH_MASK) != VIC_MORPH_LANCE) {
			printf(": unexpected morph value (0x%08x)\n", r);
			goto unmap;
		}

		r &= ~VIC_MORPH_MASK;
		r |= VIC_MORPH_VMXNET;

		bus_space_write_4(sc->sc_iot, ioh, VIC_LANCE_SIZE, r);
		bus_space_barrier(sc->sc_iot, ioh, VIC_LANCE_SIZE, 4,
		    BUS_SPACE_BARRIER_WRITE);

		bus_space_barrier(sc->sc_iot, ioh, VIC_LANCE_SIZE, 4,
		    BUS_SPACE_BARRIER_READ);
		r = bus_space_read_4(sc->sc_iot, ioh, VIC_LANCE_SIZE);

		if ((r & VIC_MORPH_MASK) != VIC_MORPH_VMXNET) {
			printf(": unable to morph vlance chip\n");
			goto unmap;
		}

		break;
	}

	if (pci_intr_map(pa, &ih) != 0) {
		printf(": unable to map interrupt\n");
		goto unmap;
	}

	sc->sc_ih = pci_intr_establish(pa->pa_pc, ih, IPL_NET,
	    vic_intr, sc, DEVNAME(sc));
	if (sc->sc_ih == NULL) {
		printf(": unable to establish interrupt\n");
		goto unmap;
	}

	if (vic_query(sc) != 0) {
		/* error printed by vic_query */
		goto unmap;
	}

	if (vic_alloc_data(sc) != 0) {
		/* error printed by vic_alloc */
		goto unmap;
	}

	timeout_set(&sc->sc_tick, vic_tick, sc);

	bcopy(sc->sc_lladdr, sc->sc_ac.ac_enaddr, ETHER_ADDR_LEN);

	ifp = &sc->sc_ac.ac_if;
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = vic_ioctl;
	ifp->if_start = vic_start;
	ifp->if_watchdog = vic_watchdog;
	ifp->if_hardmtu = VIC_JUMBO_MTU;
	strlcpy(ifp->if_xname, DEVNAME(sc), IFNAMSIZ);
	ifq_init_maxlen(&ifp->if_snd, sc->sc_ntxbuf - 1);

	ifp->if_capabilities = IFCAP_VLAN_MTU;

#if 0
	/* XXX interface capabilities */
	if (sc->sc_cap & VIC_CMD_HWCAP_VLAN)
		ifp->if_capabilities |= IFCAP_VLAN_HWTAGGING;
	if (sc->sc_cap & VIC_CMD_HWCAP_CSUM)
		ifp->if_capabilities |= IFCAP_CSUM_IPv4 | IFCAP_CSUM_TCPv4 |
		    IFCAP_CSUM_UDPv4;
#endif

	ifmedia_init(&sc->sc_media, 0, vic_media_change, vic_media_status);
	ifmedia_add(&sc->sc_media, IFM_ETHER | IFM_AUTO, 0, NULL);
	ifmedia_set(&sc->sc_media, IFM_ETHER | IFM_AUTO);

	if_attach(ifp);
	ether_ifattach(ifp);

	printf(": %s, address %s\n", pci_intr_string(pa->pa_pc, ih),
	    ether_sprintf(sc->sc_lladdr));

#ifdef VIC_DEBUG
	printf("%s: feature 0x%8x, cap 0x%8x, rx/txbuf %d/%d\n", DEVNAME(sc),
	    sc->sc_feature, sc->sc_cap, sc->sc_nrxbuf, sc->sc_ntxbuf);
#endif

	return;

unmap:
	bus_space_unmap(sc->sc_iot, ioh, sc->sc_ios);
	sc->sc_ios = 0;
}

int
vic_query(struct vic_softc *sc)
{
	u_int32_t			major, minor;

	major = vic_read(sc, VIC_VERSION_MAJOR);
	minor = vic_read(sc, VIC_VERSION_MINOR);

	/* Check for a supported version */
	if ((major & VIC_VERSION_MAJOR_M) !=
	    (VIC_MAGIC & VIC_VERSION_MAJOR_M)) {
		printf(": magic mismatch\n");
		return (1);
	}

	if (VIC_MAGIC > major || VIC_MAGIC < minor) {
		printf(": unsupported version (%X)\n",
		    major & ~VIC_VERSION_MAJOR_M);
		return (1);
	}

	sc->sc_nrxbuf = vic_read_cmd(sc, VIC_CMD_NUM_Rx_BUF);
	sc->sc_ntxbuf = vic_read_cmd(sc, VIC_CMD_NUM_Tx_BUF);
	sc->sc_feature = vic_read_cmd(sc, VIC_CMD_FEATURE);
	sc->sc_cap = vic_read_cmd(sc, VIC_CMD_HWCAP);

	vic_getlladdr(sc);

	if (sc->sc_nrxbuf > VIC_NBUF_MAX || sc->sc_nrxbuf == 0)
		sc->sc_nrxbuf = VIC_NBUF;
	if (sc->sc_ntxbuf > VIC_NBUF_MAX || sc->sc_ntxbuf == 0)
		sc->sc_ntxbuf = VIC_NBUF;

	return (0);
}

int
vic_alloc_data(struct vic_softc *sc)
{
	u_int8_t			*kva;
	u_int				offset;
	struct vic_rxdesc		*rxd;
	int				i, q;

	sc->sc_rxq[0].pktlen = MCLBYTES;
	sc->sc_rxq[1].pktlen = 4096;

	for (q = 0; q < VIC_NRXRINGS; q++) {
		sc->sc_rxq[q].bufs = mallocarray(sc->sc_nrxbuf,
		    sizeof(struct vic_rxbuf), M_DEVBUF, M_NOWAIT | M_ZERO);
		if (sc->sc_rxq[q].bufs == NULL) {
			printf(": unable to allocate rxbuf for ring %d\n", q);
			goto freerx;
		}
	}

	sc->sc_txbuf = mallocarray(sc->sc_ntxbuf, sizeof(struct vic_txbuf),
	    M_DEVBUF, M_NOWAIT);
	if (sc->sc_txbuf == NULL) {
		printf(": unable to allocate txbuf\n");
		goto freerx;
	}

	sc->sc_dma_size = sizeof(struct vic_data) +
	    (sc->sc_nrxbuf * VIC_NRXRINGS) * sizeof(struct vic_rxdesc) +
	    sc->sc_ntxbuf * sizeof(struct vic_txdesc);

	if (vic_alloc_dmamem(sc) != 0) {
		printf(": unable to allocate dma region\n");
		goto freetx;
	}
	kva = VIC_DMA_KVA(sc);

	/* set up basic vic data */
	sc->sc_data = VIC_DMA_KVA(sc);

	sc->sc_data->vd_magic = VIC_MAGIC;
	sc->sc_data->vd_length = sc->sc_dma_size;

	offset = sizeof(struct vic_data);

	/* set up the rx rings */

	for (q = 0; q < VIC_NRXRINGS; q++) {
		sc->sc_rxq[q].slots = (struct vic_rxdesc *)&kva[offset];
		sc->sc_data->vd_rx_offset[q] = offset;
		sc->sc_data->vd_rx[q].length = sc->sc_nrxbuf;

		for (i = 0; i < sc->sc_nrxbuf; i++) {
			rxd = &sc->sc_rxq[q].slots[i];

			rxd->rx_physaddr = 0;
			rxd->rx_buflength = 0;
			rxd->rx_length = 0;
			rxd->rx_owner = VIC_OWNER_DRIVER;

			offset += sizeof(struct vic_rxdesc);
		}
	}

	/* set up the tx ring */
	sc->sc_txq = (struct vic_txdesc *)&kva[offset];

	sc->sc_data->vd_tx_offset = offset;
	sc->sc_data->vd_tx_length = sc->sc_ntxbuf;

	return (0);
freetx:
	free(sc->sc_txbuf, M_DEVBUF, 0);
	q = VIC_NRXRINGS;
freerx:
	while (q--)
		free(sc->sc_rxq[q].bufs, M_DEVBUF, 0);

	return (1);
}

void
vic_rx_fill(struct vic_softc *sc, int q)
{
	struct vic_rxbuf		*rxb;
	struct vic_rxdesc		*rxd;
	u_int				slots;

	for (slots = if_rxr_get(&sc->sc_rxq[q].ring, sc->sc_nrxbuf);
	    slots > 0; slots--) {
		rxb = &sc->sc_rxq[q].bufs[sc->sc_rxq[q].end];
		rxd = &sc->sc_rxq[q].slots[sc->sc_rxq[q].end];

		rxb->rxb_m = vic_alloc_mbuf(sc, rxb->rxb_dmamap,
		    sc->sc_rxq[q].pktlen);
		if (rxb->rxb_m == NULL)
			break;

		bus_dmamap_sync(sc->sc_dmat, rxb->rxb_dmamap, 0,
		    rxb->rxb_m->m_pkthdr.len, BUS_DMASYNC_PREREAD);

		rxd->rx_physaddr = rxb->rxb_dmamap->dm_segs[0].ds_addr;
		rxd->rx_buflength = rxb->rxb_m->m_pkthdr.len;
		rxd->rx_length = 0;
		rxd->rx_owner = VIC_OWNER_NIC;

		VIC_INC(sc->sc_rxq[q].end, sc->sc_data->vd_rx[q].length);
	}
	if_rxr_put(&sc->sc_rxq[q].ring, slots);
}

int
vic_init_data(struct vic_softc *sc)
{
	struct vic_rxbuf		*rxb;
	struct vic_rxdesc		*rxd;
	struct vic_txbuf		*txb;

	int				q, i;

	for (q = 0; q < VIC_NRXRINGS; q++) {
		for (i = 0; i < sc->sc_nrxbuf; i++) {
			rxb = &sc->sc_rxq[q].bufs[i];
			rxd = &sc->sc_rxq[q].slots[i];

			if (bus_dmamap_create(sc->sc_dmat,
			    sc->sc_rxq[q].pktlen, 1, sc->sc_rxq[q].pktlen, 0,
			    BUS_DMA_NOWAIT, &rxb->rxb_dmamap) != 0) {
				printf("%s: unable to create dmamap for "
				    "ring %d slot %d\n", DEVNAME(sc), q, i);
				goto freerxbs;
			}

			/* scrub the ring */
			rxd->rx_physaddr = 0;
			rxd->rx_buflength = 0;
			rxd->rx_length = 0;
			rxd->rx_owner = VIC_OWNER_DRIVER;
		}
		sc->sc_rxq[q].end = 0;

		if_rxr_init(&sc->sc_rxq[q].ring, 2, sc->sc_nrxbuf - 1);
		vic_rx_fill(sc, q);
	}

	for (i = 0; i < sc->sc_ntxbuf; i++) {
		txb = &sc->sc_txbuf[i];
		if (bus_dmamap_create(sc->sc_dmat, VIC_JUMBO_FRAMELEN,
		    (sc->sc_cap & VIC_CMD_HWCAP_SG) ? VIC_SG_MAX : 1,
		    VIC_JUMBO_FRAMELEN, 0, BUS_DMA_NOWAIT,
		    &txb->txb_dmamap) != 0) {
			printf("%s: unable to create dmamap for tx %d\n",
			    DEVNAME(sc), i);
			goto freetxbs;
		}
		txb->txb_m = NULL;
	}

	return (0);

freetxbs:
	while (i--) {
		txb = &sc->sc_txbuf[i];
		bus_dmamap_destroy(sc->sc_dmat, txb->txb_dmamap);
	}

	i = sc->sc_nrxbuf;
	q = VIC_NRXRINGS - 1;
freerxbs:
	while (q >= 0) {
		while (i--) {
			rxb = &sc->sc_rxq[q].bufs[i];

			if (rxb->rxb_m != NULL) {
				bus_dmamap_sync(sc->sc_dmat, rxb->rxb_dmamap,
				    0, rxb->rxb_m->m_pkthdr.len,
				    BUS_DMASYNC_POSTREAD);
				bus_dmamap_unload(sc->sc_dmat, rxb->rxb_dmamap);
				m_freem(rxb->rxb_m);
				rxb->rxb_m = NULL;
			}
			bus_dmamap_destroy(sc->sc_dmat, rxb->rxb_dmamap);
		}
		q--;
	}

	return (1);
}

int
vic_uninit_data(struct vic_softc *sc)
{
	struct vic_rxbuf		*rxb;
	struct vic_rxdesc		*rxd;
	struct vic_txbuf		*txb;

	int				i, q;

	for (q = 0; q < VIC_NRXRINGS; q++) {
		for (i = 0; i < sc->sc_nrxbuf; i++) {
			rxb = &sc->sc_rxq[q].bufs[i];
			rxd = &sc->sc_rxq[q].slots[i];

			if (rxb->rxb_m != NULL) {
				bus_dmamap_sync(sc->sc_dmat, rxb->rxb_dmamap,
				    0, rxb->rxb_m->m_pkthdr.len,
				    BUS_DMASYNC_POSTREAD);
				bus_dmamap_unload(sc->sc_dmat, rxb->rxb_dmamap);
				m_freem(rxb->rxb_m);
				rxb->rxb_m = NULL;
			}
			bus_dmamap_destroy(sc->sc_dmat, rxb->rxb_dmamap);
		}
	}

	for (i = 0; i < sc->sc_ntxbuf; i++) {
		txb = &sc->sc_txbuf[i];
		bus_dmamap_destroy(sc->sc_dmat, txb->txb_dmamap);
	}

	return (0);
}

void
vic_link_state(struct vic_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	u_int32_t status;
	int link_state = LINK_STATE_DOWN;

	status = vic_read(sc, VIC_STATUS);
	if (status & VIC_STATUS_CONNECTED)
		link_state = LINK_STATE_FULL_DUPLEX;
	if (ifp->if_link_state != link_state) {
		ifp->if_link_state = link_state;
		if_link_state_change(ifp);
	}
}

int
vic_intr(void *arg)
{
	struct vic_softc *sc = (struct vic_softc *)arg;
	int q;

	vic_write(sc, VIC_CMD, VIC_CMD_INTR_ACK);

	for (q = 0; q < VIC_NRXRINGS; q++)
		vic_rx_proc(sc, q);
	vic_tx_proc(sc);

	return (-1);
}

void
vic_rx_proc(struct vic_softc *sc, int q)
{
	struct ifnet			*ifp = &sc->sc_ac.ac_if;
	struct vic_rxdesc		*rxd;
	struct vic_rxbuf		*rxb;
	struct mbuf_list		 ml = MBUF_LIST_INITIALIZER();
	struct mbuf			*m;
	int				len, idx;

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return;

	bus_dmamap_sync(sc->sc_dmat, sc->sc_dma_map, 0, sc->sc_dma_size,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	while (if_rxr_inuse(&sc->sc_rxq[q].ring) > 0) {
		idx = sc->sc_data->vd_rx[q].nextidx;
		if (idx >= sc->sc_data->vd_rx[q].length) {
			ifp->if_ierrors++;
			if (ifp->if_flags & IFF_DEBUG)
				printf("%s: receive index error\n",
				    sc->sc_dev.dv_xname);
			break;
		}

		rxd = &sc->sc_rxq[q].slots[idx];
		if (rxd->rx_owner != VIC_OWNER_DRIVER)
			break;

		rxb = &sc->sc_rxq[q].bufs[idx];

		if (rxb->rxb_m == NULL) {
			ifp->if_ierrors++;
			printf("%s: rxb %d has no mbuf\n", DEVNAME(sc), idx);
			break;
		}

		bus_dmamap_sync(sc->sc_dmat, rxb->rxb_dmamap, 0,
		    rxb->rxb_m->m_pkthdr.len, BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->sc_dmat, rxb->rxb_dmamap);

		m = rxb->rxb_m;
		rxb->rxb_m = NULL;
		len = rxd->rx_length;

		if (len < VIC_MIN_FRAMELEN) {
			m_freem(m);

			ifp->if_iqdrops++;
			goto nextp;
		}

		m->m_pkthdr.len = m->m_len = len;

		ml_enqueue(&ml, m);

 nextp:
		if_rxr_put(&sc->sc_rxq[q].ring, 1);
		VIC_INC(sc->sc_data->vd_rx[q].nextidx, sc->sc_nrxbuf);
	}

	if (ifiq_input(&ifp->if_rcv, &ml))
		if_rxr_livelocked(&sc->sc_rxq[q].ring);

	vic_rx_fill(sc, q);

	bus_dmamap_sync(sc->sc_dmat, sc->sc_dma_map, 0, sc->sc_dma_size,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

void
vic_tx_proc(struct vic_softc *sc)
{
	struct ifnet			*ifp = &sc->sc_ac.ac_if;
	struct vic_txdesc		*txd;
	struct vic_txbuf		*txb;
	int				idx;

	bus_dmamap_sync(sc->sc_dmat, sc->sc_dma_map, 0, sc->sc_dma_size,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	while (sc->sc_txpending > 0) {
		idx = sc->sc_data->vd_tx_curidx;
		if (idx >= sc->sc_data->vd_tx_length) {
			ifp->if_oerrors++;
			break;
		}

		txd = &sc->sc_txq[idx];
		if (txd->tx_owner != VIC_OWNER_DRIVER)
			break;

		txb = &sc->sc_txbuf[idx];
		if (txb->txb_m == NULL) {
			printf("%s: tx ring is corrupt\n", DEVNAME(sc));
			ifp->if_oerrors++;
			break;
		}

		bus_dmamap_sync(sc->sc_dmat, txb->txb_dmamap, 0,
		    txb->txb_dmamap->dm_mapsize, BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, txb->txb_dmamap);

		m_freem(txb->txb_m);
		txb->txb_m = NULL;
		ifq_clr_oactive(&ifp->if_snd);

		sc->sc_txpending--;
		sc->sc_data->vd_tx_stopped = 0;

		VIC_INC(sc->sc_data->vd_tx_curidx, sc->sc_data->vd_tx_length);
	}

	bus_dmamap_sync(sc->sc_dmat, sc->sc_dma_map, 0, sc->sc_dma_size,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	vic_start(ifp);
}

void
vic_iff(struct vic_softc *sc)
{
	struct arpcom *ac = &sc->sc_ac;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct ether_multi *enm;
	struct ether_multistep step;
	u_int32_t crc;
	u_int16_t *mcastfil = (u_int16_t *)sc->sc_data->vd_mcastfil;
	u_int flags;

	ifp->if_flags &= ~IFF_ALLMULTI;

	/* Always accept broadcast frames. */
	flags = VIC_CMD_IFF_BROADCAST;

	if (ifp->if_flags & IFF_PROMISC || ac->ac_multirangecnt > 0) {
		ifp->if_flags |= IFF_ALLMULTI;
		if (ifp->if_flags & IFF_PROMISC)
			flags |= VIC_CMD_IFF_PROMISC;
		else
			flags |= VIC_CMD_IFF_MULTICAST;
		memset(&sc->sc_data->vd_mcastfil, 0xff,
		    sizeof(sc->sc_data->vd_mcastfil));
	} else {
		flags |= VIC_CMD_IFF_MULTICAST;

		bzero(&sc->sc_data->vd_mcastfil,
		    sizeof(sc->sc_data->vd_mcastfil));

		ETHER_FIRST_MULTI(step, ac, enm);
		while (enm != NULL) {
			crc = ether_crc32_le(enm->enm_addrlo, ETHER_ADDR_LEN);

			crc >>= 26;

			mcastfil[crc >> 4] |= htole16(1 << (crc & 0xf));

			ETHER_NEXT_MULTI(step, enm);
		}
	}

	vic_write(sc, VIC_CMD, VIC_CMD_MCASTFIL);
	sc->sc_data->vd_iff = flags;
	vic_write(sc, VIC_CMD, VIC_CMD_IFF);
}

void
vic_getlladdr(struct vic_softc *sc)
{
	u_int32_t reg;

	/* Get MAC address */
	reg = (sc->sc_cap & VIC_CMD_HWCAP_VPROM) ? VIC_VPROM : VIC_LLADDR;

	bus_space_barrier(sc->sc_iot, sc->sc_ioh, reg, ETHER_ADDR_LEN,
	    BUS_SPACE_BARRIER_READ);
	bus_space_read_region_1(sc->sc_iot, sc->sc_ioh, reg, sc->sc_lladdr,
	    ETHER_ADDR_LEN);

	/* Update the MAC address register */
	if (reg == VIC_VPROM)
		vic_setlladdr(sc);
}

void
vic_setlladdr(struct vic_softc *sc)
{
	bus_space_write_region_1(sc->sc_iot, sc->sc_ioh, VIC_LLADDR,
	    sc->sc_lladdr, ETHER_ADDR_LEN);
	bus_space_barrier(sc->sc_iot, sc->sc_ioh, VIC_LLADDR, ETHER_ADDR_LEN,
	    BUS_SPACE_BARRIER_WRITE);
}

int
vic_media_change(struct ifnet *ifp)
{
	/* Ignore */
	return (0);
}

void
vic_media_status(struct ifnet *ifp, struct ifmediareq *imr)
{
	struct vic_softc *sc = (struct vic_softc *)ifp->if_softc;

	imr->ifm_active = IFM_ETHER | IFM_AUTO;
	imr->ifm_status = IFM_AVALID;

	vic_link_state(sc);

	if (LINK_STATE_IS_UP(ifp->if_link_state) &&
	    ifp->if_flags & IFF_UP)
		imr->ifm_status |= IFM_ACTIVE;
}

void
vic_start(struct ifnet *ifp)
{
	struct vic_softc		*sc;
	struct mbuf			*m;
	struct vic_txbuf		*txb;
	struct vic_txdesc		*txd;
	struct vic_sg			*sge;
	bus_dmamap_t			dmap;
	int				i, idx;
	int				tx = 0;

	if (!(ifp->if_flags & IFF_RUNNING))
		return;

	if (ifq_is_oactive(&ifp->if_snd))
		return;

	if (ifq_empty(&ifp->if_snd))
		return;

	sc = (struct vic_softc *)ifp->if_softc;

	bus_dmamap_sync(sc->sc_dmat, sc->sc_dma_map, 0, sc->sc_dma_size,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	for (;;) {
		if (VIC_TXURN(sc)) {
			ifq_set_oactive(&ifp->if_snd);
			break;
		}

		idx = sc->sc_data->vd_tx_nextidx;
		if (idx >= sc->sc_data->vd_tx_length) {
			printf("%s: tx idx is corrupt\n", DEVNAME(sc));
			ifp->if_oerrors++;
			break;
		}

		txd = &sc->sc_txq[idx];
		txb = &sc->sc_txbuf[idx];

		if (txb->txb_m != NULL) {
			printf("%s: tx ring is corrupt\n", DEVNAME(sc));
			sc->sc_data->vd_tx_stopped = 1;
			ifp->if_oerrors++;
			break;
		}

		m = ifq_dequeue(&ifp->if_snd);
		if (m == NULL)
			break;

		if (vic_load_txb(sc, txb, m) != 0) {
			m_freem(m);
			ifp->if_oerrors++;
			continue;
		}

#if NBPFILTER > 0
		if (ifp->if_bpf)
			bpf_mtap(ifp->if_bpf, txb->txb_m, BPF_DIRECTION_OUT);
#endif

		dmap = txb->txb_dmamap;
		txd->tx_flags = VIC_TX_FLAGS_KEEP;
		txd->tx_owner = VIC_OWNER_NIC;
		txd->tx_sa.sa_addr_type = VIC_SG_ADDR_PHYS;
		txd->tx_sa.sa_length = dmap->dm_nsegs;
		for (i = 0; i < dmap->dm_nsegs; i++) {
			sge = &txd->tx_sa.sa_sg[i];
			sge->sg_length = dmap->dm_segs[i].ds_len;
			sge->sg_addr_low = dmap->dm_segs[i].ds_addr;
		}

		if (VIC_TXURN_WARN(sc)) {
			txd->tx_flags |= VIC_TX_FLAGS_TXURN;
		}

		bus_dmamap_sync(sc->sc_dmat, dmap, 0, dmap->dm_mapsize,
		    BUS_DMASYNC_PREWRITE);

		sc->sc_txpending++;

		VIC_INC(sc->sc_data->vd_tx_nextidx, sc->sc_data->vd_tx_length);

		tx = 1;
	}

	bus_dmamap_sync(sc->sc_dmat, sc->sc_dma_map, 0, sc->sc_dma_size,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	if (tx)
		vic_read(sc, VIC_Tx_ADDR);
}

int
vic_load_txb(struct vic_softc *sc, struct vic_txbuf *txb, struct mbuf *m)
{
	bus_dmamap_t			dmap = txb->txb_dmamap;
	int				error;

	error = bus_dmamap_load_mbuf(sc->sc_dmat, dmap, m, BUS_DMA_NOWAIT);
	switch (error) {
	case 0:
		txb->txb_m = m;
		break;

	case EFBIG:
		if (m_defrag(m, M_DONTWAIT) == 0 &&
		    bus_dmamap_load_mbuf(sc->sc_dmat, dmap, m,
		    BUS_DMA_NOWAIT) == 0) {
			txb->txb_m = m;
			break;
		}

		/* FALLTHROUGH */
	default:
		return (ENOBUFS);
	}

	return (0);
}

void
vic_watchdog(struct ifnet *ifp)
{
#if 0
	struct vic_softc *sc = (struct vic_softc *)ifp->if_softc;

	if (sc->sc_txpending && sc->sc_txtimeout > 0) {
		if (--sc->sc_txtimeout == 0) {
			printf("%s: device timeout\n", sc->sc_dev.dv_xname);
			ifp->if_flags &= ~IFF_RUNNING;
			vic_init(ifp);
			ifp->if_oerrors++;
			return;
		}
	}

	if (!ifq_empty(&ifp->if_snd))
		vic_start(ifp);
#endif
}

int
vic_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct vic_softc *sc = (struct vic_softc *)ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int s, error = 0;

	s = splnet();

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		/* FALLTHROUGH */
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				error = ENETRESET;
			else
				vic_init(ifp);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				vic_stop(ifp);
		}
		break;

	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_media, cmd);
		break;

	case SIOCGIFRXR:
		error = vic_rxrinfo(sc, (struct if_rxrinfo *)ifr->ifr_data);
		break;

	default:
		error = ether_ioctl(ifp, &sc->sc_ac, cmd, data);
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			vic_iff(sc);
		error = 0;
	}

	splx(s);
	return (error);
}

int
vic_rxrinfo(struct vic_softc *sc, struct if_rxrinfo *ifri)
{
	struct if_rxring_info ifr[2];

	memset(ifr, 0, sizeof(ifr));

	ifr[0].ifr_size = MCLBYTES;
	ifr[0].ifr_info = sc->sc_rxq[0].ring;

	ifr[1].ifr_size = 4096;
	ifr[1].ifr_info = sc->sc_rxq[1].ring;

	return (if_rxr_info_ioctl(ifri, nitems(ifr), ifr));
}

void
vic_init(struct ifnet *ifp)
{
	struct vic_softc	*sc = (struct vic_softc *)ifp->if_softc;
	int			q;
	int			s;

	sc->sc_data->vd_tx_curidx = 0;
	sc->sc_data->vd_tx_nextidx = 0;
	sc->sc_data->vd_tx_stopped = sc->sc_data->vd_tx_queued = 0;
	sc->sc_data->vd_tx_saved_nextidx = 0;

	for (q = 0; q < VIC_NRXRINGS; q++) {
		sc->sc_data->vd_rx[q].nextidx = 0;
		sc->sc_data->vd_rx_saved_nextidx[q] = 0;
	}

	if (vic_init_data(sc) != 0)
		return;

	bus_dmamap_sync(sc->sc_dmat, sc->sc_dma_map, 0, sc->sc_dma_size,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	s = splnet();

	vic_write(sc, VIC_DATA_ADDR, VIC_DMA_DVA(sc));
	vic_write(sc, VIC_DATA_LENGTH, sc->sc_dma_size);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	vic_iff(sc);
	vic_write(sc, VIC_CMD, VIC_CMD_INTR_ENABLE);

	splx(s);

	timeout_add_sec(&sc->sc_tick, 1);
}

void
vic_stop(struct ifnet *ifp)
{
	struct vic_softc *sc = (struct vic_softc *)ifp->if_softc;
	int s;

	s = splnet();

	timeout_del(&sc->sc_tick);

	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	bus_dmamap_sync(sc->sc_dmat, sc->sc_dma_map, 0, sc->sc_dma_size,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	/* XXX wait for tx to complete */
	while (sc->sc_txpending > 0) {
		splx(s);
		delay(1000);
		s = splnet();
	}

	sc->sc_data->vd_tx_stopped = 1;

	vic_write(sc, VIC_CMD, VIC_CMD_INTR_DISABLE);

	sc->sc_data->vd_iff = 0;
	vic_write(sc, VIC_CMD, VIC_CMD_IFF);

	vic_write(sc, VIC_DATA_ADDR, 0);

	vic_uninit_data(sc);

	splx(s);
}

struct mbuf *
vic_alloc_mbuf(struct vic_softc *sc, bus_dmamap_t map, u_int pktlen)
{
	struct mbuf *m = NULL;

	m = MCLGETL(NULL, M_DONTWAIT, pktlen);
	if (!m)
		return (NULL);
	m->m_data += ETHER_ALIGN;
	m->m_len = m->m_pkthdr.len = pktlen - ETHER_ALIGN;

	if (bus_dmamap_load_mbuf(sc->sc_dmat, map, m, BUS_DMA_NOWAIT) != 0) {
		printf("%s: could not load mbuf DMA map\n", DEVNAME(sc));
		m_freem(m);
		return (NULL);
	}

	return (m);
}

void
vic_tick(void *arg)
{
	struct vic_softc		*sc = (struct vic_softc *)arg;

	vic_link_state(sc);

	timeout_add_sec(&sc->sc_tick, 1);
}

u_int32_t
vic_read(struct vic_softc *sc, bus_size_t r)
{
	bus_space_barrier(sc->sc_iot, sc->sc_ioh, r, 4,
	    BUS_SPACE_BARRIER_READ);
	return (bus_space_read_4(sc->sc_iot, sc->sc_ioh, r));
}

void
vic_write(struct vic_softc *sc, bus_size_t r, u_int32_t v)
{
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, r, v);
	bus_space_barrier(sc->sc_iot, sc->sc_ioh, r, 4,
	    BUS_SPACE_BARRIER_WRITE);
}

u_int32_t
vic_read_cmd(struct vic_softc *sc, u_int32_t cmd)
{
	vic_write(sc, VIC_CMD, cmd);
	return (vic_read(sc, VIC_CMD));
}

int
vic_alloc_dmamem(struct vic_softc *sc)
{
	int nsegs;

	if (bus_dmamap_create(sc->sc_dmat, sc->sc_dma_size, 1,
	    sc->sc_dma_size, 0, BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW,
	    &sc->sc_dma_map) != 0)
		goto err;

	if (bus_dmamem_alloc(sc->sc_dmat, sc->sc_dma_size, 16, 0,
	    &sc->sc_dma_seg, 1, &nsegs, BUS_DMA_NOWAIT | BUS_DMA_ZERO) != 0)
		goto destroy;

	if (bus_dmamem_map(sc->sc_dmat, &sc->sc_dma_seg, nsegs,
	    sc->sc_dma_size, &sc->sc_dma_kva, BUS_DMA_NOWAIT) != 0)
		goto free;

	if (bus_dmamap_load(sc->sc_dmat, sc->sc_dma_map, sc->sc_dma_kva,
	    sc->sc_dma_size, NULL, BUS_DMA_NOWAIT) != 0)
		goto unmap;

	return (0);

unmap:
	bus_dmamem_unmap(sc->sc_dmat, sc->sc_dma_kva, sc->sc_dma_size);
free:
	bus_dmamem_free(sc->sc_dmat, &sc->sc_dma_seg, 1);
destroy:
	bus_dmamap_destroy(sc->sc_dmat, sc->sc_dma_map);
err:
	return (1);
}

void
vic_free_dmamem(struct vic_softc *sc)
{
	bus_dmamap_unload(sc->sc_dmat, sc->sc_dma_map);
	bus_dmamem_unmap(sc->sc_dmat, sc->sc_dma_kva, sc->sc_dma_size);
	bus_dmamem_free(sc->sc_dmat, &sc->sc_dma_seg, 1);
	bus_dmamap_destroy(sc->sc_dmat, sc->sc_dma_map);
}
