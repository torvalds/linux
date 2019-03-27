/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2000 Berkeley Software Design, Inc.
 * Copyright (c) 1997, 1998, 1999, 2000
 *	Bill Paul <wpaul@osd.bsdi.com>.  All rights reserved.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * AMD Am79c972 fast ethernet PCI NIC driver. Datasheets are available
 * from http://www.amd.com.
 *
 * The AMD PCnet/PCI controllers are more advanced and functional
 * versions of the venerable 7990 LANCE. The PCnet/PCI chips retain
 * backwards compatibility with the LANCE and thus can be made
 * to work with older LANCE drivers. This is in fact how the
 * PCnet/PCI chips were supported in FreeBSD originally. The trouble
 * is that the PCnet/PCI devices offer several performance enhancements
 * which can't be exploited in LANCE compatibility mode. Chief among
 * these enhancements is the ability to perform PCI DMA operations
 * using 32-bit addressing (which eliminates the need for ISA
 * bounce-buffering), and special receive buffer alignment (which
 * allows the receive handler to pass packets to the upper protocol
 * layers without copying on both the x86 and alpha platforms).
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>

#include <net/bpf.h>

#include <vm/vm.h>              /* for vtophys */
#include <vm/pmap.h>            /* for vtophys */
#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#define PCN_USEIOSPACE

#include <dev/pcn/if_pcnreg.h>

MODULE_DEPEND(pcn, pci, 1, 1, 1);
MODULE_DEPEND(pcn, ether, 1, 1, 1);
MODULE_DEPEND(pcn, miibus, 1, 1, 1);

/* "device miibus" required.  See GENERIC if you get errors here. */
#include "miibus_if.h"

/*
 * Various supported device vendors/types and their names.
 */
static const struct pcn_type pcn_devs[] = {
	{ PCN_VENDORID, PCN_DEVICEID_PCNET, "AMD PCnet/PCI 10/100BaseTX" },
	{ PCN_VENDORID, PCN_DEVICEID_HOME, "AMD PCnet/Home HomePNA" },
	{ 0, 0, NULL }
};

static const struct pcn_chipid {
	u_int32_t	id;
	const char	*name;
} pcn_chipid[] = {
	{ Am79C971,	"Am79C971" },
	{ Am79C972,	"Am79C972" },
	{ Am79C973,	"Am79C973" },
	{ Am79C978,	"Am79C978" },
	{ Am79C975,	"Am79C975" },
	{ Am79C976,	"Am79C976" },
	{ 0, NULL },
};

static const char *pcn_chipid_name(u_int32_t);
static u_int32_t pcn_chip_id(device_t);
static const struct pcn_type *pcn_match(u_int16_t, u_int16_t);

static u_int32_t pcn_csr_read(struct pcn_softc *, int);
static u_int16_t pcn_csr_read16(struct pcn_softc *, int);
static u_int16_t pcn_bcr_read16(struct pcn_softc *, int);
static void pcn_csr_write(struct pcn_softc *, int, int);
static u_int32_t pcn_bcr_read(struct pcn_softc *, int);
static void pcn_bcr_write(struct pcn_softc *, int, int);

static int pcn_probe(device_t);
static int pcn_attach(device_t);
static int pcn_detach(device_t);

static int pcn_newbuf(struct pcn_softc *, int, struct mbuf *);
static int pcn_encap(struct pcn_softc *, struct mbuf *, u_int32_t *);
static void pcn_rxeof(struct pcn_softc *);
static void pcn_txeof(struct pcn_softc *);
static void pcn_intr(void *);
static void pcn_tick(void *);
static void pcn_start(struct ifnet *);
static void pcn_start_locked(struct ifnet *);
static int pcn_ioctl(struct ifnet *, u_long, caddr_t);
static void pcn_init(void *);
static void pcn_init_locked(struct pcn_softc *);
static void pcn_stop(struct pcn_softc *);
static void pcn_watchdog(struct pcn_softc *);
static int pcn_shutdown(device_t);
static int pcn_ifmedia_upd(struct ifnet *);
static void pcn_ifmedia_sts(struct ifnet *, struct ifmediareq *);

static int pcn_miibus_readreg(device_t, int, int);
static int pcn_miibus_writereg(device_t, int, int, int);
static void pcn_miibus_statchg(device_t);

static void pcn_setfilt(struct ifnet *);
static void pcn_setmulti(struct pcn_softc *);
static void pcn_reset(struct pcn_softc *);
static int pcn_list_rx_init(struct pcn_softc *);
static int pcn_list_tx_init(struct pcn_softc *);

#ifdef PCN_USEIOSPACE
#define PCN_RES			SYS_RES_IOPORT
#define PCN_RID			PCN_PCI_LOIO
#else
#define PCN_RES			SYS_RES_MEMORY
#define PCN_RID			PCN_PCI_LOMEM
#endif

static device_method_t pcn_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		pcn_probe),
	DEVMETHOD(device_attach,	pcn_attach),
	DEVMETHOD(device_detach,	pcn_detach),
	DEVMETHOD(device_shutdown,	pcn_shutdown),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	pcn_miibus_readreg),
	DEVMETHOD(miibus_writereg,	pcn_miibus_writereg),
	DEVMETHOD(miibus_statchg,	pcn_miibus_statchg),

	DEVMETHOD_END
};

static driver_t pcn_driver = {
	"pcn",
	pcn_methods,
	sizeof(struct pcn_softc)
};

static devclass_t pcn_devclass;

DRIVER_MODULE(pcn, pci, pcn_driver, pcn_devclass, 0, 0);
MODULE_PNP_INFO("U16:vendor;U16:device", pci, pcn, pcn_devs,
    nitems(pcn_devs) - 1);
DRIVER_MODULE(miibus, pcn, miibus_driver, miibus_devclass, 0, 0);

#define PCN_CSR_SETBIT(sc, reg, x)			\
	pcn_csr_write(sc, reg, pcn_csr_read(sc, reg) | (x))

#define PCN_CSR_CLRBIT(sc, reg, x)			\
	pcn_csr_write(sc, reg, pcn_csr_read(sc, reg) & ~(x))

#define PCN_BCR_SETBIT(sc, reg, x)			\
	pcn_bcr_write(sc, reg, pcn_bcr_read(sc, reg) | (x))

