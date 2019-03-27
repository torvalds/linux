/*-
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * RealTek 8129/8139 PCI NIC driver
 *
 * Supports several extremely cheap PCI 10/100 adapters based on
 * the RealTek chipset. Datasheets can be obtained from
 * www.realtek.com.tw.
 *
 * Written by Bill Paul <wpaul@ctr.columbia.edu>
 * Electrical Engineering Department
 * Columbia University, New York City
 */
/*
 * The RealTek 8139 PCI NIC redefines the meaning of 'low end.' This is
 * probably the worst PCI ethernet controller ever made, with the possible
 * exception of the FEAST chip made by SMC. The 8139 supports bus-master
 * DMA, but it has a terrible interface that nullifies any performance
 * gains that bus-master DMA usually offers.
 *
 * For transmission, the chip offers a series of four TX descriptor
 * registers. Each transmit frame must be in a contiguous buffer, aligned
 * on a longword (32-bit) boundary. This means we almost always have to
 * do mbuf copies in order to transmit a frame, except in the unlikely
 * case where a) the packet fits into a single mbuf, and b) the packet
 * is 32-bit aligned within the mbuf's data area. The presence of only
 * four descriptor registers means that we can never have more than four
 * packets queued for transmission at any one time.
 *
 * Reception is not much better. The driver has to allocate a single large
 * buffer area (up to 64K in size) into which the chip will DMA received
 * frames. Because we don't know where within this region received packets
 * will begin or end, we have no choice but to copy data from the buffer
 * area into mbufs in order to pass the packets up to the higher protocol
 * levels.
 *
 * It's impossible given this rotten design to really achieve decent
 * performance at 100Mbps, unless you happen to have a 400Mhz PII or
 * some equally overmuscled CPU to drive it.
 *
 * On the bright side, the 8139 does have a built-in PHY, although
 * rather than using an MDIO serial interface like most other NICs, the
 * PHY registers are directly accessible through the 8139's register
 * space. The 8139 supports autonegotiation, as well as a 64-bit multicast
 * filter.
 *
 * The 8129 chip is an older version of the 8139 that uses an external PHY
 * chip. The 8129 has a serial MDIO interface for accessing the MII where
 * the 8139 lets you directly access the on-board PHY registers. We need
 * to select which interface to use depending on the chip type.
 */

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_device_polling.h"
#endif

#include <sys/param.h>
#include <sys/endian.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>

#include <net/bpf.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <dev/mii/mii.h>
#include <dev/mii/mii_bitbang.h>
#include <dev/mii/miivar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

MODULE_DEPEND(rl, pci, 1, 1, 1);
MODULE_DEPEND(rl, ether, 1, 1, 1);
MODULE_DEPEND(rl, miibus, 1, 1, 1);

/* "device miibus" required.  See GENERIC if you get errors here. */
#include "miibus_if.h"

#include <dev/rl/if_rlreg.h>

/*
 * Various supported device vendors/types and their names.
 */
static const struct rl_type rl_devs[] = {
	{ RT_VENDORID, RT_DEVICEID_8129, RL_8129,
		"RealTek 8129 10/100BaseTX" },
	{ RT_VENDORID, RT_DEVICEID_8139, RL_8139,
		"RealTek 8139 10/100BaseTX" },
	{ RT_VENDORID, RT_DEVICEID_8139D, RL_8139,
		"RealTek 8139 10/100BaseTX" },
	{ RT_VENDORID, RT_DEVICEID_8138, RL_8139,
		"RealTek 8139 10/100BaseTX CardBus" },
	{ RT_VENDORID, RT_DEVICEID_8100, RL_8139,
		"RealTek 8100 10/100BaseTX" },
	{ ACCTON_VENDORID, ACCTON_DEVICEID_5030, RL_8139,
		"Accton MPX 5030/5038 10/100BaseTX" },
	{ DELTA_VENDORID, DELTA_DEVICEID_8139, RL_8139,
		"Delta Electronics 8139 10/100BaseTX" },
	{ ADDTRON_VENDORID, ADDTRON_DEVICEID_8139, RL_8139,
		"Addtron Technology 8139 10/100BaseTX" },
	{ DLINK_VENDORID, DLINK_DEVICEID_520TX_REVC1, RL_8139,
		"D-Link DFE-520TX (rev. C1) 10/100BaseTX" },
	{ DLINK_VENDORID, DLINK_DEVICEID_530TXPLUS, RL_8139,
		"D-Link DFE-530TX+ 10/100BaseTX" },
	{ DLINK_VENDORID, DLINK_DEVICEID_690TXD, RL_8139,
		"D-Link DFE-690TXD 10/100BaseTX" },
	{ NORTEL_VENDORID, ACCTON_DEVICEID_5030, RL_8139,
		"Nortel Networks 10/100BaseTX" },
	{ COREGA_VENDORID, COREGA_DEVICEID_FETHERCBTXD, RL_8139,
		"Corega FEther CB-TXD" },
	{ COREGA_VENDORID, COREGA_DEVICEID_FETHERIICBTXD, RL_8139,
		"Corega FEtherII CB-TXD" },
	{ PEPPERCON_VENDORID, PEPPERCON_DEVICEID_ROLF, RL_8139,
		"Peppercon AG ROL-F" },
	{ PLANEX_VENDORID, PLANEX_DEVICEID_FNW3603TX, RL_8139,
		"Planex FNW-3603-TX" },
	{ PLANEX_VENDORID, PLANEX_DEVICEID_FNW3800TX, RL_8139,
		"Planex FNW-3800-TX" },
	{ CP_VENDORID, RT_DEVICEID_8139, RL_8139,
		"Compaq HNE-300" },
	{ LEVEL1_VENDORID, LEVEL1_DEVICEID_FPC0106TX, RL_8139,
		"LevelOne FPC-0106TX" },
	{ EDIMAX_VENDORID, EDIMAX_DEVICEID_EP4103DL, RL_8139,
		"Edimax EP-4103DL CardBus" }
};

static int rl_attach(device_t);
static int rl_detach(device_t);
static void rl_dmamap_cb(void *, bus_dma_segment_t *, int, int);
static int rl_dma_alloc(struct rl_softc *);
static void rl_dma_free(struct rl_softc *);
static void rl_eeprom_putbyte(struct rl_softc *, int);
static void rl_eeprom_getword(struct rl_softc *, int, uint16_t *);
static int rl_encap(struct rl_softc *, struct mbuf **);
static int rl_list_tx_init(struct rl_softc *);
static int rl_list_rx_init(struct rl_softc *);
static int rl_ifmedia_upd(struct ifnet *);
static void rl_ifmedia_sts(struct ifnet *, struct ifmediareq *);
static int rl_ioctl(struct ifnet *, u_long, caddr_t);
static void rl_intr(void *);
static void rl_init(void *);
static void rl_init_locked(struct rl_softc *sc);
static int rl_miibus_readreg(device_t, int, int);
static void rl_miibus_statchg(device_t);
static int rl_miibus_writereg(device_t, int, int, int);
#ifdef DEVICE_POLLING
static int rl_poll(struct ifnet *ifp, enum poll_cmd cmd, int count);
static int rl_poll_locked(struct ifnet *ifp, enum poll_cmd cmd, int count);
#endif
static int rl_probe(device_t);
static void rl_read_eeprom(struct rl_softc *, uint8_t *, int, int, int);
static void rl_reset(struct rl_softc *);
static int rl_resume(device_t);
static int rl_rxeof(struct rl_softc *);
static void rl_rxfilter(struct rl_softc *);
static int rl_shutdown(device_t);
static void rl_start(struct ifnet *);
static void rl_start_locked(struct ifnet *);
static void rl_stop(struct rl_softc *);
static int rl_suspend(device_t);
static void rl_tick(void *);
static void rl_txeof(struct rl_softc *);
static void rl_watchdog(struct rl_softc *);
static void rl_setwol(struct rl_softc *);
static void rl_clrwol(struct rl_softc *);

/*
 * MII bit-bang glue
 */
static uint32_t rl_mii_bitbang_read(device_t);
static void rl_mii_bitbang_write(device_t, uint32_t);

static const struct mii_bitbang_ops rl_mii_bitbang_ops = {
	rl_mii_bitbang_read,
	rl_mii_bitbang_write,
	{
		RL_MII_DATAOUT,	/* MII_BIT_MDO */
		RL_MII_DATAIN,	/* MII_BIT_MDI */
		RL_MII_CLK,	/* MII_BIT_MDC */
		RL_MII_DIR,	/* MII_BIT_DIR_HOST_PHY */
		0,		/* MII_BIT_DIR_PHY_HOST */
	}
};

static device_method_t rl_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		rl_probe),
	DEVMETHOD(device_attach,	rl_attach),
	DEVMETHOD(device_detach,	rl_detach),
	DEVMETHOD(device_suspend,	rl_suspend),
	DEVMETHOD(device_resume,	rl_resume),
	DEVMETHOD(device_shutdown,	rl_shutdown),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	rl_miibus_readreg),
	DEVMETHOD(miibus_writereg,	rl_miibus_writereg),
	DEVMETHOD(miibus_statchg,	rl_miibus_statchg),

	DEVMETHOD_END
};

static driver_t rl_driver = {
	"rl",
	rl_methods,
	sizeof(struct rl_softc)
};

static devclass_t rl_devclass;

DRIVER_MODULE(rl, pci, rl_driver, rl_devclass, 0, 0);
MODULE_PNP_INFO("U16:vendor;U16:device", pci, rl, rl_devs,
    nitems(rl_devs) - 1);
DRIVER_MODULE(rl, cardbus, rl_driver, rl_devclass, 0, 0);
DRIVER_MODULE(miibus, rl, miibus_driver, miibus_devclass, 0, 0);

#define EE_SET(x)					\
	CSR_WRITE_1(sc, RL_EECMD,			\
		CSR_READ_1(sc, RL_EECMD) | x)

#define EE_CLR(x)					\
	CSR_WRITE_1(sc, RL_EECMD,			\
		CSR_READ_1(sc, RL_EECMD) & ~x)

/*
 * Send a read command and address to the EEPROM, check for ACK.
 */
static void
rl_eeprom_putbyte(struct rl_softc *sc, int addr)
{
	int			d, i;

	d = addr | sc->rl_eecmd_read;

	/*
	 * Feed in each bit and strobe the clock.
	 */
	for (i = 0x400; i; i >>= 1) {
		if (d & i) {
			EE_SET(RL_EE_DATAIN);
		} else {
			EE_CLR(RL_EE_DATAIN);
		}
		DELAY(100);
		EE_SET(RL_EE_CLK);
		DELAY(150);
		EE_CLR(RL_EE_CLK);
		DELAY(100);
	}
}

/*
 * Read a word of data stored in the EEPROM at address 'addr.'
 */
static void
rl_eeprom_getword(struct rl_softc *sc, int addr, uint16_t *dest)
{
	int			i;
	uint16_t		word = 0;

	/* Enter EEPROM access mode. */
	CSR_WRITE_1(sc, RL_EECMD, RL_EEMODE_PROGRAM|RL_EE_SEL);

	/*
	 * Send address of word we want to read.
	 */
	rl_eeprom_putbyte(sc, addr);

	CSR_WRITE_1(sc, RL_EECMD, RL_EEMODE_PROGRAM|RL_EE_SEL);

	/*
	 * Start reading bits from EEPROM.
	 */
	for (i = 0x8000; i; i >>= 1) {
		EE_SET(RL_EE_CLK);
		DELAY(100);
		if (CSR_READ_1(sc, RL_EECMD) & RL_EE_DATAOUT)
			word |= i;
		EE_CLR(RL_EE_CLK);
		DELAY(100);
	}

	/* Turn off EEPROM access mode. */
	CSR_WRITE_1(sc, RL_EECMD, RL_EEMODE_OFF);

	*dest = word;
}

/*
 * Read a sequence of words from the EEPROM.
 */
static void
rl_read_eeprom(struct rl_softc *sc, uint8_t *dest, int off, int cnt, int swap)
{
	int			i;
	uint16_t		word = 0, *ptr;

	for (i = 0; i < cnt; i++) {
		rl_eeprom_getword(sc, off + i, &word);
		ptr = (uint16_t *)(dest + (i * 2));
		if (swap)
			*ptr = ntohs(word);
		else
			*ptr = word;
	}
}

/*
 * Read the MII serial port for the MII bit-bang module.
 */
static uint32_t
rl_mii_bitbang_read(device_t dev)
{
	struct rl_softc *sc;
	uint32_t val;

	sc = device_get_softc(dev);

	val = CSR_READ_1(sc, RL_MII);
	CSR_BARRIER(sc, RL_MII, 1,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);

	return (val);
}

/*
 * Write the MII serial port for the MII bit-bang module.
 */
static void
rl_mii_bitbang_write(device_t dev, uint32_t val)
{
	struct rl_softc *sc;

	sc = device_get_softc(dev);

	CSR_WRITE_1(sc, RL_MII, val);
	CSR_BARRIER(sc, RL_MII, 1,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
}

static int
rl_miibus_readreg(device_t dev, int phy, int reg)
{
	struct rl_softc		*sc;
	uint16_t		rl8139_reg;

	sc = device_get_softc(dev);

	if (sc->rl_type == RL_8139) {
		switch (reg) {
		case MII_BMCR:
			rl8139_reg = RL_BMCR;
			break;
		case MII_BMSR:
			rl8139_reg = RL_BMSR;
			break;
		case MII_ANAR:
			rl8139_reg = RL_ANAR;
			break;
		case MII_ANER:
			rl8139_reg = RL_ANER;
			break;
		case MII_ANLPAR:
			rl8139_reg = RL_LPAR;
			break;
		case MII_PHYIDR1:
		case MII_PHYIDR2:
			return (0);
		/*
		 * Allow the rlphy driver to read the media status
		 * register. If we have a link partner which does not
		 * support NWAY, this is the register which will tell
		 * us the results of parallel detection.
		 */
		case RL_MEDIASTAT:
			return (CSR_READ_1(sc, RL_MEDIASTAT));
		default:
			device_printf(sc->rl_dev, "bad phy register\n");
			return (0);
		}
		return (CSR_READ_2(sc, rl8139_reg));
	}

	return (mii_bitbang_readreg(dev, &rl_mii_bitbang_ops, phy, reg));
}

static int
rl_miibus_writereg(device_t dev, int phy, int reg, int data)
{
	struct rl_softc		*sc;
	uint16_t		rl8139_reg;

	sc = device_get_softc(dev);

	if (sc->rl_type == RL_8139) {
		switch (reg) {
		case MII_BMCR:
			rl8139_reg = RL_BMCR;
			break;
		case MII_BMSR:
			rl8139_reg = RL_BMSR;
			break;
		case MII_ANAR:
			rl8139_reg = RL_ANAR;
			break;
		case MII_ANER:
			rl8139_reg = RL_ANER;
			break;
		case MII_ANLPAR:
			rl8139_reg = RL_LPAR;
			break;
		case MII_PHYIDR1:
		case MII_PHYIDR2:
			return (0);
			break;
		default:
			device_printf(sc->rl_dev, "bad phy register\n");
			return (0);
		}
		CSR_WRITE_2(sc, rl8139_reg, data);
		return (0);
	}

	mii_bitbang_writereg(dev, &rl_mii_bitbang_ops, phy, reg, data);

	return (0);
}

static void
rl_miibus_statchg(device_t dev)
{
	struct rl_softc		*sc;
	struct ifnet		*ifp;
	struct mii_data		*mii;

	sc = device_get_softc(dev);
	mii = device_get_softc(sc->rl_miibus);
	ifp = sc->rl_ifp;
	if (mii == NULL || ifp == NULL ||
	    (ifp->if_drv_flags & IFF_DRV_RUNNING) == 0)
		return;

	sc->rl_flags &= ~RL_FLAG_LINK;
	if ((mii->mii_media_status & (IFM_ACTIVE | IFM_AVALID)) ==
	    (IFM_ACTIVE | IFM_AVALID)) {
		switch (IFM_SUBTYPE(mii->mii_media_active)) {
		case IFM_10_T:
		case IFM_100_TX:
			sc->rl_flags |= RL_FLAG_LINK;
			break;
		default:
			break;
		}
	}
	/*
	 * RealTek controllers do not provide any interface to
	 * Tx/Rx MACs for resolved speed, duplex and flow-control
	 * parameters.
	 */
}

/*
 * Program the 64-bit multicast hash filter.
 */
static void
rl_rxfilter(struct rl_softc *sc)
{
	struct ifnet		*ifp = sc->rl_ifp;
	int			h = 0;
	uint32_t		hashes[2] = { 0, 0 };
	struct ifmultiaddr	*ifma;
	uint32_t		rxfilt;

	RL_LOCK_ASSERT(sc);

	rxfilt = CSR_READ_4(sc, RL_RXCFG);
	rxfilt &= ~(RL_RXCFG_RX_ALLPHYS | RL_RXCFG_RX_BROAD |
	    RL_RXCFG_RX_MULTI);
	/* Always accept frames destined for this host. */
	rxfilt |= RL_RXCFG_RX_INDIV;
	/* Set capture broadcast bit to capture broadcast frames. */
	if (ifp->if_flags & IFF_BROADCAST)
		rxfilt |= RL_RXCFG_RX_BROAD;
	if (ifp->if_flags & IFF_ALLMULTI || ifp->if_flags & IFF_PROMISC) {
		rxfilt |= RL_RXCFG_RX_MULTI;
		if (ifp->if_flags & IFF_PROMISC)
			rxfilt |= RL_RXCFG_RX_ALLPHYS;
		hashes[0] = 0xFFFFFFFF;
		hashes[1] = 0xFFFFFFFF;
	} else {
		/* Now program new ones. */
		if_maddr_rlock(ifp);
		CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
			if (ifma->ifma_addr->sa_family != AF_LINK)
				continue;
			h = ether_crc32_be(LLADDR((struct sockaddr_dl *)
			    ifma->ifma_addr), ETHER_ADDR_LEN) >> 26;
			if (h < 32)
				hashes[0] |= (1 << h);
			else
				hashes[1] |= (1 << (h - 32));
		}
		if_maddr_runlock(ifp);
		if (hashes[0] != 0 || hashes[1] != 0)
			rxfilt |= RL_RXCFG_RX_MULTI;
	}

	CSR_WRITE_4(sc, RL_MAR0, hashes[0]);
	CSR_WRITE_4(sc, RL_MAR4, hashes[1]);
	CSR_WRITE_4(sc, RL_RXCFG, rxfilt);
}