#define PCN_BCR_CLRBIT(sc, reg, x)			\
	pcn_bcr_write(sc, reg, pcn_bcr_read(sc, reg) & ~(x))

static u_int32_t
pcn_csr_read(sc, reg)
	struct pcn_softc	*sc;
	int			reg;
{
	CSR_WRITE_4(sc, PCN_IO32_RAP, reg);
	return(CSR_READ_4(sc, PCN_IO32_RDP));
}

static u_int16_t
pcn_csr_read16(sc, reg)
	struct pcn_softc	*sc;
	int			reg;
{
	CSR_WRITE_2(sc, PCN_IO16_RAP, reg);
	return(CSR_READ_2(sc, PCN_IO16_RDP));
}

static void
pcn_csr_write(sc, reg, val)
	struct pcn_softc	*sc;
	int			reg;
	int			val;
{
	CSR_WRITE_4(sc, PCN_IO32_RAP, reg);
	CSR_WRITE_4(sc, PCN_IO32_RDP, val);
	return;
}

static u_int32_t
pcn_bcr_read(sc, reg)
	struct pcn_softc	*sc;
	int			reg;
{
	CSR_WRITE_4(sc, PCN_IO32_RAP, reg);
	return(CSR_READ_4(sc, PCN_IO32_BDP));
}

static u_int16_t
pcn_bcr_read16(sc, reg)
	struct pcn_softc	*sc;
	int			reg;
{
	CSR_WRITE_2(sc, PCN_IO16_RAP, reg);
	return(CSR_READ_2(sc, PCN_IO16_BDP));
}

static void
pcn_bcr_write(sc, reg, val)
	struct pcn_softc	*sc;
	int			reg;
	int			val;
{
	CSR_WRITE_4(sc, PCN_IO32_RAP, reg);
	CSR_WRITE_4(sc, PCN_IO32_BDP, val);
	return;
}

static int
pcn_miibus_readreg(dev, phy, reg)
	device_t		dev;
	int			phy, reg;
{
	struct pcn_softc	*sc;
	int			val;

	sc = device_get_softc(dev);

	/*
	 * At least Am79C971 with DP83840A wedge when isolating the
	 * external PHY so we can't allow multiple external PHYs.
	 * There are cards that use Am79C971 with both the internal
	 * and an external PHY though.
	 * For internal PHYs it doesn't really matter whether we can
	 * isolate the remaining internal and the external ones in
	 * the PHY drivers as the internal PHYs have to be enabled
	 * individually in PCN_BCR_PHYSEL, PCN_CSR_MODE, etc.
	 * With Am79C97{3,5,8} we don't support switching beetween
	 * the internal and external PHYs, yet, so we can't allow
	 * multiple PHYs with these either.
	 * Am79C97{2,6} actually only support external PHYs (not
	 * connectable internal ones respond at the usual addresses,
	 * which don't hurt if we let them show up on the bus) and
	 * isolating them works.
	 */
	if (((sc->pcn_type == Am79C971 && phy != PCN_PHYAD_10BT) ||
	    sc->pcn_type == Am79C973 || sc->pcn_type == Am79C975 ||
	    sc->pcn_type == Am79C978) && sc->pcn_extphyaddr != -1 &&
	    phy != sc->pcn_extphyaddr)
		return(0);

	pcn_bcr_write(sc, PCN_BCR_MIIADDR, reg | (phy << 5));
	val = pcn_bcr_read(sc, PCN_BCR_MIIDATA) & 0xFFFF;
	if (val == 0xFFFF)
		return(0);

	if (((sc->pcn_type == Am79C971 && phy != PCN_PHYAD_10BT) ||
	    sc->pcn_type == Am79C973 || sc->pcn_type == Am79C975 ||
	    sc->pcn_type == Am79C978) && sc->pcn_extphyaddr == -1)
		sc->pcn_extphyaddr = phy;

	return(val);
}

static int
pcn_miibus_writereg(dev, phy, reg, data)
	device_t		dev;
	int			phy, reg, data;
{
	struct pcn_softc	*sc;

	sc = device_get_softc(dev);

	pcn_bcr_write(sc, PCN_BCR_MIIADDR, reg | (phy << 5));
	pcn_bcr_write(sc, PCN_BCR_MIIDATA, data);

	return(0);
}

static void
pcn_miibus_statchg(dev)
	device_t		dev;
{
	struct pcn_softc	*sc;
	struct mii_data		*mii;

	sc = device_get_softc(dev);
	mii = device_get_softc(sc->pcn_miibus);

	if ((mii->mii_media_active & IFM_GMASK) == IFM_FDX) {
		PCN_BCR_SETBIT(sc, PCN_BCR_DUPLEX, PCN_DUPLEX_FDEN);
	} else {
		PCN_BCR_CLRBIT(sc, PCN_BCR_DUPLEX, PCN_DUPLEX_FDEN);
	}

	return;
}

static void
pcn_setmulti(sc)
	struct pcn_softc	*sc;
{
	struct ifnet		*ifp;
	struct ifmultiaddr	*ifma;
	u_int32_t		h, i;
	u_int16_t		hashes[4] = { 0, 0, 0, 0 };

	ifp = sc->pcn_ifp;

	PCN_CSR_SETBIT(sc, PCN_CSR_EXTCTL1, PCN_EXTCTL1_SPND);

	if (ifp->if_flags & IFF_ALLMULTI || ifp->if_flags & IFF_PROMISC) {
		for (i = 0; i < 4; i++)
			pcn_csr_write(sc, PCN_CSR_MAR0 + i, 0xFFFF);
		PCN_CSR_CLRBIT(sc, PCN_CSR_EXTCTL1, PCN_EXTCTL1_SPND);
		return;
	}

	/* first, zot all the existing hash bits */
	for (i = 0; i < 4; i++)
		pcn_csr_write(sc, PCN_CSR_MAR0 + i, 0);

	/* now program new ones */
	if_maddr_rlock(ifp);
	CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		h = ether_crc32_le(LLADDR((struct sockaddr_dl *)
		    ifma->ifma_addr), ETHER_ADDR_LEN) >> 26;
		hashes[h >> 4] |= 1 << (h & 0xF);
	}
	if_maddr_runlock(ifp);

	for (i = 0; i < 4; i++)
		pcn_csr_write(sc, PCN_CSR_MAR0 + i, hashes[i]);

	PCN_CSR_CLRBIT(sc, PCN_CSR_EXTCTL1, PCN_EXTCTL1_SPND);

	return;
}