static void
rl_reset(struct rl_softc *sc)
{
	int			i;

	RL_LOCK_ASSERT(sc);

	CSR_WRITE_1(sc, RL_COMMAND, RL_CMD_RESET);

	for (i = 0; i < RL_TIMEOUT; i++) {
		DELAY(10);
		if (!(CSR_READ_1(sc, RL_COMMAND) & RL_CMD_RESET))
			break;
	}
	if (i == RL_TIMEOUT)
		device_printf(sc->rl_dev, "reset never completed!\n");
}

/*
 * Probe for a RealTek 8129/8139 chip. Check the PCI vendor and device
 * IDs against our list and return a device name if we find a match.
 */
static int
rl_probe(device_t dev)
{
	const struct rl_type	*t;
	uint16_t		devid, revid, vendor;
	int			i;
	
	vendor = pci_get_vendor(dev);
	devid = pci_get_device(dev);
	revid = pci_get_revid(dev);

	if (vendor == RT_VENDORID && devid == RT_DEVICEID_8139) {
		if (revid == 0x20) {
			/* 8139C+, let re(4) take care of this device. */
			return (ENXIO);
		}
	}
	t = rl_devs;
	for (i = 0; i < nitems(rl_devs); i++, t++) {
		if (vendor == t->rl_vid && devid == t->rl_did) {
			device_set_desc(dev, t->rl_name);
			return (BUS_PROBE_DEFAULT);
		}
	}

	return (ENXIO);
}

struct rl_dmamap_arg {
	bus_addr_t	rl_busaddr;
};

static void
rl_dmamap_cb(void *arg, bus_dma_segment_t *segs, int nsegs, int error)
{
	struct rl_dmamap_arg	*ctx;

	if (error != 0)
		return;

	KASSERT(nsegs == 1, ("%s: %d segments returned!", __func__, nsegs));

        ctx = (struct rl_dmamap_arg *)arg;
        ctx->rl_busaddr = segs[0].ds_addr;
}

/*
 * Attach the interface. Allocate softc structures, do ifmedia
 * setup and ethernet/BPF attach.
 */
static int
rl_attach(device_t dev)
{
	uint8_t			eaddr[ETHER_ADDR_LEN];
	uint16_t		as[3];
	struct ifnet		*ifp;
	struct rl_softc		*sc;
	const struct rl_type	*t;
	struct sysctl_ctx_list	*ctx;
	struct sysctl_oid_list	*children;
	int			error = 0, hwrev, i, phy, pmc, rid;
	int			prefer_iomap, unit;
	uint16_t		rl_did = 0;
	char			tn[32];

	sc = device_get_softc(dev);
	unit = device_get_unit(dev);
	sc->rl_dev = dev;

	sc->rl_twister_enable = 0;
	snprintf(tn, sizeof(tn), "dev.rl.%d.twister_enable", unit);
	TUNABLE_INT_FETCH(tn, &sc->rl_twister_enable);
	ctx = device_get_sysctl_ctx(sc->rl_dev);
	children = SYSCTL_CHILDREN(device_get_sysctl_tree(sc->rl_dev));
	SYSCTL_ADD_INT(ctx, children, OID_AUTO, "twister_enable", CTLFLAG_RD,
	   &sc->rl_twister_enable, 0, "");

	mtx_init(&sc->rl_mtx, device_get_nameunit(dev), MTX_NETWORK_LOCK,
	    MTX_DEF);
	callout_init_mtx(&sc->rl_stat_callout, &sc->rl_mtx, 0);

	pci_enable_busmaster(dev);


	/*
	 * Map control/status registers.
	 * Default to using PIO access for this driver. On SMP systems,
	 * there appear to be problems with memory mapped mode: it looks
	 * like doing too many memory mapped access back to back in rapid
	 * succession can hang the bus. I'm inclined to blame this on
	 * crummy design/construction on the part of RealTek. Memory
	 * mapped mode does appear to work on uniprocessor systems though.
	 */
	prefer_iomap = 1;
	snprintf(tn, sizeof(tn), "dev.rl.%d.prefer_iomap", unit);
	TUNABLE_INT_FETCH(tn, &prefer_iomap);
	if (prefer_iomap) {
		sc->rl_res_id = PCIR_BAR(0);
		sc->rl_res_type = SYS_RES_IOPORT;
		sc->rl_res = bus_alloc_resource_any(dev, sc->rl_res_type,
		    &sc->rl_res_id, RF_ACTIVE);
	}
	if (prefer_iomap == 0 || sc->rl_res == NULL) {
		sc->rl_res_id = PCIR_BAR(1);
		sc->rl_res_type = SYS_RES_MEMORY;
		sc->rl_res = bus_alloc_resource_any(dev, sc->rl_res_type,
		    &sc->rl_res_id, RF_ACTIVE);
	}
	if (sc->rl_res == NULL) {
		device_printf(dev, "couldn't map ports/memory\n");
		error = ENXIO;
		goto fail;
	}

#ifdef notdef
	/*
	 * Detect the Realtek 8139B. For some reason, this chip is very
	 * unstable when left to autoselect the media
	 * The best workaround is to set the device to the required
	 * media type or to set it to the 10 Meg speed.
	 */
	if ((rman_get_end(sc->rl_res) - rman_get_start(sc->rl_res)) == 0xFF)
		device_printf(dev,
"Realtek 8139B detected. Warning, this may be unstable in autoselect mode\n");
#endif

	sc->rl_btag = rman_get_bustag(sc->rl_res);
	sc->rl_bhandle = rman_get_bushandle(sc->rl_res);

	/* Allocate interrupt */
	rid = 0;
	sc->rl_irq[0] = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_SHAREABLE | RF_ACTIVE);

	if (sc->rl_irq[0] == NULL) {
		device_printf(dev, "couldn't map interrupt\n");
		error = ENXIO;
		goto fail;
	}

	sc->rl_cfg0 = RL_8139_CFG0;
	sc->rl_cfg1 = RL_8139_CFG1;
	sc->rl_cfg2 = 0;
	sc->rl_cfg3 = RL_8139_CFG3;
	sc->rl_cfg4 = RL_8139_CFG4;
	sc->rl_cfg5 = RL_8139_CFG5;

	/*
	 * Reset the adapter. Only take the lock here as it's needed in
	 * order to call rl_reset().
	 */
	RL_LOCK(sc);
	rl_reset(sc);
	RL_UNLOCK(sc);

	sc->rl_eecmd_read = RL_EECMD_READ_6BIT;
	rl_read_eeprom(sc, (uint8_t *)&rl_did, 0, 1, 0);
	if (rl_did != 0x8129)
		sc->rl_eecmd_read = RL_EECMD_READ_8BIT;

	/*
	 * Get station address from the EEPROM.
	 */
	rl_read_eeprom(sc, (uint8_t *)as, RL_EE_EADDR, 3, 0);
	for (i = 0; i < 3; i++) {
		eaddr[(i * 2) + 0] = as[i] & 0xff;
		eaddr[(i * 2) + 1] = as[i] >> 8;
	}

	/*
	 * Now read the exact device type from the EEPROM to find
	 * out if it's an 8129 or 8139.
	 */
	rl_read_eeprom(sc, (uint8_t *)&rl_did, RL_EE_PCI_DID, 1, 0);

	t = rl_devs;
	sc->rl_type = 0;
	while(t->rl_name != NULL) {
		if (rl_did == t->rl_did) {
			sc->rl_type = t->rl_basetype;
			break;
		}
		t++;
	}

	if (sc->rl_type == 0) {
		device_printf(dev, "unknown device ID: %x assuming 8139\n",
		    rl_did);
		sc->rl_type = RL_8139;
		/*
		 * Read RL_IDR register to get ethernet address as accessing
		 * EEPROM may not extract correct address.
		 */
		for (i = 0; i < ETHER_ADDR_LEN; i++)
			eaddr[i] = CSR_READ_1(sc, RL_IDR0 + i);
	}

	if ((error = rl_dma_alloc(sc)) != 0)
		goto fail;

	ifp = sc->rl_ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(dev, "can not if_alloc()\n");
		error = ENOSPC;
		goto fail;
	}

#define	RL_PHYAD_INTERNAL	0

	/* Do MII setup */
	phy = MII_PHY_ANY;
	if (sc->rl_type == RL_8139)
		phy = RL_PHYAD_INTERNAL;
	error = mii_attach(dev, &sc->rl_miibus, ifp, rl_ifmedia_upd,
	    rl_ifmedia_sts, BMSR_DEFCAPMASK, phy, MII_OFFSET_ANY, 0);
	if (error != 0) {
		device_printf(dev, "attaching PHYs failed\n");
		goto fail;
	}

	ifp->if_softc = sc;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_mtu = ETHERMTU;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = rl_ioctl;
	ifp->if_start = rl_start;
	ifp->if_init = rl_init;
	ifp->if_capabilities = IFCAP_VLAN_MTU;
	/* Check WOL for RTL8139B or newer controllers. */
	if (sc->rl_type == RL_8139 &&
	    pci_find_cap(sc->rl_dev, PCIY_PMG, &pmc) == 0) {
		hwrev = CSR_READ_4(sc, RL_TXCFG) & RL_TXCFG_HWREV;
		switch (hwrev) {
		case RL_HWREV_8139B:
		case RL_HWREV_8130:
		case RL_HWREV_8139C:
		case RL_HWREV_8139D:
		case RL_HWREV_8101:
		case RL_HWREV_8100:
			ifp->if_capabilities |= IFCAP_WOL;
			/* Disable WOL. */
			rl_clrwol(sc);
			break;
		default:
			break;
		}
	}
	ifp->if_capenable = ifp->if_capabilities;
	ifp->if_capenable &= ~(IFCAP_WOL_UCAST | IFCAP_WOL_MCAST);