static void
pcn_reset(sc)
	struct pcn_softc	*sc;
{
	/*
	 * Issue a reset by reading from the RESET register.
	 * Note that we don't know if the chip is operating in
	 * 16-bit or 32-bit mode at this point, so we attempt
	 * to reset the chip both ways. If one fails, the other
	 * will succeed.
	 */
	CSR_READ_2(sc, PCN_IO16_RESET);
	CSR_READ_4(sc, PCN_IO32_RESET);

	/* Wait a little while for the chip to get its brains in order. */
	DELAY(1000);

	/* Select 32-bit (DWIO) mode */
	CSR_WRITE_4(sc, PCN_IO32_RDP, 0);

	/* Select software style 3. */
	pcn_bcr_write(sc, PCN_BCR_SSTYLE, PCN_SWSTYLE_PCNETPCI_BURST);

        return;
}

static const char *
pcn_chipid_name(u_int32_t id)
{
	const struct pcn_chipid *p;

	p = pcn_chipid;
	while (p->name) {
		if (id == p->id)
			return (p->name);
		p++;
	}
	return ("Unknown");
}

static u_int32_t
pcn_chip_id(device_t dev)
{
	struct pcn_softc	*sc;
	u_int32_t		chip_id;

	sc = device_get_softc(dev);
	/*
	 * Note: we can *NOT* put the chip into
	 * 32-bit mode yet. The le(4) driver will only
	 * work in 16-bit mode, and once the chip
	 * goes into 32-bit mode, the only way to
	 * get it out again is with a hardware reset.
	 * So if pcn_probe() is called before the
	 * le(4) driver's probe routine, the chip will
	 * be locked into 32-bit operation and the
	 * le(4) driver will be unable to attach to it.
	 * Note II: if the chip happens to already
	 * be in 32-bit mode, we still need to check
	 * the chip ID, but first we have to detect
	 * 32-bit mode using only 16-bit operations.
	 * The safest way to do this is to read the
	 * PCI subsystem ID from BCR23/24 and compare
	 * that with the value read from PCI config
	 * space.
	 */
	chip_id = pcn_bcr_read16(sc, PCN_BCR_PCISUBSYSID);
	chip_id <<= 16;
	chip_id |= pcn_bcr_read16(sc, PCN_BCR_PCISUBVENID);
	/*
	 * Note III: the test for 0x10001000 is a hack to
	 * pacify VMware, who's pseudo-PCnet interface is
	 * broken. Reading the subsystem register from PCI
	 * config space yields 0x00000000 while reading the
	 * same value from I/O space yields 0x10001000. It's
	 * not supposed to be that way.
	 */
	if (chip_id == pci_read_config(dev,
	    PCIR_SUBVEND_0, 4) || chip_id == 0x10001000) {
		/* We're in 16-bit mode. */
		chip_id = pcn_csr_read16(sc, PCN_CSR_CHIPID1);
		chip_id <<= 16;
		chip_id |= pcn_csr_read16(sc, PCN_CSR_CHIPID0);
	} else {
		/* We're in 32-bit mode. */
		chip_id = pcn_csr_read(sc, PCN_CSR_CHIPID1);
		chip_id <<= 16;
		chip_id |= pcn_csr_read(sc, PCN_CSR_CHIPID0);
	}

	return (chip_id);
}

static const struct pcn_type *
pcn_match(u_int16_t vid, u_int16_t did)
{
	const struct pcn_type	*t;

	t = pcn_devs;
	while (t->pcn_name != NULL) {
		if ((vid == t->pcn_vid) && (did == t->pcn_did))
			return (t);
		t++;
	}
	return (NULL);
}

/*
 * Probe for an AMD chip. Check the PCI vendor and device
 * IDs against our list and return a device name if we find a match.
 */
static int
pcn_probe(dev)
	device_t		dev;
{
	const struct pcn_type	*t;
	struct pcn_softc	*sc;
	int			rid;
	u_int32_t		chip_id;

	t = pcn_match(pci_get_vendor(dev), pci_get_device(dev));
	if (t == NULL)
		return (ENXIO);
	sc = device_get_softc(dev);

	/*
	 * Temporarily map the I/O space so we can read the chip ID register.
	 */
	rid = PCN_RID;
	sc->pcn_res = bus_alloc_resource_any(dev, PCN_RES, &rid, RF_ACTIVE);
	if (sc->pcn_res == NULL) {
		device_printf(dev, "couldn't map ports/memory\n");
		return(ENXIO);
	}
	sc->pcn_btag = rman_get_bustag(sc->pcn_res);
	sc->pcn_bhandle = rman_get_bushandle(sc->pcn_res);

	chip_id = pcn_chip_id(dev);

	bus_release_resource(dev, PCN_RES, PCN_RID, sc->pcn_res);

	switch((chip_id >> 12) & PART_MASK) {
	case Am79C971:
	case Am79C972:
	case Am79C973:
	case Am79C975:
	case Am79C976:
	case Am79C978:
		break;
	default:
		return(ENXIO);
	}
	device_set_desc(dev, t->pcn_name);
	return(BUS_PROBE_DEFAULT);
}

/*
 * Attach the interface. Allocate softc structures, do ifmedia
 * setup and ethernet/BPF attach.
 */
static int
pcn_attach(dev)
	device_t		dev;
{
	u_int32_t		eaddr[2];
	struct pcn_softc	*sc;
	struct mii_data		*mii;
	struct mii_softc	*miisc;
	struct ifnet		*ifp;
	int			error = 0, rid;

	sc = device_get_softc(dev);

	/* Initialize our mutex. */
	mtx_init(&sc->pcn_mtx, device_get_nameunit(dev), MTX_NETWORK_LOCK,
	    MTX_DEF);
	/*
	 * Map control/status registers.
	 */
	pci_enable_busmaster(dev);

	/* Retrieve the chip ID */
	sc->pcn_type = (pcn_chip_id(dev) >> 12) & PART_MASK;
	device_printf(dev, "Chip ID %04x (%s)\n",
		sc->pcn_type, pcn_chipid_name(sc->pcn_type));

	rid = PCN_RID;
	sc->pcn_res = bus_alloc_resource_any(dev, PCN_RES, &rid, RF_ACTIVE);

	if (sc->pcn_res == NULL) {
		device_printf(dev, "couldn't map ports/memory\n");
		error = ENXIO;
		goto fail;
	}

	sc->pcn_btag = rman_get_bustag(sc->pcn_res);
	sc->pcn_bhandle = rman_get_bushandle(sc->pcn_res);

	/* Allocate interrupt */
	rid = 0;
	sc->pcn_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_SHAREABLE | RF_ACTIVE);

	if (sc->pcn_irq == NULL) {
		device_printf(dev, "couldn't map interrupt\n");
		error = ENXIO;
		goto fail;
	}

	/* Reset the adapter. */
	pcn_reset(sc);

	/*
	 * Get station address from the EEPROM.
	 */
	eaddr[0] = CSR_READ_4(sc, PCN_IO32_APROM00);
	eaddr[1] = CSR_READ_4(sc, PCN_IO32_APROM01);

	callout_init_mtx(&sc->pcn_stat_callout, &sc->pcn_mtx, 0);

	sc->pcn_ldata = contigmalloc(sizeof(struct pcn_list_data), M_DEVBUF,
	    M_NOWAIT, 0, 0xffffffff, PAGE_SIZE, 0);

	if (sc->pcn_ldata == NULL) {
		device_printf(dev, "no memory for list buffers!\n");
		error = ENXIO;
		goto fail;
	}
	bzero(sc->pcn_ldata, sizeof(struct pcn_list_data));

	ifp = sc->pcn_ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(dev, "can not if_alloc()\n");
		error = ENOSPC;
		goto fail;
	}
	ifp->if_softc = sc;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = pcn_ioctl;
	ifp->if_start = pcn_start;
	ifp->if_init = pcn_init;
	ifp->if_snd.ifq_maxlen = PCN_TX_LIST_CNT - 1;

	/*
	 * Do MII setup.
	 * See the comment in pcn_miibus_readreg() for why we can't
	 * universally pass MIIF_NOISOLATE here.
	 */
	sc->pcn_extphyaddr = -1;
	error = mii_attach(dev, &sc->pcn_miibus, ifp, pcn_ifmedia_upd,
	   pcn_ifmedia_sts, BMSR_DEFCAPMASK, MII_PHY_ANY, MII_OFFSET_ANY, 0);
	if (error != 0) {
		device_printf(dev, "attaching PHYs failed\n");
		goto fail;
	}
	/*
	 * Record the media instances of internal PHYs, which map the
	 * built-in interfaces to the MII, so we can set the active
	 * PHY/port based on the currently selected media.
	 */
	sc->pcn_inst_10bt = -1;
	mii = device_get_softc(sc->pcn_miibus);
	LIST_FOREACH(miisc, &mii->mii_phys, mii_list) {
		switch (miisc->mii_phy) {
		case PCN_PHYAD_10BT:
			sc->pcn_inst_10bt = miisc->mii_inst;
			break;
		/*
		 * XXX deal with the Am79C97{3,5} internal 100baseT
		 * and the Am79C978 internal HomePNA PHYs.
		 */
		}
	}

	/*
	 * Call MI attach routine.
	 */
	ether_ifattach(ifp, (u_int8_t *) eaddr);

	/* Hook interrupt last to avoid having to lock softc */
	error = bus_setup_intr(dev, sc->pcn_irq, INTR_TYPE_NET | INTR_MPSAFE,
	    NULL, pcn_intr, sc, &sc->pcn_intrhand);

	if (error) {
		device_printf(dev, "couldn't set up irq\n");
		ether_ifdetach(ifp);
		goto fail;
	}

fail:
	if (error)
		pcn_detach(dev);

	gone_by_fcp101_dev(dev);

	return(error);
}

/*
 * Shutdown hardware and free up resources. This can be called any
 * time after the mutex has been initialized. It is called in both
 * the error case in attach and the normal detach case so it needs
 * to be careful about only freeing resources that have actually been
 * allocated.
 */
static int
pcn_detach(dev)
	device_t		dev;
{
	struct pcn_softc	*sc;
	struct ifnet		*ifp;

	sc = device_get_softc(dev);
	ifp = sc->pcn_ifp;

	KASSERT(mtx_initialized(&sc->pcn_mtx), ("pcn mutex not initialized"));

	/* These should only be active if attach succeeded */
	if (device_is_attached(dev)) {
		PCN_LOCK(sc);
		pcn_reset(sc);
		pcn_stop(sc);
		PCN_UNLOCK(sc);
		callout_drain(&sc->pcn_stat_callout);
		ether_ifdetach(ifp);
	}
	if (sc->pcn_miibus)
		device_delete_child(dev, sc->pcn_miibus);
	bus_generic_detach(dev);

	if (sc->pcn_intrhand)
		bus_teardown_intr(dev, sc->pcn_irq, sc->pcn_intrhand);
	if (sc->pcn_irq)
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->pcn_irq);
	if (sc->pcn_res)
		bus_release_resource(dev, PCN_RES, PCN_RID, sc->pcn_res);

	if (ifp)
		if_free(ifp);

	if (sc->pcn_ldata) {
		contigfree(sc->pcn_ldata, sizeof(struct pcn_list_data),
		    M_DEVBUF);
	}

	mtx_destroy(&sc->pcn_mtx);

	return(0);
}

/*
 * Initialize the transmit descriptors.
 */
static int
pcn_list_tx_init(sc)
	struct pcn_softc	*sc;
{
	struct pcn_list_data	*ld;
	struct pcn_ring_data	*cd;
	int			i;

	cd = &sc->pcn_cdata;
	ld = sc->pcn_ldata;

	for (i = 0; i < PCN_TX_LIST_CNT; i++) {
		cd->pcn_tx_chain[i] = NULL;
		ld->pcn_tx_list[i].pcn_tbaddr = 0;
		ld->pcn_tx_list[i].pcn_txctl = 0;
		ld->pcn_tx_list[i].pcn_txstat = 0;
	}

	cd->pcn_tx_prod = cd->pcn_tx_cons = cd->pcn_tx_cnt = 0;

	return(0);
}


/*
 * Initialize the RX descriptors and allocate mbufs for them.
 */