#ifdef DEVICE_POLLING
	ifp->if_capabilities |= IFCAP_POLLING;
#endif
	IFQ_SET_MAXLEN(&ifp->if_snd, ifqmaxlen);
	ifp->if_snd.ifq_drv_maxlen = ifqmaxlen;
	IFQ_SET_READY(&ifp->if_snd);

	/*
	 * Call MI attach routine.
	 */
	ether_ifattach(ifp, eaddr);

	/* Hook interrupt last to avoid having to lock softc */
	error = bus_setup_intr(dev, sc->rl_irq[0], INTR_TYPE_NET | INTR_MPSAFE,
	    NULL, rl_intr, sc, &sc->rl_intrhand[0]);
	if (error) {
		device_printf(sc->rl_dev, "couldn't set up irq\n");
		ether_ifdetach(ifp);
	}

fail:
	if (error)
		rl_detach(dev);

	return (error);
}

/*
 * Shutdown hardware and free up resources. This can be called any
 * time after the mutex has been initialized. It is called in both
 * the error case in attach and the normal detach case so it needs
 * to be careful about only freeing resources that have actually been
 * allocated.
 */
static int
rl_detach(device_t dev)
{
	struct rl_softc		*sc;
	struct ifnet		*ifp;

	sc = device_get_softc(dev);
	ifp = sc->rl_ifp;

	KASSERT(mtx_initialized(&sc->rl_mtx), ("rl mutex not initialized"));

#ifdef DEVICE_POLLING
	if (ifp->if_capenable & IFCAP_POLLING)
		ether_poll_deregister(ifp);
#endif
	/* These should only be active if attach succeeded */
	if (device_is_attached(dev)) {
		RL_LOCK(sc);
		rl_stop(sc);
		RL_UNLOCK(sc);
		callout_drain(&sc->rl_stat_callout);
		ether_ifdetach(ifp);
	}
#if 0
	sc->suspended = 1;
#endif
	if (sc->rl_miibus)
		device_delete_child(dev, sc->rl_miibus);
	bus_generic_detach(dev);

	if (sc->rl_intrhand[0])
		bus_teardown_intr(dev, sc->rl_irq[0], sc->rl_intrhand[0]);
	if (sc->rl_irq[0])
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->rl_irq[0]);
	if (sc->rl_res)
		bus_release_resource(dev, sc->rl_res_type, sc->rl_res_id,
		    sc->rl_res);

	if (ifp)
		if_free(ifp);

	rl_dma_free(sc);

	mtx_destroy(&sc->rl_mtx);

	return (0);
}

static int
rl_dma_alloc(struct rl_softc *sc)
{
	struct rl_dmamap_arg	ctx;
	int			error, i;

	/*
	 * Allocate the parent bus DMA tag appropriate for PCI.
	 */
	error = bus_dma_tag_create(bus_get_dma_tag(sc->rl_dev),	/* parent */
	    1, 0,			/* alignment, boundary */
	    BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    BUS_SPACE_MAXSIZE_32BIT, 0,	/* maxsize, nsegments */
	    BUS_SPACE_MAXSIZE_32BIT,	/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->rl_parent_tag);
	if (error) {
                device_printf(sc->rl_dev,
		    "failed to create parent DMA tag.\n");
		goto fail;
	}
	/* Create DMA tag for Rx memory block. */
	error = bus_dma_tag_create(sc->rl_parent_tag,	/* parent */
	    RL_RX_8139_BUF_ALIGN, 0,	/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    RL_RXBUFLEN + RL_RX_8139_BUF_GUARD_SZ, 1,	/* maxsize,nsegments */
	    RL_RXBUFLEN + RL_RX_8139_BUF_GUARD_SZ,	/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->rl_cdata.rl_rx_tag);
	if (error) {
                device_printf(sc->rl_dev,
		    "failed to create Rx memory block DMA tag.\n");
		goto fail;
	}
	/* Create DMA tag for Tx buffer. */
	error = bus_dma_tag_create(sc->rl_parent_tag,	/* parent */
	    RL_TX_8139_BUF_ALIGN, 0,	/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    MCLBYTES, 1,		/* maxsize, nsegments */
	    MCLBYTES,			/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->rl_cdata.rl_tx_tag);
	if (error) {
                device_printf(sc->rl_dev, "failed to create Tx DMA tag.\n");
		goto fail;
	}

	/*
	 * Allocate DMA'able memory and load DMA map for Rx memory block.
	 */
	error = bus_dmamem_alloc(sc->rl_cdata.rl_rx_tag,
	    (void **)&sc->rl_cdata.rl_rx_buf, BUS_DMA_WAITOK |
	    BUS_DMA_COHERENT | BUS_DMA_ZERO, &sc->rl_cdata.rl_rx_dmamap);
	if (error != 0) {
		device_printf(sc->rl_dev,
		    "failed to allocate Rx DMA memory block.\n");
		goto fail;
	}
	ctx.rl_busaddr = 0;
	error = bus_dmamap_load(sc->rl_cdata.rl_rx_tag,
	    sc->rl_cdata.rl_rx_dmamap, sc->rl_cdata.rl_rx_buf,
	    RL_RXBUFLEN + RL_RX_8139_BUF_GUARD_SZ, rl_dmamap_cb, &ctx,
	    BUS_DMA_NOWAIT);
	if (error != 0 || ctx.rl_busaddr == 0) {
		device_printf(sc->rl_dev,
		    "could not load Rx DMA memory block.\n");
		goto fail;
	}
	sc->rl_cdata.rl_rx_buf_paddr = ctx.rl_busaddr;

	/* Create DMA maps for Tx buffers. */
	for (i = 0; i < RL_TX_LIST_CNT; i++) {
		sc->rl_cdata.rl_tx_chain[i] = NULL;
		sc->rl_cdata.rl_tx_dmamap[i] = NULL;
		error = bus_dmamap_create(sc->rl_cdata.rl_tx_tag, 0,
		    &sc->rl_cdata.rl_tx_dmamap[i]);
		if (error != 0) {
			device_printf(sc->rl_dev,
			    "could not create Tx dmamap.\n");
			goto fail;
		}
	}

	/* Leave a few bytes before the start of the RX ring buffer. */
	sc->rl_cdata.rl_rx_buf_ptr = sc->rl_cdata.rl_rx_buf;
	sc->rl_cdata.rl_rx_buf += RL_RX_8139_BUF_RESERVE;

fail:
	return (error);
}

static void
rl_dma_free(struct rl_softc *sc)
{
	int			i;

	/* Rx memory block. */
	if (sc->rl_cdata.rl_rx_tag != NULL) {
		if (sc->rl_cdata.rl_rx_buf_paddr != 0)
			bus_dmamap_unload(sc->rl_cdata.rl_rx_tag,
			    sc->rl_cdata.rl_rx_dmamap);
		if (sc->rl_cdata.rl_rx_buf_ptr != NULL)
			bus_dmamem_free(sc->rl_cdata.rl_rx_tag,
			    sc->rl_cdata.rl_rx_buf_ptr,
			    sc->rl_cdata.rl_rx_dmamap);
		sc->rl_cdata.rl_rx_buf_ptr = NULL;
		sc->rl_cdata.rl_rx_buf = NULL;
		sc->rl_cdata.rl_rx_buf_paddr = 0;
		bus_dma_tag_destroy(sc->rl_cdata.rl_rx_tag);
		sc->rl_cdata.rl_tx_tag = NULL;
	}

	/* Tx buffers. */
	if (sc->rl_cdata.rl_tx_tag != NULL) {
		for (i = 0; i < RL_TX_LIST_CNT; i++) {
			if (sc->rl_cdata.rl_tx_dmamap[i] != NULL) {
				bus_dmamap_destroy(
				    sc->rl_cdata.rl_tx_tag,
				    sc->rl_cdata.rl_tx_dmamap[i]);
				sc->rl_cdata.rl_tx_dmamap[i] = NULL;
			}
		}
		bus_dma_tag_destroy(sc->rl_cdata.rl_tx_tag);
		sc->rl_cdata.rl_tx_tag = NULL;
	}

	if (sc->rl_parent_tag != NULL) {
		bus_dma_tag_destroy(sc->rl_parent_tag);
		sc->rl_parent_tag = NULL;
	}
}

/*
 * Initialize the transmit descriptors.
 */
static int
rl_list_tx_init(struct rl_softc *sc)
{
	struct rl_chain_data	*cd;
	int			i;

	RL_LOCK_ASSERT(sc);

	cd = &sc->rl_cdata;
	for (i = 0; i < RL_TX_LIST_CNT; i++) {
		cd->rl_tx_chain[i] = NULL;
		CSR_WRITE_4(sc,
		    RL_TXADDR0 + (i * sizeof(uint32_t)), 0x0000000);
	}

	sc->rl_cdata.cur_tx = 0;
	sc->rl_cdata.last_tx = 0;

	return (0);
}

static int
rl_list_rx_init(struct rl_softc *sc)
{

	RL_LOCK_ASSERT(sc);

	bzero(sc->rl_cdata.rl_rx_buf_ptr,
	    RL_RXBUFLEN + RL_RX_8139_BUF_GUARD_SZ);
	bus_dmamap_sync(sc->rl_cdata.rl_tx_tag, sc->rl_cdata.rl_rx_dmamap,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	return (0);
}

/*
 * A frame has been uploaded: pass the resulting mbuf chain up to
 * the higher level protocols.
 *
 * You know there's something wrong with a PCI bus-master chip design
 * when you have to use m_devget().
 *
 * The receive operation is badly documented in the datasheet, so I'll
 * attempt to document it here. The driver provides a buffer area and
 * places its base address in the RX buffer start address register.
 * The chip then begins copying frames into the RX buffer. Each frame
 * is preceded by a 32-bit RX status word which specifies the length
 * of the frame and certain other status bits. Each frame (starting with
 * the status word) is also 32-bit aligned. The frame length is in the
 * first 16 bits of the status word; the lower 15 bits correspond with
 * the 'rx status register' mentioned in the datasheet.
 *
 * Note: to make the Alpha happy, the frame payload needs to be aligned
 * on a 32-bit boundary. To achieve this, we pass RL_ETHER_ALIGN (2 bytes)
 * as the offset argument to m_devget().
 */
static int
rl_rxeof(struct rl_softc *sc)
{
	struct mbuf		*m;
	struct ifnet		*ifp = sc->rl_ifp;
	uint8_t			*rxbufpos;
	int			total_len = 0;
	int			wrap = 0;
	int			rx_npkts = 0;
	uint32_t		rxstat;
	uint16_t		cur_rx;
	uint16_t		limit;
	uint16_t		max_bytes, rx_bytes = 0;

	RL_LOCK_ASSERT(sc);

	bus_dmamap_sync(sc->rl_cdata.rl_rx_tag, sc->rl_cdata.rl_rx_dmamap,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	cur_rx = (CSR_READ_2(sc, RL_CURRXADDR) + 16) % RL_RXBUFLEN;

	/* Do not try to read past this point. */
	limit = CSR_READ_2(sc, RL_CURRXBUF) % RL_RXBUFLEN;

	if (limit < cur_rx)
		max_bytes = (RL_RXBUFLEN - cur_rx) + limit;
	else
		max_bytes = limit - cur_rx;

	while((CSR_READ_1(sc, RL_COMMAND) & RL_CMD_EMPTY_RXBUF) == 0) {
#ifdef DEVICE_POLLING
		if (ifp->if_capenable & IFCAP_POLLING) {
			if (sc->rxcycles <= 0)
				break;
			sc->rxcycles--;
		}
#endif
		rxbufpos = sc->rl_cdata.rl_rx_buf + cur_rx;
		rxstat = le32toh(*(uint32_t *)rxbufpos);

		/*
		 * Here's a totally undocumented fact for you. When the
		 * RealTek chip is in the process of copying a packet into
		 * RAM for you, the length will be 0xfff0. If you spot a
		 * packet header with this value, you need to stop. The
		 * datasheet makes absolutely no mention of this and
		 * RealTek should be shot for this.
		 */
		total_len = rxstat >> 16;
		if (total_len == RL_RXSTAT_UNFINISHED)
			break;

		if (!(rxstat & RL_RXSTAT_RXOK) ||
		    total_len < ETHER_MIN_LEN ||
		    total_len > ETHER_MAX_LEN + ETHER_VLAN_ENCAP_LEN) {
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
			rl_init_locked(sc);
			return (rx_npkts);
		}

		/* No errors; receive the packet. */
		rx_bytes += total_len + 4;

		/*
		 * XXX The RealTek chip includes the CRC with every
		 * received frame, and there's no way to turn this
		 * behavior off (at least, I can't find anything in
		 * the manual that explains how to do it) so we have
		 * to trim off the CRC manually.
		 */
		total_len -= ETHER_CRC_LEN;

		/*
		 * Avoid trying to read more bytes than we know
		 * the chip has prepared for us.
		 */
		if (rx_bytes > max_bytes)
			break;

		rxbufpos = sc->rl_cdata.rl_rx_buf +
			((cur_rx + sizeof(uint32_t)) % RL_RXBUFLEN);
		if (rxbufpos == (sc->rl_cdata.rl_rx_buf + RL_RXBUFLEN))
			rxbufpos = sc->rl_cdata.rl_rx_buf;

		wrap = (sc->rl_cdata.rl_rx_buf + RL_RXBUFLEN) - rxbufpos;
		if (total_len > wrap) {
			m = m_devget(rxbufpos, total_len, RL_ETHER_ALIGN, ifp,
			    NULL);
			if (m != NULL)
				m_copyback(m, wrap, total_len - wrap,
					sc->rl_cdata.rl_rx_buf);
			cur_rx = (total_len - wrap + ETHER_CRC_LEN);
		} else {
			m = m_devget(rxbufpos, total_len, RL_ETHER_ALIGN, ifp,
			    NULL);
			cur_rx += total_len + 4 + ETHER_CRC_LEN;
		}

		/* Round up to 32-bit boundary. */
		cur_rx = (cur_rx + 3) & ~3;
		CSR_WRITE_2(sc, RL_CURRXADDR, cur_rx - 16);

		if (m == NULL) {
			if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
			continue;
		}

		if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
		RL_UNLOCK(sc);
		(*ifp->if_input)(ifp, m);
		RL_LOCK(sc);
		rx_npkts++;
	}

	/* No need to sync Rx memory block as we didn't modify it. */
	return (rx_npkts);
}

/*
 * A frame was downloaded to the chip. It's safe for us to clean up
 * the list buffers.
 */
static void
rl_txeof(struct rl_softc *sc)
{
	struct ifnet		*ifp = sc->rl_ifp;
	uint32_t		txstat;

	RL_LOCK_ASSERT(sc);

	/*
	 * Go through our tx list and free mbufs for those
	 * frames that have been uploaded.
	 */
	do {
		if (RL_LAST_TXMBUF(sc) == NULL)
			break;
		txstat = CSR_READ_4(sc, RL_LAST_TXSTAT(sc));
		if (!(txstat & (RL_TXSTAT_TX_OK|
		    RL_TXSTAT_TX_UNDERRUN|RL_TXSTAT_TXABRT)))
			break;

		if_inc_counter(ifp, IFCOUNTER_COLLISIONS, (txstat & RL_TXSTAT_COLLCNT) >> 24);

		bus_dmamap_sync(sc->rl_cdata.rl_tx_tag, RL_LAST_DMAMAP(sc),
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->rl_cdata.rl_tx_tag, RL_LAST_DMAMAP(sc));
		m_freem(RL_LAST_TXMBUF(sc));
		RL_LAST_TXMBUF(sc) = NULL;
		/*
		 * If there was a transmit underrun, bump the TX threshold.
		 * Make sure not to overflow the 63 * 32byte we can address
		 * with the 6 available bit.
		 */
		if ((txstat & RL_TXSTAT_TX_UNDERRUN) &&
		    (sc->rl_txthresh < 2016))
			sc->rl_txthresh += 32;
		if (txstat & RL_TXSTAT_TX_OK)
			if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
		else {
			int			oldthresh;
			if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
			if ((txstat & RL_TXSTAT_TXABRT) ||
			    (txstat & RL_TXSTAT_OUTOFWIN))
				CSR_WRITE_4(sc, RL_TXCFG, RL_TXCFG_CONFIG);
			oldthresh = sc->rl_txthresh;
			/* error recovery */
			ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
			rl_init_locked(sc);
			/* restore original threshold */
			sc->rl_txthresh = oldthresh;
			return;
		}
		RL_INC(sc->rl_cdata.last_tx);
		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
	} while (sc->rl_cdata.last_tx != sc->rl_cdata.cur_tx);

	if (RL_LAST_TXMBUF(sc) == NULL)
		sc->rl_watchdog_timer = 0;
}

static void
rl_twister_update(struct rl_softc *sc)
{
	uint16_t linktest;
	/*
	 * Table provided by RealTek (Kinston <shangh@realtek.com.tw>) for
	 * Linux driver.  Values undocumented otherwise.
	 */
	static const uint32_t param[4][4] = {
		{0xcb39de43, 0xcb39ce43, 0xfb38de03, 0xcb38de43},
		{0xcb39de43, 0xcb39ce43, 0xcb39ce83, 0xcb39ce83},
		{0xcb39de43, 0xcb39ce43, 0xcb39ce83, 0xcb39ce83},
		{0xbb39de43, 0xbb39ce43, 0xbb39ce83, 0xbb39ce83}
	};

	/*
	 * Tune the so-called twister registers of the RTL8139.  These
	 * are used to compensate for impedance mismatches.  The
	 * method for tuning these registers is undocumented and the
	 * following procedure is collected from public sources.
	 */
	switch (sc->rl_twister)
	{
	case CHK_LINK:
		/*
		 * If we have a sufficient link, then we can proceed in
		 * the state machine to the next stage.  If not, then
		 * disable further tuning after writing sane defaults.
		 */
		if (CSR_READ_2(sc, RL_CSCFG) & RL_CSCFG_LINK_OK) {
			CSR_WRITE_2(sc, RL_CSCFG, RL_CSCFG_LINK_DOWN_OFF_CMD);
			sc->rl_twister = FIND_ROW;
		} else {
			CSR_WRITE_2(sc, RL_CSCFG, RL_CSCFG_LINK_DOWN_CMD);
			CSR_WRITE_4(sc, RL_NWAYTST, RL_NWAYTST_CBL_TEST);
			CSR_WRITE_4(sc, RL_PARA78, RL_PARA78_DEF);
			CSR_WRITE_4(sc, RL_PARA7C, RL_PARA7C_DEF);
			sc->rl_twister = DONE;
		}
		break;
	case FIND_ROW:
		/*
		 * Read how long it took to see the echo to find the tuning
		 * row to use.
		 */
		linktest = CSR_READ_2(sc, RL_CSCFG) & RL_CSCFG_STATUS;
		if (linktest == RL_CSCFG_ROW3)
			sc->rl_twist_row = 3;
		else if (linktest == RL_CSCFG_ROW2)
			sc->rl_twist_row = 2;
		else if (linktest == RL_CSCFG_ROW1)
			sc->rl_twist_row = 1;
		else
			sc->rl_twist_row = 0;
		sc->rl_twist_col = 0;
		sc->rl_twister = SET_PARAM;
		break;
	case SET_PARAM:
		if (sc->rl_twist_col == 0)
			CSR_WRITE_4(sc, RL_NWAYTST, RL_NWAYTST_RESET);
		CSR_WRITE_4(sc, RL_PARA7C,
		    param[sc->rl_twist_row][sc->rl_twist_col]);
		if (++sc->rl_twist_col == 4) {
			if (sc->rl_twist_row == 3)
				sc->rl_twister = RECHK_LONG;
			else
				sc->rl_twister = DONE;
		}
		break;
	case RECHK_LONG:
		/*
		 * For long cables, we have to double check to make sure we
		 * don't mistune.
		 */
		linktest = CSR_READ_2(sc, RL_CSCFG) & RL_CSCFG_STATUS;
		if (linktest == RL_CSCFG_ROW3)
			sc->rl_twister = DONE;
		else {
			CSR_WRITE_4(sc, RL_PARA7C, RL_PARA7C_RETUNE);
			sc->rl_twister = RETUNE;
		}
		break;
	case RETUNE:
		/* Retune for a shorter cable (try column 2) */
		CSR_WRITE_4(sc, RL_NWAYTST, RL_NWAYTST_CBL_TEST);
		CSR_WRITE_4(sc, RL_PARA78, RL_PARA78_DEF);
		CSR_WRITE_4(sc, RL_PARA7C, RL_PARA7C_DEF);
		CSR_WRITE_4(sc, RL_NWAYTST, RL_NWAYTST_RESET);
		sc->rl_twist_row--;
		sc->rl_twist_col = 0;
		sc->rl_twister = SET_PARAM;
		break;

	case DONE:
		break;
	}
	
}

static void
rl_tick(void *xsc)
{
	struct rl_softc		*sc = xsc;
	struct mii_data		*mii;
	int ticks;

	RL_LOCK_ASSERT(sc);
	/*
	 * If we're doing the twister cable calibration, then we need to defer
	 * watchdog timeouts.  This is a no-op in normal operations, but
	 * can falsely trigger when the cable calibration takes a while and
	 * there was traffic ready to go when rl was started.
	 *
	 * We don't defer mii_tick since that updates the mii status, which
	 * helps the twister process, at least according to similar patches
	 * for the Linux driver I found online while doing the fixes.  Worst
	 * case is a few extra mii reads during calibration.
	 */
	mii = device_get_softc(sc->rl_miibus);
	mii_tick(mii);
	if ((sc->rl_flags & RL_FLAG_LINK) == 0)
		rl_miibus_statchg(sc->rl_dev);
	if (sc->rl_twister_enable) {
		if (sc->rl_twister == DONE)
			rl_watchdog(sc);
		else
			rl_twister_update(sc);
		if (sc->rl_twister == DONE)
			ticks = hz;
		else
			ticks = hz / 10;
	} else {
		rl_watchdog(sc);
		ticks = hz;
	}

	callout_reset(&sc->rl_stat_callout, ticks, rl_tick, sc);
}

#ifdef DEVICE_POLLING
static int
rl_poll(struct ifnet *ifp, enum poll_cmd cmd, int count)
{
	struct rl_softc *sc = ifp->if_softc;
	int rx_npkts = 0;

	RL_LOCK(sc);
	if (ifp->if_drv_flags & IFF_DRV_RUNNING)
		rx_npkts = rl_poll_locked(ifp, cmd, count);
	RL_UNLOCK(sc);
	return (rx_npkts);
}

static int
rl_poll_locked(struct ifnet *ifp, enum poll_cmd cmd, int count)
{
	struct rl_softc *sc = ifp->if_softc;
	int rx_npkts;

	RL_LOCK_ASSERT(sc);

	sc->rxcycles = count;
	rx_npkts = rl_rxeof(sc);
	rl_txeof(sc);

	if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd))
		rl_start_locked(ifp);

	if (cmd == POLL_AND_CHECK_STATUS) {
		uint16_t	status;

		/* We should also check the status register. */
		status = CSR_READ_2(sc, RL_ISR);
		if (status == 0xffff)
			return (rx_npkts);
		if (status != 0)
			CSR_WRITE_2(sc, RL_ISR, status);

		/* XXX We should check behaviour on receiver stalls. */

		if (status & RL_ISR_SYSTEM_ERR) {
			ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
			rl_init_locked(sc);
		}
	}
	return (rx_npkts);
}
#endif /* DEVICE_POLLING */