static int
pcn_list_rx_init(sc)
	struct pcn_softc	*sc;
{
	struct pcn_ring_data	*cd;
	int			i;

	cd = &sc->pcn_cdata;

	for (i = 0; i < PCN_RX_LIST_CNT; i++) {
		if (pcn_newbuf(sc, i, NULL) == ENOBUFS)
			return(ENOBUFS);
	}

	cd->pcn_rx_prod = 0;

	return(0);
}

/*
 * Initialize an RX descriptor and attach an MBUF cluster.
 */
static int
pcn_newbuf(sc, idx, m)
	struct pcn_softc	*sc;
	int			idx;
	struct mbuf		*m;
{
	struct mbuf		*m_new = NULL;
	struct pcn_rx_desc	*c;

	c = &sc->pcn_ldata->pcn_rx_list[idx];

	if (m == NULL) {
		MGETHDR(m_new, M_NOWAIT, MT_DATA);
		if (m_new == NULL)
			return(ENOBUFS);

		if (!(MCLGET(m_new, M_NOWAIT))) {
			m_freem(m_new);
			return(ENOBUFS);
		}
		m_new->m_len = m_new->m_pkthdr.len = MCLBYTES;
	} else {
		m_new = m;
		m_new->m_len = m_new->m_pkthdr.len = MCLBYTES;
		m_new->m_data = m_new->m_ext.ext_buf;
	}

	m_adj(m_new, ETHER_ALIGN);

	sc->pcn_cdata.pcn_rx_chain[idx] = m_new;
	c->pcn_rbaddr = vtophys(mtod(m_new, caddr_t));
	c->pcn_bufsz = (~(PCN_RXLEN) + 1) & PCN_RXLEN_BUFSZ;
	c->pcn_bufsz |= PCN_RXLEN_MBO;
	c->pcn_rxstat = PCN_RXSTAT_STP|PCN_RXSTAT_ENP|PCN_RXSTAT_OWN;

	return(0);
}

/*
 * A frame has been uploaded: pass the resulting mbuf chain up to
 * the higher level protocols.
 */
static void
pcn_rxeof(sc)
	struct pcn_softc	*sc;
{
        struct mbuf		*m;
        struct ifnet		*ifp;
	struct pcn_rx_desc	*cur_rx;
	int			i;

	PCN_LOCK_ASSERT(sc);

	ifp = sc->pcn_ifp;
	i = sc->pcn_cdata.pcn_rx_prod;

	while(PCN_OWN_RXDESC(&sc->pcn_ldata->pcn_rx_list[i])) {
		cur_rx = &sc->pcn_ldata->pcn_rx_list[i];
		m = sc->pcn_cdata.pcn_rx_chain[i];
		sc->pcn_cdata.pcn_rx_chain[i] = NULL;

		/*
		 * If an error occurs, update stats, clear the
		 * status word and leave the mbuf cluster in place:
		 * it should simply get re-used next time this descriptor
	 	 * comes up in the ring.
		 */
		if (cur_rx->pcn_rxstat & PCN_RXSTAT_ERR) {
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			pcn_newbuf(sc, i, m);
			PCN_INC(i, PCN_RX_LIST_CNT);
			continue;
		}

		if (pcn_newbuf(sc, i, NULL)) {
			/* Ran out of mbufs; recycle this one. */
			pcn_newbuf(sc, i, m);
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			PCN_INC(i, PCN_RX_LIST_CNT);
			continue;
		}

		PCN_INC(i, PCN_RX_LIST_CNT);

		/* No errors; receive the packet. */
		if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
		m->m_len = m->m_pkthdr.len =
		    cur_rx->pcn_rxlen - ETHER_CRC_LEN;
		m->m_pkthdr.rcvif = ifp;

		PCN_UNLOCK(sc);
		(*ifp->if_input)(ifp, m);
		PCN_LOCK(sc);
	}

	sc->pcn_cdata.pcn_rx_prod = i;

	return;
}

/*
 * A frame was downloaded to the chip. It's safe for us to clean up
 * the list buffers.
 */

static void
pcn_txeof(sc)
	struct pcn_softc	*sc;
{
	struct pcn_tx_desc	*cur_tx = NULL;
	struct ifnet		*ifp;
	u_int32_t		idx;

	ifp = sc->pcn_ifp;

	/*
	 * Go through our tx list and free mbufs for those
	 * frames that have been transmitted.
	 */
	idx = sc->pcn_cdata.pcn_tx_cons;
	while (idx != sc->pcn_cdata.pcn_tx_prod) {
		cur_tx = &sc->pcn_ldata->pcn_tx_list[idx];

		if (!PCN_OWN_TXDESC(cur_tx))
			break;

		if (!(cur_tx->pcn_txctl & PCN_TXCTL_ENP)) {
			sc->pcn_cdata.pcn_tx_cnt--;
			PCN_INC(idx, PCN_TX_LIST_CNT);
			continue;
		}

		if (cur_tx->pcn_txctl & PCN_TXCTL_ERR) {
			if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
			if (cur_tx->pcn_txstat & PCN_TXSTAT_EXDEF)
				if_inc_counter(ifp, IFCOUNTER_COLLISIONS, 1);
			if (cur_tx->pcn_txstat & PCN_TXSTAT_RTRY)
				if_inc_counter(ifp, IFCOUNTER_COLLISIONS, 1);
		}

		if_inc_counter(ifp, IFCOUNTER_COLLISIONS,
		    cur_tx->pcn_txstat & PCN_TXSTAT_TRC);

		if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
		if (sc->pcn_cdata.pcn_tx_chain[idx] != NULL) {
			m_freem(sc->pcn_cdata.pcn_tx_chain[idx]);
			sc->pcn_cdata.pcn_tx_chain[idx] = NULL;
		}

		sc->pcn_cdata.pcn_tx_cnt--;
		PCN_INC(idx, PCN_TX_LIST_CNT);
	}

	if (idx != sc->pcn_cdata.pcn_tx_cons) {
		/* Some buffers have been freed. */
		sc->pcn_cdata.pcn_tx_cons = idx;
		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
	}
	sc->pcn_timer = (sc->pcn_cdata.pcn_tx_cnt == 0) ? 0 : 5;

	return;
}