static void
rl_intr(void *arg)
{
	struct rl_softc		*sc = arg;
	struct ifnet		*ifp = sc->rl_ifp;
	uint16_t		status;
	int			count;

	RL_LOCK(sc);

	if (sc->suspended)
		goto done_locked;

#ifdef DEVICE_POLLING
	if  (ifp->if_capenable & IFCAP_POLLING)
		goto done_locked;
#endif

	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0)
		goto done_locked2;
	status = CSR_READ_2(sc, RL_ISR);
	if (status == 0xffff || (status & RL_INTRS) == 0)
		goto done_locked;
	/*
	 * Ours, disable further interrupts.
	 */
	CSR_WRITE_2(sc, RL_IMR, 0);
	for (count = 16; count > 0; count--) {
		CSR_WRITE_2(sc, RL_ISR, status);
		if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
			if (status & (RL_ISR_RX_OK | RL_ISR_RX_ERR))
				rl_rxeof(sc);
			if (status & (RL_ISR_TX_OK | RL_ISR_TX_ERR))
				rl_txeof(sc);
			if (status & RL_ISR_SYSTEM_ERR) {
				ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
				rl_init_locked(sc);
				RL_UNLOCK(sc);
				return;
			}
		}
		status = CSR_READ_2(sc, RL_ISR);
		/* If the card has gone away, the read returns 0xffff. */
		if (status == 0xffff || (status & RL_INTRS) == 0)
			break;
	}

	if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd))
		rl_start_locked(ifp);

done_locked2:
	if (ifp->if_drv_flags & IFF_DRV_RUNNING)
		CSR_WRITE_2(sc, RL_IMR, RL_INTRS);
done_locked:
	RL_UNLOCK(sc);
}

/*
 * Encapsulate an mbuf chain in a descriptor by coupling the mbuf data
 * pointers to the fragment pointers.
 */
static int
rl_encap(struct rl_softc *sc, struct mbuf **m_head)
{
	struct mbuf		*m;
	bus_dma_segment_t	txsegs[1];
	int			error, nsegs, padlen;

	RL_LOCK_ASSERT(sc);

	m = *m_head;
	padlen = 0;
	/*
	 * Hardware doesn't auto-pad, so we have to make sure
	 * pad short frames out to the minimum frame length.
	 */
	if (m->m_pkthdr.len < RL_MIN_FRAMELEN)
		padlen = RL_MIN_FRAMELEN - m->m_pkthdr.len;
	/*
	 * The RealTek is brain damaged and wants longword-aligned
	 * TX buffers, plus we can only have one fragment buffer
	 * per packet. We have to copy pretty much all the time.
	 */
	if (m->m_next != NULL || (mtod(m, uintptr_t) & 3) != 0 ||
	    (padlen > 0 && M_TRAILINGSPACE(m) < padlen)) {
		m = m_defrag(*m_head, M_NOWAIT);
		if (m == NULL) {
			m_freem(*m_head);
			*m_head = NULL;
			return (ENOMEM);
		}
	}
	*m_head = m;

	if (padlen > 0) {
		/*
		 * Make security-conscious people happy: zero out the
		 * bytes in the pad area, since we don't know what
		 * this mbuf cluster buffer's previous user might
		 * have left in it.
		 */
		bzero(mtod(m, char *) + m->m_pkthdr.len, padlen);
		m->m_pkthdr.len += padlen;
		m->m_len = m->m_pkthdr.len;
	}

	error = bus_dmamap_load_mbuf_sg(sc->rl_cdata.rl_tx_tag,
	    RL_CUR_DMAMAP(sc), m, txsegs, &nsegs, 0);
	if (error != 0)
		return (error);
	if (nsegs == 0) {
		m_freem(*m_head);
		*m_head = NULL;
		return (EIO);
	}

	RL_CUR_TXMBUF(sc) = m;
	bus_dmamap_sync(sc->rl_cdata.rl_tx_tag, RL_CUR_DMAMAP(sc),
	    BUS_DMASYNC_PREWRITE);
	CSR_WRITE_4(sc, RL_CUR_TXADDR(sc), RL_ADDR_LO(txsegs[0].ds_addr));

	return (0);
}

/*
 * Main transmit routine.
 */
static void
rl_start(struct ifnet *ifp)
{
	struct rl_softc		*sc = ifp->if_softc;

	RL_LOCK(sc);
	rl_start_locked(ifp);
	RL_UNLOCK(sc);
}

static void
rl_start_locked(struct ifnet *ifp)
{
	struct rl_softc		*sc = ifp->if_softc;
	struct mbuf		*m_head = NULL;

	RL_LOCK_ASSERT(sc);

	if ((ifp->if_drv_flags & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) !=
	    IFF_DRV_RUNNING || (sc->rl_flags & RL_FLAG_LINK) == 0)
		return;

	while (RL_CUR_TXMBUF(sc) == NULL) {

		IFQ_DRV_DEQUEUE(&ifp->if_snd, m_head);

		if (m_head == NULL)
			break;

		if (rl_encap(sc, &m_head)) {
			if (m_head == NULL)
				break;
			IFQ_DRV_PREPEND(&ifp->if_snd, m_head);
			ifp->if_drv_flags |= IFF_DRV_OACTIVE;
			break;
		}

		/* Pass a copy of this mbuf chain to the bpf subsystem. */
		BPF_MTAP(ifp, RL_CUR_TXMBUF(sc));

		/* Transmit the frame. */
		CSR_WRITE_4(sc, RL_CUR_TXSTAT(sc),
		    RL_TXTHRESH(sc->rl_txthresh) |
		    RL_CUR_TXMBUF(sc)->m_pkthdr.len);

		RL_INC(sc->rl_cdata.cur_tx);

		/* Set a timeout in case the chip goes out to lunch. */
		sc->rl_watchdog_timer = 5;
	}

	/*
	 * We broke out of the loop because all our TX slots are
	 * full. Mark the NIC as busy until it drains some of the
	 * packets from the queue.
	 */
	if (RL_CUR_TXMBUF(sc) != NULL)
		ifp->if_drv_flags |= IFF_DRV_OACTIVE;
}

static void
rl_init(void *xsc)
{
	struct rl_softc		*sc = xsc;

	RL_LOCK(sc);
	rl_init_locked(sc);
	RL_UNLOCK(sc);
}

static void
rl_init_locked(struct rl_softc *sc)
{
	struct ifnet		*ifp = sc->rl_ifp;
	struct mii_data		*mii;
	uint32_t		eaddr[2];

	RL_LOCK_ASSERT(sc);

	mii = device_get_softc(sc->rl_miibus);

	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
		return;

	/*
	 * Cancel pending I/O and free all RX/TX buffers.
	 */
	rl_stop(sc);

	rl_reset(sc);
	if (sc->rl_twister_enable) {
		/*
		 * Reset twister register tuning state.  The twister
		 * registers and their tuning are undocumented, but
		 * are necessary to cope with bad links.  rl_twister =
		 * DONE here will disable this entirely.
		 */
		sc->rl_twister = CHK_LINK;
	}

	/*
	 * Init our MAC address.  Even though the chipset
	 * documentation doesn't mention it, we need to enter "Config
	 * register write enable" mode to modify the ID registers.
	 */
	CSR_WRITE_1(sc, RL_EECMD, RL_EEMODE_WRITECFG);
	bzero(eaddr, sizeof(eaddr));
	bcopy(IF_LLADDR(sc->rl_ifp), eaddr, ETHER_ADDR_LEN);
	CSR_WRITE_STREAM_4(sc, RL_IDR0, eaddr[0]);
	CSR_WRITE_STREAM_4(sc, RL_IDR4, eaddr[1]);
	CSR_WRITE_1(sc, RL_EECMD, RL_EEMODE_OFF);

	/* Init the RX memory block pointer register. */
	CSR_WRITE_4(sc, RL_RXADDR, sc->rl_cdata.rl_rx_buf_paddr +
	    RL_RX_8139_BUF_RESERVE);
	/* Init TX descriptors. */
	rl_list_tx_init(sc);
	/* Init Rx memory block. */
	rl_list_rx_init(sc);

	/*
	 * Enable transmit and receive.
	 */
	CSR_WRITE_1(sc, RL_COMMAND, RL_CMD_TX_ENB|RL_CMD_RX_ENB);

	/*
	 * Set the initial TX and RX configuration.
	 */
	CSR_WRITE_4(sc, RL_TXCFG, RL_TXCFG_CONFIG);
	CSR_WRITE_4(sc, RL_RXCFG, RL_RXCFG_CONFIG);

	/* Set RX filter. */
	rl_rxfilter(sc);

#ifdef DEVICE_POLLING
	/* Disable interrupts if we are polling. */
	if (ifp->if_capenable & IFCAP_POLLING)
		CSR_WRITE_2(sc, RL_IMR, 0);
	else
#endif
	/* Enable interrupts. */
	CSR_WRITE_2(sc, RL_IMR, RL_INTRS);

	/* Set initial TX threshold */
	sc->rl_txthresh = RL_TX_THRESH_INIT;

	/* Start RX/TX process. */
	CSR_WRITE_4(sc, RL_MISSEDPKT, 0);

	/* Enable receiver and transmitter. */
	CSR_WRITE_1(sc, RL_COMMAND, RL_CMD_TX_ENB|RL_CMD_RX_ENB);

	sc->rl_flags &= ~RL_FLAG_LINK;
	mii_mediachg(mii);

	CSR_WRITE_1(sc, sc->rl_cfg1, RL_CFG1_DRVLOAD|RL_CFG1_FULLDUPLEX);

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

	callout_reset(&sc->rl_stat_callout, hz, rl_tick, sc);
}

/*
 * Set media options.
 */
static int
rl_ifmedia_upd(struct ifnet *ifp)
{
	struct rl_softc		*sc = ifp->if_softc;
	struct mii_data		*mii;

	mii = device_get_softc(sc->rl_miibus);

	RL_LOCK(sc);
	mii_mediachg(mii);
	RL_UNLOCK(sc);

	return (0);
}

/*
 * Report current media status.
 */
static void
rl_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct rl_softc		*sc = ifp->if_softc;
	struct mii_data		*mii;

	mii = device_get_softc(sc->rl_miibus);

	RL_LOCK(sc);
	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
	RL_UNLOCK(sc);
}

static int
rl_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct ifreq		*ifr = (struct ifreq *)data;
	struct mii_data		*mii;
	struct rl_softc		*sc = ifp->if_softc;
	int			error = 0, mask;

	switch (command) {
	case SIOCSIFFLAGS:
		RL_LOCK(sc);
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_drv_flags & IFF_DRV_RUNNING &&
			    ((ifp->if_flags ^ sc->rl_if_flags) &
                            (IFF_PROMISC | IFF_ALLMULTI)))
				rl_rxfilter(sc);
                        else
				rl_init_locked(sc);
                } else if (ifp->if_drv_flags & IFF_DRV_RUNNING)
			rl_stop(sc);
		sc->rl_if_flags = ifp->if_flags;
		RL_UNLOCK(sc);
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		RL_LOCK(sc);
		rl_rxfilter(sc);
		RL_UNLOCK(sc);
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		mii = device_get_softc(sc->rl_miibus);
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, command);
		break;
	case SIOCSIFCAP:
		mask = ifr->ifr_reqcap ^ ifp->if_capenable;
#ifdef DEVICE_POLLING
		if (ifr->ifr_reqcap & IFCAP_POLLING &&
		    !(ifp->if_capenable & IFCAP_POLLING)) {
			error = ether_poll_register(rl_poll, ifp);
			if (error)
				return(error);
			RL_LOCK(sc);
			/* Disable interrupts */
			CSR_WRITE_2(sc, RL_IMR, 0x0000);
			ifp->if_capenable |= IFCAP_POLLING;
			RL_UNLOCK(sc);
			return (error);
			
		}
		if (!(ifr->ifr_reqcap & IFCAP_POLLING) &&
		    ifp->if_capenable & IFCAP_POLLING) {
			error = ether_poll_deregister(ifp);
			/* Enable interrupts. */
			RL_LOCK(sc);
			CSR_WRITE_2(sc, RL_IMR, RL_INTRS);
			ifp->if_capenable &= ~IFCAP_POLLING;
			RL_UNLOCK(sc);
			return (error);
		}