static void
pcn_tick(xsc)
	void			*xsc;
{
	struct pcn_softc	*sc;
	struct mii_data		*mii;
	struct ifnet		*ifp;

	sc = xsc;
	ifp = sc->pcn_ifp;
	PCN_LOCK_ASSERT(sc);

	mii = device_get_softc(sc->pcn_miibus);
	mii_tick(mii);

	/* link just died */
	if (sc->pcn_link && !(mii->mii_media_status & IFM_ACTIVE))
		sc->pcn_link = 0;

	/* link just came up, restart */
	if (!sc->pcn_link && mii->mii_media_status & IFM_ACTIVE &&
	    IFM_SUBTYPE(mii->mii_media_active) != IFM_NONE) {
		sc->pcn_link++;
		if (ifp->if_snd.ifq_head != NULL)
			pcn_start_locked(ifp);
	}

	if (sc->pcn_timer > 0 && --sc->pcn_timer == 0)
		pcn_watchdog(sc);
	callout_reset(&sc->pcn_stat_callout, hz, pcn_tick, sc);

	return;
}

static void
pcn_intr(arg)
	void			*arg;
{
	struct pcn_softc	*sc;
	struct ifnet		*ifp;
	u_int32_t		status;

	sc = arg;
	ifp = sc->pcn_ifp;

	PCN_LOCK(sc);

	/* Suppress unwanted interrupts */
	if (!(ifp->if_flags & IFF_UP)) {
		pcn_stop(sc);
		PCN_UNLOCK(sc);
		return;
	}

	CSR_WRITE_4(sc, PCN_IO32_RAP, PCN_CSR_CSR);

	while ((status = CSR_READ_4(sc, PCN_IO32_RDP)) & PCN_CSR_INTR) {
		CSR_WRITE_4(sc, PCN_IO32_RDP, status);

		if (status & PCN_CSR_RINT)
			pcn_rxeof(sc);

		if (status & PCN_CSR_TINT)
			pcn_txeof(sc);

		if (status & PCN_CSR_ERR) {
			pcn_init_locked(sc);
			break;
		}
	}

	if (ifp->if_snd.ifq_head != NULL)
		pcn_start_locked(ifp);

	PCN_UNLOCK(sc);
	return;
}

/*
 * Encapsulate an mbuf chain in a descriptor by coupling the mbuf data
 * pointers to the fragment pointers.
 */
static int
pcn_encap(sc, m_head, txidx)
	struct pcn_softc	*sc;
	struct mbuf		*m_head;
	u_int32_t		*txidx;
{
	struct pcn_tx_desc	*f = NULL;
	struct mbuf		*m;
	int			frag, cur, cnt = 0;

	/*
 	 * Start packing the mbufs in this chain into
	 * the fragment pointers. Stop when we run out
 	 * of fragments or hit the end of the mbuf chain.
	 */
	m = m_head;
	cur = frag = *txidx;

	for (m = m_head; m != NULL; m = m->m_next) {
		if (m->m_len == 0)
			continue;

		if ((PCN_TX_LIST_CNT - (sc->pcn_cdata.pcn_tx_cnt + cnt)) < 2)
			return(ENOBUFS);
		f = &sc->pcn_ldata->pcn_tx_list[frag];
		f->pcn_txctl = (~(m->m_len) + 1) & PCN_TXCTL_BUFSZ;
		f->pcn_txctl |= PCN_TXCTL_MBO;
		f->pcn_tbaddr = vtophys(mtod(m, vm_offset_t));
		if (cnt == 0)
			f->pcn_txctl |= PCN_TXCTL_STP;
		else
			f->pcn_txctl |= PCN_TXCTL_OWN;
		cur = frag;
		PCN_INC(frag, PCN_TX_LIST_CNT);
		cnt++;
	}

	if (m != NULL)
		return(ENOBUFS);

	sc->pcn_cdata.pcn_tx_chain[cur] = m_head;
	sc->pcn_ldata->pcn_tx_list[cur].pcn_txctl |=
	    PCN_TXCTL_ENP|PCN_TXCTL_ADD_FCS|PCN_TXCTL_MORE_LTINT;
	sc->pcn_ldata->pcn_tx_list[*txidx].pcn_txctl |= PCN_TXCTL_OWN;
	sc->pcn_cdata.pcn_tx_cnt += cnt;
	*txidx = frag;

	return(0);
}

/*
 * Main transmit routine. To avoid having to do mbuf copies, we put pointers
 * to the mbuf data regions directly in the transmit lists. We also save a
 * copy of the pointers since the transmit list fragment pointers are
 * physical addresses.
 */
static void
pcn_start(ifp)
	struct ifnet		*ifp;
{
	struct pcn_softc	*sc;

	sc = ifp->if_softc;
	PCN_LOCK(sc);
	pcn_start_locked(ifp);
	PCN_UNLOCK(sc);
}

static void
pcn_start_locked(ifp)
	struct ifnet		*ifp;
{
	struct pcn_softc	*sc;
	struct mbuf		*m_head = NULL;
	u_int32_t		idx;

	sc = ifp->if_softc;

	PCN_LOCK_ASSERT(sc);

	if (!sc->pcn_link)
		return;

	idx = sc->pcn_cdata.pcn_tx_prod;

	if (ifp->if_drv_flags & IFF_DRV_OACTIVE)
		return;

	while(sc->pcn_cdata.pcn_tx_chain[idx] == NULL) {
		IF_DEQUEUE(&ifp->if_snd, m_head);
		if (m_head == NULL)
			break;

		if (pcn_encap(sc, m_head, &idx)) {
			IF_PREPEND(&ifp->if_snd, m_head);
			ifp->if_drv_flags |= IFF_DRV_OACTIVE;
			break;
		}

		/*
		 * If there's a BPF listener, bounce a copy of this frame
		 * to him.
		 */
		BPF_MTAP(ifp, m_head);

	}

	/* Transmit */
	sc->pcn_cdata.pcn_tx_prod = idx;
	pcn_csr_write(sc, PCN_CSR_CSR, PCN_CSR_TX|PCN_CSR_INTEN);

	/*
	 * Set a timeout in case the chip goes out to lunch.
	 */
	sc->pcn_timer = 5;

	return;
}

static void
pcn_setfilt(ifp)
	struct ifnet		*ifp;
{
	struct pcn_softc	*sc;

	sc = ifp->if_softc;

	 /* If we want promiscuous mode, set the allframes bit. */
	if (ifp->if_flags & IFF_PROMISC) {
		PCN_CSR_SETBIT(sc, PCN_CSR_MODE, PCN_MODE_PROMISC);
	} else {
		PCN_CSR_CLRBIT(sc, PCN_CSR_MODE, PCN_MODE_PROMISC);
	}

	/* Set the capture broadcast bit to capture broadcast frames. */
	if (ifp->if_flags & IFF_BROADCAST) {
		PCN_CSR_CLRBIT(sc, PCN_CSR_MODE, PCN_MODE_RXNOBROAD);
	} else {
		PCN_CSR_SETBIT(sc, PCN_CSR_MODE, PCN_MODE_RXNOBROAD);
	}

	return;
}

static void
pcn_init(xsc)
	void			*xsc;
{
	struct pcn_softc	*sc = xsc;

	PCN_LOCK(sc);
	pcn_init_locked(sc);
	PCN_UNLOCK(sc);
}

static void
pcn_init_locked(sc)
	struct pcn_softc	*sc;
{
	struct ifnet		*ifp = sc->pcn_ifp;
	struct mii_data		*mii = NULL;
	struct ifmedia_entry	*ife;

	PCN_LOCK_ASSERT(sc);

	/*
	 * Cancel pending I/O and free all RX/TX buffers.
	 */
	pcn_stop(sc);
	pcn_reset(sc);

	mii = device_get_softc(sc->pcn_miibus);
	ife = mii->mii_media.ifm_cur;

	/* Set MAC address */
	pcn_csr_write(sc, PCN_CSR_PAR0,
	    ((u_int16_t *)IF_LLADDR(sc->pcn_ifp))[0]);
	pcn_csr_write(sc, PCN_CSR_PAR1,
	    ((u_int16_t *)IF_LLADDR(sc->pcn_ifp))[1]);
	pcn_csr_write(sc, PCN_CSR_PAR2,
	    ((u_int16_t *)IF_LLADDR(sc->pcn_ifp))[2]);

	/* Init circular RX list. */
	if (pcn_list_rx_init(sc) == ENOBUFS) {
		if_printf(ifp, "initialization failed: no "
		    "memory for rx buffers\n");
		pcn_stop(sc);
		return;
	}

	/*
	 * Init tx descriptors.
	 */
	pcn_list_tx_init(sc);

	/* Clear PCN_MISC_ASEL so we can set the port via PCN_CSR_MODE. */
	PCN_BCR_CLRBIT(sc, PCN_BCR_MISCCFG, PCN_MISC_ASEL);

	/*
	 * Set up the port based on the currently selected media.
	 * For Am79C978 we've to unconditionally set PCN_PORT_MII and
	 * set the PHY in PCN_BCR_PHYSEL instead.
	 */
	if (sc->pcn_type != Am79C978 &&
	    IFM_INST(ife->ifm_media) == sc->pcn_inst_10bt)
		pcn_csr_write(sc, PCN_CSR_MODE, PCN_PORT_10BASET);
	else
		pcn_csr_write(sc, PCN_CSR_MODE, PCN_PORT_MII);

	/* Set up RX filter. */
	pcn_setfilt(ifp);

	/*
	 * Load the multicast filter.
	 */
	pcn_setmulti(sc);

	/*
	 * Load the addresses of the RX and TX lists.
	 */
	pcn_csr_write(sc, PCN_CSR_RXADDR0,
	    vtophys(&sc->pcn_ldata->pcn_rx_list[0]) & 0xFFFF);
	pcn_csr_write(sc, PCN_CSR_RXADDR1,
	    (vtophys(&sc->pcn_ldata->pcn_rx_list[0]) >> 16) & 0xFFFF);
	pcn_csr_write(sc, PCN_CSR_TXADDR0,
	    vtophys(&sc->pcn_ldata->pcn_tx_list[0]) & 0xFFFF);
	pcn_csr_write(sc, PCN_CSR_TXADDR1,
	    (vtophys(&sc->pcn_ldata->pcn_tx_list[0]) >> 16) & 0xFFFF);

	/* Set the RX and TX ring sizes. */
	pcn_csr_write(sc, PCN_CSR_RXRINGLEN, (~PCN_RX_LIST_CNT) + 1);
	pcn_csr_write(sc, PCN_CSR_TXRINGLEN, (~PCN_TX_LIST_CNT) + 1);

	/* We're not using the initialization block. */
	pcn_csr_write(sc, PCN_CSR_IAB1, 0);

	/* Enable fast suspend mode. */
	PCN_CSR_SETBIT(sc, PCN_CSR_EXTCTL2, PCN_EXTCTL2_FASTSPNDE);

	/*
	 * Enable burst read and write. Also set the no underflow
	 * bit. This will avoid transmit underruns in certain
	 * conditions while still providing decent performance.
	 */
	PCN_BCR_SETBIT(sc, PCN_BCR_BUSCTL, PCN_BUSCTL_NOUFLOW|
	    PCN_BUSCTL_BREAD|PCN_BUSCTL_BWRITE);

	/* Enable graceful recovery from underflow. */
	PCN_CSR_SETBIT(sc, PCN_CSR_IMR, PCN_IMR_DXSUFLO);

	/* Enable auto-padding of short TX frames. */
	PCN_CSR_SETBIT(sc, PCN_CSR_TFEAT, PCN_TFEAT_PAD_TX);

	/* Disable MII autoneg (we handle this ourselves). */
	PCN_BCR_SETBIT(sc, PCN_BCR_MIICTL, PCN_MIICTL_DANAS);

	if (sc->pcn_type == Am79C978)
		/* XXX support other PHYs? */
		pcn_bcr_write(sc, PCN_BCR_PHYSEL,
		    PCN_PHYSEL_PCNET|PCN_PHY_HOMEPNA);

	/* Enable interrupts and start the controller running. */
	pcn_csr_write(sc, PCN_CSR_CSR, PCN_CSR_INTEN|PCN_CSR_START);

	mii_mediachg(mii);

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

	callout_reset(&sc->pcn_stat_callout, hz, pcn_tick, sc);

	return;
}

/*
 * Set media options.
 */
static int
pcn_ifmedia_upd(ifp)
	struct ifnet		*ifp;
{
	struct pcn_softc	*sc;