#endif /* DEVICE_POLLING */
		if ((mask & IFCAP_WOL) != 0 &&
		    (ifp->if_capabilities & IFCAP_WOL) != 0) {
			if ((mask & IFCAP_WOL_UCAST) != 0)
				ifp->if_capenable ^= IFCAP_WOL_UCAST;
			if ((mask & IFCAP_WOL_MCAST) != 0)
				ifp->if_capenable ^= IFCAP_WOL_MCAST;
			if ((mask & IFCAP_WOL_MAGIC) != 0)
				ifp->if_capenable ^= IFCAP_WOL_MAGIC;
		}
		break;
	default:
		error = ether_ioctl(ifp, command, data);
		break;
	}

	return (error);
}

static void
rl_watchdog(struct rl_softc *sc)
{

	RL_LOCK_ASSERT(sc);

	if (sc->rl_watchdog_timer == 0 || --sc->rl_watchdog_timer >0)
		return;

	device_printf(sc->rl_dev, "watchdog timeout\n");
	if_inc_counter(sc->rl_ifp, IFCOUNTER_OERRORS, 1);

	rl_txeof(sc);
	rl_rxeof(sc);
	sc->rl_ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
	rl_init_locked(sc);
}

/*
 * Stop the adapter and free any mbufs allocated to the
 * RX and TX lists.
 */
static void
rl_stop(struct rl_softc *sc)
{
	int			i;
	struct ifnet		*ifp = sc->rl_ifp;

	RL_LOCK_ASSERT(sc);

	sc->rl_watchdog_timer = 0;
	callout_stop(&sc->rl_stat_callout);
	ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);
	sc->rl_flags &= ~RL_FLAG_LINK;

	CSR_WRITE_1(sc, RL_COMMAND, 0x00);
	CSR_WRITE_2(sc, RL_IMR, 0x0000);
	for (i = 0; i < RL_TIMEOUT; i++) {
		DELAY(10);
		if ((CSR_READ_1(sc, RL_COMMAND) &
		    (RL_CMD_RX_ENB | RL_CMD_TX_ENB)) == 0)
			break;
	}
	if (i == RL_TIMEOUT)
		device_printf(sc->rl_dev, "Unable to stop Tx/Rx MAC\n");

	/*
	 * Free the TX list buffers.
	 */
	for (i = 0; i < RL_TX_LIST_CNT; i++) {
		if (sc->rl_cdata.rl_tx_chain[i] != NULL) {
			bus_dmamap_sync(sc->rl_cdata.rl_tx_tag,
			    sc->rl_cdata.rl_tx_dmamap[i],
			    BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->rl_cdata.rl_tx_tag,
			    sc->rl_cdata.rl_tx_dmamap[i]);
			m_freem(sc->rl_cdata.rl_tx_chain[i]);
			sc->rl_cdata.rl_tx_chain[i] = NULL;
			CSR_WRITE_4(sc, RL_TXADDR0 + (i * sizeof(uint32_t)),
			    0x0000000);
		}
	}
}

/*
 * Device suspend routine.  Stop the interface and save some PCI
 * settings in case the BIOS doesn't restore them properly on
 * resume.
 */
static int
rl_suspend(device_t dev)
{
	struct rl_softc		*sc;

	sc = device_get_softc(dev);

	RL_LOCK(sc);
	rl_stop(sc);
	rl_setwol(sc);
	sc->suspended = 1;
	RL_UNLOCK(sc);

	return (0);
}

/*
 * Device resume routine.  Restore some PCI settings in case the BIOS
 * doesn't, re-enable busmastering, and restart the interface if
 * appropriate.
 */
static int
rl_resume(device_t dev)
{
	struct rl_softc		*sc;
	struct ifnet		*ifp;
	int			pmc;
	uint16_t		pmstat;

	sc = device_get_softc(dev);
	ifp = sc->rl_ifp;

	RL_LOCK(sc);

	if ((ifp->if_capabilities & IFCAP_WOL) != 0 &&
	    pci_find_cap(sc->rl_dev, PCIY_PMG, &pmc) == 0) {
		/* Disable PME and clear PME status. */
		pmstat = pci_read_config(sc->rl_dev,
		    pmc + PCIR_POWER_STATUS, 2);
		if ((pmstat & PCIM_PSTAT_PMEENABLE) != 0) {
			pmstat &= ~PCIM_PSTAT_PMEENABLE;
			pci_write_config(sc->rl_dev,
			    pmc + PCIR_POWER_STATUS, pmstat, 2);
		}
		/*
		 * Clear WOL matching such that normal Rx filtering
		 * wouldn't interfere with WOL patterns.
		 */
		rl_clrwol(sc);
	}

	/* reinitialize interface if necessary */
	if (ifp->if_flags & IFF_UP)
		rl_init_locked(sc);

	sc->suspended = 0;

	RL_UNLOCK(sc);

	return (0);
}

/*
 * Stop all chip I/O so that the kernel's probe routines don't
 * get confused by errant DMAs when rebooting.
 */
static int
rl_shutdown(device_t dev)
{
	struct rl_softc		*sc;

	sc = device_get_softc(dev);

	RL_LOCK(sc);
	rl_stop(sc);
	/*
	 * Mark interface as down since otherwise we will panic if
	 * interrupt comes in later on, which can happen in some
	 * cases.
	 */
	sc->rl_ifp->if_flags &= ~IFF_UP;
	rl_setwol(sc);
	RL_UNLOCK(sc);

	return (0);
}

static void
rl_setwol(struct rl_softc *sc)
{
	struct ifnet		*ifp;
	int			pmc;
	uint16_t		pmstat;
	uint8_t			v;

	RL_LOCK_ASSERT(sc);

	ifp = sc->rl_ifp;
	if ((ifp->if_capabilities & IFCAP_WOL) == 0)
		return;
	if (pci_find_cap(sc->rl_dev, PCIY_PMG, &pmc) != 0)
		return;

	/* Enable config register write. */
	CSR_WRITE_1(sc, RL_EECMD, RL_EE_MODE);

	/* Enable PME. */
	v = CSR_READ_1(sc, sc->rl_cfg1);
	v &= ~RL_CFG1_PME;
	if ((ifp->if_capenable & IFCAP_WOL) != 0)
		v |= RL_CFG1_PME;
	CSR_WRITE_1(sc, sc->rl_cfg1, v);

	v = CSR_READ_1(sc, sc->rl_cfg3);
	v &= ~(RL_CFG3_WOL_LINK | RL_CFG3_WOL_MAGIC);
	if ((ifp->if_capenable & IFCAP_WOL_MAGIC) != 0)
		v |= RL_CFG3_WOL_MAGIC;
	CSR_WRITE_1(sc, sc->rl_cfg3, v);

	v = CSR_READ_1(sc, sc->rl_cfg5);
	v &= ~(RL_CFG5_WOL_BCAST | RL_CFG5_WOL_MCAST | RL_CFG5_WOL_UCAST);
	v &= ~RL_CFG5_WOL_LANWAKE;
	if ((ifp->if_capenable & IFCAP_WOL_UCAST) != 0)
		v |= RL_CFG5_WOL_UCAST;
	if ((ifp->if_capenable & IFCAP_WOL_MCAST) != 0)
		v |= RL_CFG5_WOL_MCAST | RL_CFG5_WOL_BCAST;
	if ((ifp->if_capenable & IFCAP_WOL) != 0)
		v |= RL_CFG5_WOL_LANWAKE;
	CSR_WRITE_1(sc, sc->rl_cfg5, v);

	/* Config register write done. */
	CSR_WRITE_1(sc, RL_EECMD, RL_EEMODE_OFF);

	/* Request PME if WOL is requested. */
	pmstat = pci_read_config(sc->rl_dev, pmc + PCIR_POWER_STATUS, 2);
	pmstat &= ~(PCIM_PSTAT_PME | PCIM_PSTAT_PMEENABLE);
	if ((ifp->if_capenable & IFCAP_WOL) != 0)
		pmstat |= PCIM_PSTAT_PME | PCIM_PSTAT_PMEENABLE;
	pci_write_config(sc->rl_dev, pmc + PCIR_POWER_STATUS, pmstat, 2);
}

static void
rl_clrwol(struct rl_softc *sc)
{
	struct ifnet		*ifp;
	uint8_t			v;

	ifp = sc->rl_ifp;
	if ((ifp->if_capabilities & IFCAP_WOL) == 0)
		return;

	/* Enable config register write. */
	CSR_WRITE_1(sc, RL_EECMD, RL_EE_MODE);

	v = CSR_READ_1(sc, sc->rl_cfg3);
	v &= ~(RL_CFG3_WOL_LINK | RL_CFG3_WOL_MAGIC);
	CSR_WRITE_1(sc, sc->rl_cfg3, v);

	/* Config register write done. */
	CSR_WRITE_1(sc, RL_EECMD, RL_EEMODE_OFF);

	v = CSR_READ_1(sc, sc->rl_cfg5);
	v &= ~(RL_CFG5_WOL_BCAST | RL_CFG5_WOL_MCAST | RL_CFG5_WOL_UCAST);
	v &= ~RL_CFG5_WOL_LANWAKE;
	CSR_WRITE_1(sc, sc->rl_cfg5, v);
}