	sc = ifp->if_softc;

	PCN_LOCK(sc);

	/*
	 * At least Am79C971 with DP83840A can wedge when switching
	 * from the internal 10baseT PHY to the external PHY without
	 * issuing pcn_reset(). For setting the port in PCN_CSR_MODE
	 * the PCnet chip has to be powered down or stopped anyway
	 * and although documented otherwise it doesn't take effect
	 * until the next initialization.
	 */
	sc->pcn_link = 0;
	pcn_stop(sc);
	pcn_reset(sc);
	pcn_init_locked(sc);
	if (ifp->if_snd.ifq_head != NULL)
		pcn_start_locked(ifp);

	PCN_UNLOCK(sc);

	return(0);
}

/*
 * Report current media status.
 */
static void
pcn_ifmedia_sts(ifp, ifmr)
	struct ifnet		*ifp;
	struct ifmediareq	*ifmr;
{
	struct pcn_softc	*sc;
	struct mii_data		*mii;

	sc = ifp->if_softc;

	mii = device_get_softc(sc->pcn_miibus);
	PCN_LOCK(sc);
	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
	PCN_UNLOCK(sc);

	return;
}

static int
pcn_ioctl(ifp, command, data)
	struct ifnet		*ifp;
	u_long			command;
	caddr_t			data;
{
	struct pcn_softc	*sc = ifp->if_softc;
	struct ifreq		*ifr = (struct ifreq *) data;
	struct mii_data		*mii = NULL;
	int			error = 0;

	switch(command) {
	case SIOCSIFFLAGS:
		PCN_LOCK(sc);
		if (ifp->if_flags & IFF_UP) {
                        if (ifp->if_drv_flags & IFF_DRV_RUNNING &&
			    ifp->if_flags & IFF_PROMISC &&
			    !(sc->pcn_if_flags & IFF_PROMISC)) {
				PCN_CSR_SETBIT(sc, PCN_CSR_EXTCTL1,
				    PCN_EXTCTL1_SPND);
				pcn_setfilt(ifp);
				PCN_CSR_CLRBIT(sc, PCN_CSR_EXTCTL1,
				    PCN_EXTCTL1_SPND);
				pcn_csr_write(sc, PCN_CSR_CSR,
				    PCN_CSR_INTEN|PCN_CSR_START);
			} else if (ifp->if_drv_flags & IFF_DRV_RUNNING &&
			    !(ifp->if_flags & IFF_PROMISC) &&
				sc->pcn_if_flags & IFF_PROMISC) {
				PCN_CSR_SETBIT(sc, PCN_CSR_EXTCTL1,
				    PCN_EXTCTL1_SPND);
				pcn_setfilt(ifp);
				PCN_CSR_CLRBIT(sc, PCN_CSR_EXTCTL1,
				    PCN_EXTCTL1_SPND);
				pcn_csr_write(sc, PCN_CSR_CSR,
				    PCN_CSR_INTEN|PCN_CSR_START);
			} else if (!(ifp->if_drv_flags & IFF_DRV_RUNNING))
				pcn_init_locked(sc);
		} else {
			if (ifp->if_drv_flags & IFF_DRV_RUNNING)
				pcn_stop(sc);
		}
		sc->pcn_if_flags = ifp->if_flags;
		PCN_UNLOCK(sc);
		error = 0;
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		PCN_LOCK(sc);
		pcn_setmulti(sc);
		PCN_UNLOCK(sc);
		error = 0;
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		mii = device_get_softc(sc->pcn_miibus);
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, command);
		break;
	default:
		error = ether_ioctl(ifp, command, data);
		break;
	}

	return(error);
}

static void
pcn_watchdog(struct pcn_softc *sc)
{
	struct ifnet		*ifp;

	PCN_LOCK_ASSERT(sc);
	ifp = sc->pcn_ifp;

	if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
	if_printf(ifp, "watchdog timeout\n");

	pcn_stop(sc);
	pcn_reset(sc);
	pcn_init_locked(sc);

	if (ifp->if_snd.ifq_head != NULL)
		pcn_start_locked(ifp);
}

/*
 * Stop the adapter and free any mbufs allocated to the
 * RX and TX lists.
 */
static void
pcn_stop(struct pcn_softc *sc)
{
	int			i;
	struct ifnet		*ifp;

	PCN_LOCK_ASSERT(sc);
	ifp = sc->pcn_ifp;
	sc->pcn_timer = 0;

	callout_stop(&sc->pcn_stat_callout);

	/* Turn off interrupts */
	PCN_CSR_CLRBIT(sc, PCN_CSR_CSR, PCN_CSR_INTEN);
	/* Stop adapter */
	PCN_CSR_SETBIT(sc, PCN_CSR_CSR, PCN_CSR_STOP);
	sc->pcn_link = 0;

	/*
	 * Free data in the RX lists.
	 */
	for (i = 0; i < PCN_RX_LIST_CNT; i++) {
		if (sc->pcn_cdata.pcn_rx_chain[i] != NULL) {
			m_freem(sc->pcn_cdata.pcn_rx_chain[i]);
			sc->pcn_cdata.pcn_rx_chain[i] = NULL;
		}
	}
	bzero((char *)&sc->pcn_ldata->pcn_rx_list,
		sizeof(sc->pcn_ldata->pcn_rx_list));

	/*
	 * Free the TX list buffers.
	 */
	for (i = 0; i < PCN_TX_LIST_CNT; i++) {
		if (sc->pcn_cdata.pcn_tx_chain[i] != NULL) {
			m_freem(sc->pcn_cdata.pcn_tx_chain[i]);
			sc->pcn_cdata.pcn_tx_chain[i] = NULL;
		}
	}

	bzero((char *)&sc->pcn_ldata->pcn_tx_list,
		sizeof(sc->pcn_ldata->pcn_tx_list));

	ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);

	return;
}

/*
 * Stop all chip I/O so that the kernel's probe routines don't
 * get confused by errant DMAs when rebooting.
 */
static int
pcn_shutdown(device_t dev)
{
	struct pcn_softc	*sc;

	sc = device_get_softc(dev);

	PCN_LOCK(sc);
	pcn_reset(sc);
	pcn_stop(sc);
	PCN_UNLOCK(sc);

	return 0;
}
