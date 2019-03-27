/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
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
 * 3Com 3c90x Etherlink XL PCI NIC driver
 *
 * Supports the 3Com "boomerang", "cyclone" and "hurricane" PCI
 * bus-master chips (3c90x cards and embedded controllers) including
 * the following:
 *
 * 3Com 3c900-TPO	10Mbps/RJ-45
 * 3Com 3c900-COMBO	10Mbps/RJ-45,AUI,BNC
 * 3Com 3c905-TX	10/100Mbps/RJ-45
 * 3Com 3c905-T4	10/100Mbps/RJ-45
 * 3Com 3c900B-TPO	10Mbps/RJ-45
 * 3Com 3c900B-COMBO	10Mbps/RJ-45,AUI,BNC
 * 3Com 3c900B-TPC	10Mbps/RJ-45,BNC
 * 3Com 3c900B-FL	10Mbps/Fiber-optic
 * 3Com 3c905B-COMBO	10/100Mbps/RJ-45,AUI,BNC
 * 3Com 3c905B-TX	10/100Mbps/RJ-45
 * 3Com 3c905B-FL/FX	10/100Mbps/Fiber-optic
 * 3Com 3c905C-TX	10/100Mbps/RJ-45 (Tornado ASIC)
 * 3Com 3c980-TX	10/100Mbps server adapter (Hurricane ASIC)
 * 3Com 3c980C-TX	10/100Mbps server adapter (Tornado ASIC)
 * 3Com 3cSOHO100-TX	10/100Mbps/RJ-45 (Hurricane ASIC)
 * 3Com 3c450-TX	10/100Mbps/RJ-45 (Tornado ASIC)
 * 3Com 3c555		10/100Mbps/RJ-45 (MiniPCI, Laptop Hurricane)
 * 3Com 3c556		10/100Mbps/RJ-45 (MiniPCI, Hurricane ASIC)
 * 3Com 3c556B		10/100Mbps/RJ-45 (MiniPCI, Hurricane ASIC)
 * 3Com 3c575TX		10/100Mbps/RJ-45 (Cardbus, Hurricane ASIC)
 * 3Com 3c575B		10/100Mbps/RJ-45 (Cardbus, Hurricane ASIC)
 * 3Com 3c575C		10/100Mbps/RJ-45 (Cardbus, Hurricane ASIC)
 * 3Com 3cxfem656	10/100Mbps/RJ-45 (Cardbus, Hurricane ASIC)
 * 3Com 3cxfem656b	10/100Mbps/RJ-45 (Cardbus, Hurricane ASIC)
 * 3Com 3cxfem656c	10/100Mbps/RJ-45 (Cardbus, Tornado ASIC)
 * Dell Optiplex GX1 on-board 3c918 10/100Mbps/RJ-45
 * Dell on-board 3c920 10/100Mbps/RJ-45
 * Dell Precision on-board 3c905B 10/100Mbps/RJ-45
 * Dell Latitude laptop docking station embedded 3c905-TX
 *
 * Written by Bill Paul <wpaul@ctr.columbia.edu>
 * Electrical Engineering Department
 * Columbia University, New York City
 */
/*
 * The 3c90x series chips use a bus-master DMA interface for transferring
 * packets to and from the controller chip. Some of the "vortex" cards
 * (3c59x) also supported a bus master mode, however for those chips
 * you could only DMA packets to/from a contiguous memory buffer. For
 * transmission this would mean copying the contents of the queued mbuf
 * chain into an mbuf cluster and then DMAing the cluster. This extra
 * copy would sort of defeat the purpose of the bus master support for
 * any packet that doesn't fit into a single mbuf.
 *
 * By contrast, the 3c90x cards support a fragment-based bus master
 * mode where mbuf chains can be encapsulated using TX descriptors.
 * This is similar to other PCI chips such as the Texas Instruments
 * ThunderLAN and the Intel 82557/82558.
 *
 * The "vortex" driver (if_vx.c) happens to work for the "boomerang"
 * bus master chips because they maintain the old PIO interface for
 * backwards compatibility, but starting with the 3c905B and the
 * "cyclone" chips, the compatibility interface has been dropped.
 * Since using bus master DMA is a big win, we use this driver to
 * support the PCI "boomerang" chips even though they work with the
 * "vortex" driver in order to obtain better performance.
 */

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_device_polling.h"
#endif

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/socket.h>
#include <sys/taskqueue.h>

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

MODULE_DEPEND(xl, pci, 1, 1, 1);
MODULE_DEPEND(xl, ether, 1, 1, 1);
MODULE_DEPEND(xl, miibus, 1, 1, 1);

/* "device miibus" required.  See GENERIC if you get errors here. */
#include "miibus_if.h"

#include <dev/xl/if_xlreg.h>

/*
 * TX Checksumming is disabled by default for two reasons:
 * - TX Checksumming will occasionally produce corrupt packets
 * - TX Checksumming seems to reduce performance
 *
 * Only 905B/C cards were reported to have this problem, it is possible
 * that later chips _may_ be immune.
 */
#define	XL905B_TXCSUM_BROKEN	1

#ifdef XL905B_TXCSUM_BROKEN
#define XL905B_CSUM_FEATURES	0
#else
#define XL905B_CSUM_FEATURES	(CSUM_IP | CSUM_TCP | CSUM_UDP)
#endif

/*
 * Various supported device vendors/types and their names.
 */
static const struct xl_type xl_devs[] = {
	{ TC_VENDORID, TC_DEVICEID_BOOMERANG_10BT,
		"3Com 3c900-TPO Etherlink XL" },
	{ TC_VENDORID, TC_DEVICEID_BOOMERANG_10BT_COMBO,
		"3Com 3c900-COMBO Etherlink XL" },
	{ TC_VENDORID, TC_DEVICEID_BOOMERANG_10_100BT,
		"3Com 3c905-TX Fast Etherlink XL" },
	{ TC_VENDORID, TC_DEVICEID_BOOMERANG_100BT4,
		"3Com 3c905-T4 Fast Etherlink XL" },
	{ TC_VENDORID, TC_DEVICEID_KRAKATOA_10BT,
		"3Com 3c900B-TPO Etherlink XL" },
	{ TC_VENDORID, TC_DEVICEID_KRAKATOA_10BT_COMBO,
		"3Com 3c900B-COMBO Etherlink XL" },
	{ TC_VENDORID, TC_DEVICEID_KRAKATOA_10BT_TPC,
		"3Com 3c900B-TPC Etherlink XL" },
	{ TC_VENDORID, TC_DEVICEID_CYCLONE_10FL,
		"3Com 3c900B-FL Etherlink XL" },
	{ TC_VENDORID, TC_DEVICEID_HURRICANE_10_100BT,
		"3Com 3c905B-TX Fast Etherlink XL" },
	{ TC_VENDORID, TC_DEVICEID_CYCLONE_10_100BT4,
		"3Com 3c905B-T4 Fast Etherlink XL" },
	{ TC_VENDORID, TC_DEVICEID_CYCLONE_10_100FX,
		"3Com 3c905B-FX/SC Fast Etherlink XL" },
	{ TC_VENDORID, TC_DEVICEID_CYCLONE_10_100_COMBO,
		"3Com 3c905B-COMBO Fast Etherlink XL" },
	{ TC_VENDORID, TC_DEVICEID_TORNADO_10_100BT,
		"3Com 3c905C-TX Fast Etherlink XL" },
	{ TC_VENDORID, TC_DEVICEID_TORNADO_10_100BT_920B,
		"3Com 3c920B-EMB Integrated Fast Etherlink XL" },
	{ TC_VENDORID, TC_DEVICEID_TORNADO_10_100BT_920B_WNM,
		"3Com 3c920B-EMB-WNM Integrated Fast Etherlink XL" },
	{ TC_VENDORID, TC_DEVICEID_HURRICANE_10_100BT_SERV,
		"3Com 3c980 Fast Etherlink XL" },
	{ TC_VENDORID, TC_DEVICEID_TORNADO_10_100BT_SERV,
		"3Com 3c980C Fast Etherlink XL" },
	{ TC_VENDORID, TC_DEVICEID_HURRICANE_SOHO100TX,
		"3Com 3cSOHO100-TX OfficeConnect" },
	{ TC_VENDORID, TC_DEVICEID_TORNADO_HOMECONNECT,
		"3Com 3c450-TX HomeConnect" },
	{ TC_VENDORID, TC_DEVICEID_HURRICANE_555,
		"3Com 3c555 Fast Etherlink XL" },
	{ TC_VENDORID, TC_DEVICEID_HURRICANE_556,
		"3Com 3c556 Fast Etherlink XL" },
	{ TC_VENDORID, TC_DEVICEID_HURRICANE_556B,
		"3Com 3c556B Fast Etherlink XL" },
	{ TC_VENDORID, TC_DEVICEID_HURRICANE_575A,
		"3Com 3c575TX Fast Etherlink XL" },
	{ TC_VENDORID, TC_DEVICEID_HURRICANE_575B,
		"3Com 3c575B Fast Etherlink XL" },
	{ TC_VENDORID, TC_DEVICEID_HURRICANE_575C,
		"3Com 3c575C Fast Etherlink XL" },
	{ TC_VENDORID, TC_DEVICEID_HURRICANE_656,
		"3Com 3c656 Fast Etherlink XL" },
	{ TC_VENDORID, TC_DEVICEID_HURRICANE_656B,
		"3Com 3c656B Fast Etherlink XL" },
	{ TC_VENDORID, TC_DEVICEID_TORNADO_656C,
		"3Com 3c656C Fast Etherlink XL" },
	{ 0, 0, NULL }
};

static int xl_probe(device_t);
static int xl_attach(device_t);
static int xl_detach(device_t);

static int xl_newbuf(struct xl_softc *, struct xl_chain_onefrag *);
static void xl_tick(void *);
static void xl_stats_update(struct xl_softc *);
static int xl_encap(struct xl_softc *, struct xl_chain *, struct mbuf **);
static int xl_rxeof(struct xl_softc *);
static void xl_rxeof_task(void *, int);
static int xl_rx_resync(struct xl_softc *);
static void xl_txeof(struct xl_softc *);
static void xl_txeof_90xB(struct xl_softc *);
static void xl_txeoc(struct xl_softc *);
static void xl_intr(void *);
static void xl_start(struct ifnet *);
static void xl_start_locked(struct ifnet *);
static void xl_start_90xB_locked(struct ifnet *);
static int xl_ioctl(struct ifnet *, u_long, caddr_t);
static void xl_init(void *);
static void xl_init_locked(struct xl_softc *);
static void xl_stop(struct xl_softc *);
static int xl_watchdog(struct xl_softc *);
static int xl_shutdown(device_t);
static int xl_suspend(device_t);
static int xl_resume(device_t);
static void xl_setwol(struct xl_softc *);

#ifdef DEVICE_POLLING
static int xl_poll(struct ifnet *ifp, enum poll_cmd cmd, int count);
static int xl_poll_locked(struct ifnet *ifp, enum poll_cmd cmd, int count);
#endif

static int xl_ifmedia_upd(struct ifnet *);
static void xl_ifmedia_sts(struct ifnet *, struct ifmediareq *);

static int xl_eeprom_wait(struct xl_softc *);
static int xl_read_eeprom(struct xl_softc *, caddr_t, int, int, int);

static void xl_rxfilter(struct xl_softc *);
static void xl_rxfilter_90x(struct xl_softc *);
static void xl_rxfilter_90xB(struct xl_softc *);
static void xl_setcfg(struct xl_softc *);
static void xl_setmode(struct xl_softc *, int);
static void xl_reset(struct xl_softc *);
static int xl_list_rx_init(struct xl_softc *);
static int xl_list_tx_init(struct xl_softc *);
static int xl_list_tx_init_90xB(struct xl_softc *);
static void xl_wait(struct xl_softc *);
static void xl_mediacheck(struct xl_softc *);
static void xl_choose_media(struct xl_softc *sc, int *media);
static void xl_choose_xcvr(struct xl_softc *, int);
static void xl_dma_map_addr(void *, bus_dma_segment_t *, int, int);
#ifdef notdef
static void xl_testpacket(struct xl_softc *);
#endif

static int xl_miibus_readreg(device_t, int, int);
static int xl_miibus_writereg(device_t, int, int, int);
static void xl_miibus_statchg(device_t);
static void xl_miibus_mediainit(device_t);

/*
 * MII bit-bang glue
 */
static uint32_t xl_mii_bitbang_read(device_t);
static void xl_mii_bitbang_write(device_t, uint32_t);

static const struct mii_bitbang_ops xl_mii_bitbang_ops = {
	xl_mii_bitbang_read,
	xl_mii_bitbang_write,
	{
		XL_MII_DATA,		/* MII_BIT_MDO */
		XL_MII_DATA,		/* MII_BIT_MDI */
		XL_MII_CLK,		/* MII_BIT_MDC */
		XL_MII_DIR,		/* MII_BIT_DIR_HOST_PHY */
		0,			/* MII_BIT_DIR_PHY_HOST */
	}
};

static device_method_t xl_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		xl_probe),
	DEVMETHOD(device_attach,	xl_attach),
	DEVMETHOD(device_detach,	xl_detach),
	DEVMETHOD(device_shutdown,	xl_shutdown),
	DEVMETHOD(device_suspend,	xl_suspend),
	DEVMETHOD(device_resume,	xl_resume),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	xl_miibus_readreg),
	DEVMETHOD(miibus_writereg,	xl_miibus_writereg),
	DEVMETHOD(miibus_statchg,	xl_miibus_statchg),
	DEVMETHOD(miibus_mediainit,	xl_miibus_mediainit),

	DEVMETHOD_END
};

static driver_t xl_driver = {
	"xl",
	xl_methods,
	sizeof(struct xl_softc)
};

static devclass_t xl_devclass;

DRIVER_MODULE_ORDERED(xl, pci, xl_driver, xl_devclass, NULL, NULL,
    SI_ORDER_ANY);
DRIVER_MODULE(miibus, xl, miibus_driver, miibus_devclass, NULL, NULL);
MODULE_PNP_INFO("U16:vendor;U16:device;D:#", pci, xl, xl_devs,
    nitems(xl_devs) - 1);

static void
xl_dma_map_addr(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	u_int32_t *paddr;

	paddr = arg;
	*paddr = segs->ds_addr;
}

/*
 * Murphy's law says that it's possible the chip can wedge and
 * the 'command in progress' bit may never clear. Hence, we wait
 * only a finite amount of time to avoid getting caught in an
 * infinite loop. Normally this delay routine would be a macro,
 * but it isn't called during normal operation so we can afford
 * to make it a function.  Suppress warning when card gone.
 */
static void
xl_wait(struct xl_softc *sc)
{
	int			i;

	for (i = 0; i < XL_TIMEOUT; i++) {
		if ((CSR_READ_2(sc, XL_STATUS) & XL_STAT_CMDBUSY) == 0)
			break;
	}

	if (i == XL_TIMEOUT && bus_child_present(sc->xl_dev))
		device_printf(sc->xl_dev, "command never completed!\n");
}

/*
 * MII access routines are provided for adapters with external
 * PHYs (3c905-TX, 3c905-T4, 3c905B-T4) and those with built-in
 * autoneg logic that's faked up to look like a PHY (3c905B-TX).
 * Note: if you don't perform the MDIO operations just right,
 * it's possible to end up with code that works correctly with
 * some chips/CPUs/processor speeds/bus speeds/etc but not
 * with others.
 */

/*
 * Read the MII serial port for the MII bit-bang module.
 */
static uint32_t
xl_mii_bitbang_read(device_t dev)
{
	struct xl_softc		*sc;
	uint32_t		val;

	sc = device_get_softc(dev);

	/* We're already in window 4. */
	val = CSR_READ_2(sc, XL_W4_PHY_MGMT);
	CSR_BARRIER(sc, XL_W4_PHY_MGMT, 2,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);

	return (val);
}

/*
 * Write the MII serial port for the MII bit-bang module.
 */
static void
xl_mii_bitbang_write(device_t dev, uint32_t val)
{
	struct xl_softc		*sc;

	sc = device_get_softc(dev);

	/* We're already in window 4. */
	CSR_WRITE_2(sc, XL_W4_PHY_MGMT,	val);
	CSR_BARRIER(sc, XL_W4_PHY_MGMT, 2,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
}

static int
xl_miibus_readreg(device_t dev, int phy, int reg)
{
	struct xl_softc		*sc;

	sc = device_get_softc(dev);

	/* Select the window 4. */
	XL_SEL_WIN(4);

	return (mii_bitbang_readreg(dev, &xl_mii_bitbang_ops, phy, reg));
}

static int
xl_miibus_writereg(device_t dev, int phy, int reg, int data)
{
	struct xl_softc		*sc;

	sc = device_get_softc(dev);

	/* Select the window 4. */
	XL_SEL_WIN(4);

	mii_bitbang_writereg(dev, &xl_mii_bitbang_ops, phy, reg, data);

	return (0);
}

static void
xl_miibus_statchg(device_t dev)
{
	struct xl_softc		*sc;
	struct mii_data		*mii;
	uint8_t			macctl;

	sc = device_get_softc(dev);
	mii = device_get_softc(sc->xl_miibus);

	xl_setcfg(sc);

	/* Set ASIC's duplex mode to match the PHY. */
	XL_SEL_WIN(3);
	macctl = CSR_READ_1(sc, XL_W3_MAC_CTRL);
	if ((IFM_OPTIONS(mii->mii_media_active) & IFM_FDX) != 0) {
		macctl |= XL_MACCTRL_DUPLEX;
		if (sc->xl_type == XL_TYPE_905B) {
			if ((IFM_OPTIONS(mii->mii_media_active) &
			    IFM_ETH_RXPAUSE) != 0)
				macctl |= XL_MACCTRL_FLOW_CONTROL_ENB;
			else
				macctl &= ~XL_MACCTRL_FLOW_CONTROL_ENB;
		}
	} else {
		macctl &= ~XL_MACCTRL_DUPLEX;
		if (sc->xl_type == XL_TYPE_905B)
			macctl &= ~XL_MACCTRL_FLOW_CONTROL_ENB;
	}
	CSR_WRITE_1(sc, XL_W3_MAC_CTRL, macctl);
}

/*
 * Special support for the 3c905B-COMBO. This card has 10/100 support
 * plus BNC and AUI ports. This means we will have both an miibus attached
 * plus some non-MII media settings. In order to allow this, we have to
 * add the extra media to the miibus's ifmedia struct, but we can't do
 * that during xl_attach() because the miibus hasn't been attached yet.
 * So instead, we wait until the miibus probe/attach is done, at which
 * point we will get a callback telling is that it's safe to add our
 * extra media.
 */
static void
xl_miibus_mediainit(device_t dev)
{
	struct xl_softc		*sc;
	struct mii_data		*mii;
	struct ifmedia		*ifm;

	sc = device_get_softc(dev);
	mii = device_get_softc(sc->xl_miibus);
	ifm = &mii->mii_media;

	if (sc->xl_media & (XL_MEDIAOPT_AUI | XL_MEDIAOPT_10FL)) {
		/*
		 * Check for a 10baseFL board in disguise.
		 */
		if (sc->xl_type == XL_TYPE_905B &&
		    sc->xl_media == XL_MEDIAOPT_10FL) {
			if (bootverbose)
				device_printf(sc->xl_dev, "found 10baseFL\n");
			ifmedia_add(ifm, IFM_ETHER | IFM_10_FL, 0, NULL);
			ifmedia_add(ifm, IFM_ETHER | IFM_10_FL|IFM_HDX, 0,
			    NULL);
			if (sc->xl_caps & XL_CAPS_FULL_DUPLEX)
				ifmedia_add(ifm,
				    IFM_ETHER | IFM_10_FL | IFM_FDX, 0, NULL);
		} else {
			if (bootverbose)
				device_printf(sc->xl_dev, "found AUI\n");
			ifmedia_add(ifm, IFM_ETHER | IFM_10_5, 0, NULL);
		}
	}

	if (sc->xl_media & XL_MEDIAOPT_BNC) {
		if (bootverbose)
			device_printf(sc->xl_dev, "found BNC\n");
		ifmedia_add(ifm, IFM_ETHER | IFM_10_2, 0, NULL);
	}
}

/*
 * The EEPROM is slow: give it time to come ready after issuing
 * it a command.
 */
static int
xl_eeprom_wait(struct xl_softc *sc)
{
	int			i;

	for (i = 0; i < 100; i++) {
		if (CSR_READ_2(sc, XL_W0_EE_CMD) & XL_EE_BUSY)
			DELAY(162);
		else
			break;
	}

	if (i == 100) {
		device_printf(sc->xl_dev, "eeprom failed to come ready\n");
		return (1);
	}

	return (0);
}

/*
 * Read a sequence of words from the EEPROM. Note that ethernet address
 * data is stored in the EEPROM in network byte order.
 */
static int
xl_read_eeprom(struct xl_softc *sc, caddr_t dest, int off, int cnt, int swap)
{
	int			err = 0, i;
	u_int16_t		word = 0, *ptr;

#define EEPROM_5BIT_OFFSET(A) ((((A) << 2) & 0x7F00) | ((A) & 0x003F))
#define EEPROM_8BIT_OFFSET(A) ((A) & 0x003F)
	/*
	 * XXX: WARNING! DANGER!
	 * It's easy to accidentally overwrite the rom content!
	 * Note: the 3c575 uses 8bit EEPROM offsets.
	 */
	XL_SEL_WIN(0);

	if (xl_eeprom_wait(sc))
		return (1);

	if (sc->xl_flags & XL_FLAG_EEPROM_OFFSET_30)
		off += 0x30;

	for (i = 0; i < cnt; i++) {
		if (sc->xl_flags & XL_FLAG_8BITROM)
			CSR_WRITE_2(sc, XL_W0_EE_CMD,
			    XL_EE_8BIT_READ | EEPROM_8BIT_OFFSET(off + i));
		else
			CSR_WRITE_2(sc, XL_W0_EE_CMD,
			    XL_EE_READ | EEPROM_5BIT_OFFSET(off + i));
		err = xl_eeprom_wait(sc);
		if (err)
			break;
		word = CSR_READ_2(sc, XL_W0_EE_DATA);
		ptr = (u_int16_t *)(dest + (i * 2));
		if (swap)
			*ptr = ntohs(word);
		else
			*ptr = word;
	}

	return (err ? 1 : 0);
}

static void
xl_rxfilter(struct xl_softc *sc)
{

	if (sc->xl_type == XL_TYPE_905B)
		xl_rxfilter_90xB(sc);
	else
		xl_rxfilter_90x(sc);
}

/*
 * NICs older than the 3c905B have only one multicast option, which
 * is to enable reception of all multicast frames.
 */
static void
xl_rxfilter_90x(struct xl_softc *sc)
{
	struct ifnet		*ifp;
	struct ifmultiaddr	*ifma;
	u_int8_t		rxfilt;

	XL_LOCK_ASSERT(sc);

	ifp = sc->xl_ifp;

	XL_SEL_WIN(5);
	rxfilt = CSR_READ_1(sc, XL_W5_RX_FILTER);
	rxfilt &= ~(XL_RXFILTER_ALLFRAMES | XL_RXFILTER_ALLMULTI |
	    XL_RXFILTER_BROADCAST | XL_RXFILTER_INDIVIDUAL);

	/* Set the individual bit to receive frames for this host only. */
	rxfilt |= XL_RXFILTER_INDIVIDUAL;
	/* Set capture broadcast bit to capture broadcast frames. */
	if (ifp->if_flags & IFF_BROADCAST)
		rxfilt |= XL_RXFILTER_BROADCAST;

	/* If we want promiscuous mode, set the allframes bit. */
	if (ifp->if_flags & (IFF_PROMISC | IFF_ALLMULTI)) {
		if (ifp->if_flags & IFF_PROMISC)
			rxfilt |= XL_RXFILTER_ALLFRAMES;
		if (ifp->if_flags & IFF_ALLMULTI)
			rxfilt |= XL_RXFILTER_ALLMULTI;
	} else {
		if_maddr_rlock(ifp);
		CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
			if (ifma->ifma_addr->sa_family != AF_LINK)
				continue;
			rxfilt |= XL_RXFILTER_ALLMULTI;
			break;
		}
		if_maddr_runlock(ifp);
	}

	CSR_WRITE_2(sc, XL_COMMAND, rxfilt | XL_CMD_RX_SET_FILT);
	XL_SEL_WIN(7);
}

/*
 * 3c905B adapters have a hash filter that we can program.
 */
static void
xl_rxfilter_90xB(struct xl_softc *sc)
{
	struct ifnet		*ifp;
	struct ifmultiaddr	*ifma;
	int			i, mcnt;
	u_int16_t		h;
	u_int8_t		rxfilt;

	XL_LOCK_ASSERT(sc);

	ifp = sc->xl_ifp;

	XL_SEL_WIN(5);
	rxfilt = CSR_READ_1(sc, XL_W5_RX_FILTER);
	rxfilt &= ~(XL_RXFILTER_ALLFRAMES | XL_RXFILTER_ALLMULTI |
	    XL_RXFILTER_BROADCAST | XL_RXFILTER_INDIVIDUAL |
	    XL_RXFILTER_MULTIHASH);

	/* Set the individual bit to receive frames for this host only. */
	rxfilt |= XL_RXFILTER_INDIVIDUAL;
	/* Set capture broadcast bit to capture broadcast frames. */
	if (ifp->if_flags & IFF_BROADCAST)
		rxfilt |= XL_RXFILTER_BROADCAST;

	/* If we want promiscuous mode, set the allframes bit. */
	if (ifp->if_flags & (IFF_PROMISC | IFF_ALLMULTI)) {
		if (ifp->if_flags & IFF_PROMISC)
			rxfilt |= XL_RXFILTER_ALLFRAMES;
		if (ifp->if_flags & IFF_ALLMULTI)
			rxfilt |= XL_RXFILTER_ALLMULTI;
	} else {
		/* First, zot all the existing hash bits. */
		for (i = 0; i < XL_HASHFILT_SIZE; i++)
			CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_RX_SET_HASH | i);

		/* Now program new ones. */
		mcnt = 0;
		if_maddr_rlock(ifp);
		CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
			if (ifma->ifma_addr->sa_family != AF_LINK)
				continue;
			/*
			 * Note: the 3c905B currently only supports a 64-bit
			 * hash table, which means we really only need 6 bits,
			 * but the manual indicates that future chip revisions
			 * will have a 256-bit hash table, hence the routine
			 * is set up to calculate 8 bits of position info in
			 * case we need it some day.
			 * Note II, The Sequel: _CURRENT_ versions of the
			 * 3c905B have a 256 bit hash table. This means we have
			 * to use all 8 bits regardless.  On older cards, the
			 * upper 2 bits will be ignored. Grrrr....
			 */
			h = ether_crc32_be(LLADDR((struct sockaddr_dl *)
			    ifma->ifma_addr), ETHER_ADDR_LEN) & 0xFF;
			CSR_WRITE_2(sc, XL_COMMAND,
			    h | XL_CMD_RX_SET_HASH | XL_HASH_SET);
			mcnt++;
		}
		if_maddr_runlock(ifp);
		if (mcnt > 0)
			rxfilt |= XL_RXFILTER_MULTIHASH;
	}

	CSR_WRITE_2(sc, XL_COMMAND, rxfilt | XL_CMD_RX_SET_FILT);
	XL_SEL_WIN(7);
}

static void
xl_setcfg(struct xl_softc *sc)
{
	u_int32_t		icfg;

	/*XL_LOCK_ASSERT(sc);*/

	XL_SEL_WIN(3);
	icfg = CSR_READ_4(sc, XL_W3_INTERNAL_CFG);
	icfg &= ~XL_ICFG_CONNECTOR_MASK;
	if (sc->xl_media & XL_MEDIAOPT_MII ||
		sc->xl_media & XL_MEDIAOPT_BT4)
		icfg |= (XL_XCVR_MII << XL_ICFG_CONNECTOR_BITS);
	if (sc->xl_media & XL_MEDIAOPT_BTX)
		icfg |= (XL_XCVR_AUTO << XL_ICFG_CONNECTOR_BITS);

	CSR_WRITE_4(sc, XL_W3_INTERNAL_CFG, icfg);
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_COAX_STOP);
}

static void
xl_setmode(struct xl_softc *sc, int media)
{
	u_int32_t		icfg;
	u_int16_t		mediastat;
	char			*pmsg = "", *dmsg = "";

	XL_LOCK_ASSERT(sc);

	XL_SEL_WIN(4);
	mediastat = CSR_READ_2(sc, XL_W4_MEDIA_STATUS);
	XL_SEL_WIN(3);
	icfg = CSR_READ_4(sc, XL_W3_INTERNAL_CFG);

	if (sc->xl_media & XL_MEDIAOPT_BT) {
		if (IFM_SUBTYPE(media) == IFM_10_T) {
			pmsg = "10baseT transceiver";
			sc->xl_xcvr = XL_XCVR_10BT;
			icfg &= ~XL_ICFG_CONNECTOR_MASK;
			icfg |= (XL_XCVR_10BT << XL_ICFG_CONNECTOR_BITS);
			mediastat |= XL_MEDIASTAT_LINKBEAT |
			    XL_MEDIASTAT_JABGUARD;
			mediastat &= ~XL_MEDIASTAT_SQEENB;
		}
	}

	if (sc->xl_media & XL_MEDIAOPT_BFX) {
		if (IFM_SUBTYPE(media) == IFM_100_FX) {
			pmsg = "100baseFX port";
			sc->xl_xcvr = XL_XCVR_100BFX;
			icfg &= ~XL_ICFG_CONNECTOR_MASK;
			icfg |= (XL_XCVR_100BFX << XL_ICFG_CONNECTOR_BITS);
			mediastat |= XL_MEDIASTAT_LINKBEAT;
			mediastat &= ~XL_MEDIASTAT_SQEENB;
		}
	}

	if (sc->xl_media & (XL_MEDIAOPT_AUI|XL_MEDIAOPT_10FL)) {
		if (IFM_SUBTYPE(media) == IFM_10_5) {
			pmsg = "AUI port";
			sc->xl_xcvr = XL_XCVR_AUI;
			icfg &= ~XL_ICFG_CONNECTOR_MASK;
			icfg |= (XL_XCVR_AUI << XL_ICFG_CONNECTOR_BITS);
			mediastat &= ~(XL_MEDIASTAT_LINKBEAT |
			    XL_MEDIASTAT_JABGUARD);
			mediastat |= ~XL_MEDIASTAT_SQEENB;
		}
		if (IFM_SUBTYPE(media) == IFM_10_FL) {
			pmsg = "10baseFL transceiver";
			sc->xl_xcvr = XL_XCVR_AUI;
			icfg &= ~XL_ICFG_CONNECTOR_MASK;
			icfg |= (XL_XCVR_AUI << XL_ICFG_CONNECTOR_BITS);
			mediastat &= ~(XL_MEDIASTAT_LINKBEAT |
			    XL_MEDIASTAT_JABGUARD);
			mediastat |= ~XL_MEDIASTAT_SQEENB;
		}
	}

	if (sc->xl_media & XL_MEDIAOPT_BNC) {
		if (IFM_SUBTYPE(media) == IFM_10_2) {
			pmsg = "AUI port";
			sc->xl_xcvr = XL_XCVR_COAX;
			icfg &= ~XL_ICFG_CONNECTOR_MASK;
			icfg |= (XL_XCVR_COAX << XL_ICFG_CONNECTOR_BITS);
			mediastat &= ~(XL_MEDIASTAT_LINKBEAT |
			    XL_MEDIASTAT_JABGUARD | XL_MEDIASTAT_SQEENB);
		}
	}

	if ((media & IFM_GMASK) == IFM_FDX ||
			IFM_SUBTYPE(media) == IFM_100_FX) {
		dmsg = "full";
		XL_SEL_WIN(3);
		CSR_WRITE_1(sc, XL_W3_MAC_CTRL, XL_MACCTRL_DUPLEX);
	} else {
		dmsg = "half";
		XL_SEL_WIN(3);
		CSR_WRITE_1(sc, XL_W3_MAC_CTRL,
			(CSR_READ_1(sc, XL_W3_MAC_CTRL) & ~XL_MACCTRL_DUPLEX));
	}

	if (IFM_SUBTYPE(media) == IFM_10_2)
		CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_COAX_START);
	else
		CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_COAX_STOP);

	CSR_WRITE_4(sc, XL_W3_INTERNAL_CFG, icfg);
	XL_SEL_WIN(4);
	CSR_WRITE_2(sc, XL_W4_MEDIA_STATUS, mediastat);

	DELAY(800);
	XL_SEL_WIN(7);

	device_printf(sc->xl_dev, "selecting %s, %s duplex\n", pmsg, dmsg);
}

static void
xl_reset(struct xl_softc *sc)
{
	int			i;

	XL_LOCK_ASSERT(sc);

	XL_SEL_WIN(0);
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_RESET |
	    ((sc->xl_flags & XL_FLAG_WEIRDRESET) ?
	     XL_RESETOPT_DISADVFD:0));

	/*
	 * If we're using memory mapped register mode, pause briefly
	 * after issuing the reset command before trying to access any
	 * other registers. With my 3c575C CardBus card, failing to do
	 * this results in the system locking up while trying to poll
	 * the command busy bit in the status register.
	 */
	if (sc->xl_flags & XL_FLAG_USE_MMIO)
		DELAY(100000);

	for (i = 0; i < XL_TIMEOUT; i++) {
		DELAY(10);
		if (!(CSR_READ_2(sc, XL_STATUS) & XL_STAT_CMDBUSY))
			break;
	}

	if (i == XL_TIMEOUT)
		device_printf(sc->xl_dev, "reset didn't complete\n");

	/* Reset TX and RX. */
	/* Note: the RX reset takes an absurd amount of time
	 * on newer versions of the Tornado chips such as those
	 * on the 3c905CX and newer 3c908C cards. We wait an
	 * extra amount of time so that xl_wait() doesn't complain
	 * and annoy the users.
	 */
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_RX_RESET);
	DELAY(100000);
	xl_wait(sc);
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_TX_RESET);
	xl_wait(sc);

	if (sc->xl_flags & XL_FLAG_INVERT_LED_PWR ||
	    sc->xl_flags & XL_FLAG_INVERT_MII_PWR) {
		XL_SEL_WIN(2);
		CSR_WRITE_2(sc, XL_W2_RESET_OPTIONS,
		    CSR_READ_2(sc, XL_W2_RESET_OPTIONS) |
		    ((sc->xl_flags & XL_FLAG_INVERT_LED_PWR) ?
		    XL_RESETOPT_INVERT_LED : 0) |
		    ((sc->xl_flags & XL_FLAG_INVERT_MII_PWR) ?
		    XL_RESETOPT_INVERT_MII : 0));
	}

	/* Wait a little while for the chip to get its brains in order. */
	DELAY(100000);
}

/*
 * Probe for a 3Com Etherlink XL chip. Check the PCI vendor and device
 * IDs against our list and return a device name if we find a match.
 */
static int
xl_probe(device_t dev)
{
	const struct xl_type	*t;

	t = xl_devs;

	while (t->xl_name != NULL) {
		if ((pci_get_vendor(dev) == t->xl_vid) &&
		    (pci_get_device(dev) == t->xl_did)) {
			device_set_desc(dev, t->xl_name);
			return (BUS_PROBE_DEFAULT);
		}
		t++;
	}

	return (ENXIO);
}

/*
 * This routine is a kludge to work around possible hardware faults
 * or manufacturing defects that can cause the media options register
 * (or reset options register, as it's called for the first generation
 * 3c90x adapters) to return an incorrect result. I have encountered
 * one Dell Latitude laptop docking station with an integrated 3c905-TX
 * which doesn't have any of the 'mediaopt' bits set. This screws up
 * the attach routine pretty badly because it doesn't know what media
 * to look for. If we find ourselves in this predicament, this routine
 * will try to guess the media options values and warn the user of a
 * possible manufacturing defect with his adapter/system/whatever.
 */
static void
xl_mediacheck(struct xl_softc *sc)
{

	/*
	 * If some of the media options bits are set, assume they are
	 * correct. If not, try to figure it out down below.
	 * XXX I should check for 10baseFL, but I don't have an adapter
	 * to test with.
	 */
	if (sc->xl_media & (XL_MEDIAOPT_MASK & ~XL_MEDIAOPT_VCO)) {
		/*
		 * Check the XCVR value. If it's not in the normal range
		 * of values, we need to fake it up here.
		 */
		if (sc->xl_xcvr <= XL_XCVR_AUTO)
			return;
		else {
			device_printf(sc->xl_dev,
			    "bogus xcvr value in EEPROM (%x)\n", sc->xl_xcvr);
			device_printf(sc->xl_dev,
			    "choosing new default based on card type\n");
		}
	} else {
		if (sc->xl_type == XL_TYPE_905B &&
		    sc->xl_media & XL_MEDIAOPT_10FL)
			return;
		device_printf(sc->xl_dev,
"WARNING: no media options bits set in the media options register!!\n");
		device_printf(sc->xl_dev,
"this could be a manufacturing defect in your adapter or system\n");
		device_printf(sc->xl_dev,
"attempting to guess media type; you should probably consult your vendor\n");
	}

	xl_choose_xcvr(sc, 1);
}

static void
xl_choose_xcvr(struct xl_softc *sc, int verbose)
{
	u_int16_t		devid;

	/*
	 * Read the device ID from the EEPROM.
	 * This is what's loaded into the PCI device ID register, so it has
	 * to be correct otherwise we wouldn't have gotten this far.
	 */
	xl_read_eeprom(sc, (caddr_t)&devid, XL_EE_PRODID, 1, 0);

	switch (devid) {
	case TC_DEVICEID_BOOMERANG_10BT:	/* 3c900-TPO */
	case TC_DEVICEID_KRAKATOA_10BT:		/* 3c900B-TPO */
		sc->xl_media = XL_MEDIAOPT_BT;
		sc->xl_xcvr = XL_XCVR_10BT;
		if (verbose)
			device_printf(sc->xl_dev,
			    "guessing 10BaseT transceiver\n");
		break;
	case TC_DEVICEID_BOOMERANG_10BT_COMBO:	/* 3c900-COMBO */
	case TC_DEVICEID_KRAKATOA_10BT_COMBO:	/* 3c900B-COMBO */
		sc->xl_media = XL_MEDIAOPT_BT|XL_MEDIAOPT_BNC|XL_MEDIAOPT_AUI;
		sc->xl_xcvr = XL_XCVR_10BT;
		if (verbose)
			device_printf(sc->xl_dev,
			    "guessing COMBO (AUI/BNC/TP)\n");
		break;
	case TC_DEVICEID_KRAKATOA_10BT_TPC:	/* 3c900B-TPC */
		sc->xl_media = XL_MEDIAOPT_BT|XL_MEDIAOPT_BNC;
		sc->xl_xcvr = XL_XCVR_10BT;
		if (verbose)
			device_printf(sc->xl_dev, "guessing TPC (BNC/TP)\n");
		break;
	case TC_DEVICEID_CYCLONE_10FL:		/* 3c900B-FL */
		sc->xl_media = XL_MEDIAOPT_10FL;
		sc->xl_xcvr = XL_XCVR_AUI;
		if (verbose)
			device_printf(sc->xl_dev, "guessing 10baseFL\n");
		break;
	case TC_DEVICEID_BOOMERANG_10_100BT:	/* 3c905-TX */
	case TC_DEVICEID_HURRICANE_555:		/* 3c555 */
	case TC_DEVICEID_HURRICANE_556:		/* 3c556 */
	case TC_DEVICEID_HURRICANE_556B:	/* 3c556B */
	case TC_DEVICEID_HURRICANE_575A:	/* 3c575TX */
	case TC_DEVICEID_HURRICANE_575B:	/* 3c575B */
	case TC_DEVICEID_HURRICANE_575C:	/* 3c575C */
	case TC_DEVICEID_HURRICANE_656:		/* 3c656 */
	case TC_DEVICEID_HURRICANE_656B:	/* 3c656B */
	case TC_DEVICEID_TORNADO_656C:		/* 3c656C */
	case TC_DEVICEID_TORNADO_10_100BT_920B:	/* 3c920B-EMB */
	case TC_DEVICEID_TORNADO_10_100BT_920B_WNM:	/* 3c920B-EMB-WNM */
		sc->xl_media = XL_MEDIAOPT_MII;
		sc->xl_xcvr = XL_XCVR_MII;
		if (verbose)
			device_printf(sc->xl_dev, "guessing MII\n");
		break;
	case TC_DEVICEID_BOOMERANG_100BT4:	/* 3c905-T4 */
	case TC_DEVICEID_CYCLONE_10_100BT4:	/* 3c905B-T4 */
		sc->xl_media = XL_MEDIAOPT_BT4;
		sc->xl_xcvr = XL_XCVR_MII;
		if (verbose)
			device_printf(sc->xl_dev, "guessing 100baseT4/MII\n");
		break;
	case TC_DEVICEID_HURRICANE_10_100BT:	/* 3c905B-TX */
	case TC_DEVICEID_HURRICANE_10_100BT_SERV:/*3c980-TX */
	case TC_DEVICEID_TORNADO_10_100BT_SERV:	/* 3c980C-TX */
	case TC_DEVICEID_HURRICANE_SOHO100TX:	/* 3cSOHO100-TX */
	case TC_DEVICEID_TORNADO_10_100BT:	/* 3c905C-TX */
	case TC_DEVICEID_TORNADO_HOMECONNECT:	/* 3c450-TX */
		sc->xl_media = XL_MEDIAOPT_BTX;
		sc->xl_xcvr = XL_XCVR_AUTO;
		if (verbose)
			device_printf(sc->xl_dev, "guessing 10/100 internal\n");
		break;
	case TC_DEVICEID_CYCLONE_10_100_COMBO:	/* 3c905B-COMBO */
		sc->xl_media = XL_MEDIAOPT_BTX|XL_MEDIAOPT_BNC|XL_MEDIAOPT_AUI;
		sc->xl_xcvr = XL_XCVR_AUTO;
		if (verbose)
			device_printf(sc->xl_dev,
			    "guessing 10/100 plus BNC/AUI\n");
		break;
	default:
		device_printf(sc->xl_dev,
		    "unknown device ID: %x -- defaulting to 10baseT\n", devid);
		sc->xl_media = XL_MEDIAOPT_BT;
		break;
	}
}

/*
 * Attach the interface. Allocate softc structures, do ifmedia
 * setup and ethernet/BPF attach.
 */
static int
xl_attach(device_t dev)
{
	u_char			eaddr[ETHER_ADDR_LEN];
	u_int16_t		sinfo2, xcvr[2];
	struct xl_softc		*sc;
	struct ifnet		*ifp;
	int			media, pmcap;
	int			error = 0, phy, rid, res, unit;
	uint16_t		did;

	sc = device_get_softc(dev);
	sc->xl_dev = dev;

	unit = device_get_unit(dev);

	mtx_init(&sc->xl_mtx, device_get_nameunit(dev), MTX_NETWORK_LOCK,
	    MTX_DEF);
	ifmedia_init(&sc->ifmedia, 0, xl_ifmedia_upd, xl_ifmedia_sts);

	did = pci_get_device(dev);

	sc->xl_flags = 0;
	if (did == TC_DEVICEID_HURRICANE_555)
		sc->xl_flags |= XL_FLAG_EEPROM_OFFSET_30 | XL_FLAG_PHYOK;
	if (did == TC_DEVICEID_HURRICANE_556 ||
	    did == TC_DEVICEID_HURRICANE_556B)
		sc->xl_flags |= XL_FLAG_FUNCREG | XL_FLAG_PHYOK |
		    XL_FLAG_EEPROM_OFFSET_30 | XL_FLAG_WEIRDRESET |
		    XL_FLAG_INVERT_LED_PWR | XL_FLAG_INVERT_MII_PWR;
	if (did == TC_DEVICEID_HURRICANE_555 ||
	    did == TC_DEVICEID_HURRICANE_556)
		sc->xl_flags |= XL_FLAG_8BITROM;
	if (did == TC_DEVICEID_HURRICANE_556B)
		sc->xl_flags |= XL_FLAG_NO_XCVR_PWR;

	if (did == TC_DEVICEID_HURRICANE_575B ||
	    did == TC_DEVICEID_HURRICANE_575C ||
	    did == TC_DEVICEID_HURRICANE_656B ||
	    did == TC_DEVICEID_TORNADO_656C)
		sc->xl_flags |= XL_FLAG_FUNCREG;
	if (did == TC_DEVICEID_HURRICANE_575A ||
	    did == TC_DEVICEID_HURRICANE_575B ||
	    did == TC_DEVICEID_HURRICANE_575C ||
	    did == TC_DEVICEID_HURRICANE_656B ||
	    did == TC_DEVICEID_TORNADO_656C)
		sc->xl_flags |= XL_FLAG_PHYOK | XL_FLAG_EEPROM_OFFSET_30 |
		  XL_FLAG_8BITROM;
	if (did == TC_DEVICEID_HURRICANE_656)
		sc->xl_flags |= XL_FLAG_FUNCREG | XL_FLAG_PHYOK;
	if (did == TC_DEVICEID_HURRICANE_575B)
		sc->xl_flags |= XL_FLAG_INVERT_LED_PWR;
	if (did == TC_DEVICEID_HURRICANE_575C)
		sc->xl_flags |= XL_FLAG_INVERT_MII_PWR;
	if (did == TC_DEVICEID_TORNADO_656C)
		sc->xl_flags |= XL_FLAG_INVERT_MII_PWR;
	if (did == TC_DEVICEID_HURRICANE_656 ||
	    did == TC_DEVICEID_HURRICANE_656B)
		sc->xl_flags |= XL_FLAG_INVERT_MII_PWR |
		    XL_FLAG_INVERT_LED_PWR;
	if (did == TC_DEVICEID_TORNADO_10_100BT_920B ||
	    did == TC_DEVICEID_TORNADO_10_100BT_920B_WNM)
		sc->xl_flags |= XL_FLAG_PHYOK;

	switch (did) {
	case TC_DEVICEID_BOOMERANG_10_100BT:	/* 3c905-TX */
	case TC_DEVICEID_HURRICANE_575A:
	case TC_DEVICEID_HURRICANE_575B:
	case TC_DEVICEID_HURRICANE_575C:
		sc->xl_flags |= XL_FLAG_NO_MMIO;
		break;
	default:
		break;
	}

	/*
	 * Map control/status registers.
	 */
	pci_enable_busmaster(dev);

	if ((sc->xl_flags & XL_FLAG_NO_MMIO) == 0) {
		rid = XL_PCI_LOMEM;
		res = SYS_RES_MEMORY;

		sc->xl_res = bus_alloc_resource_any(dev, res, &rid, RF_ACTIVE);
	}

	if (sc->xl_res != NULL) {
		sc->xl_flags |= XL_FLAG_USE_MMIO;
		if (bootverbose)
			device_printf(dev, "using memory mapped I/O\n");
	} else {
		rid = XL_PCI_LOIO;
		res = SYS_RES_IOPORT;
		sc->xl_res = bus_alloc_resource_any(dev, res, &rid, RF_ACTIVE);
		if (sc->xl_res == NULL) {
			device_printf(dev, "couldn't map ports/memory\n");
			error = ENXIO;
			goto fail;
		}
		if (bootverbose)
			device_printf(dev, "using port I/O\n");
	}

	sc->xl_btag = rman_get_bustag(sc->xl_res);
	sc->xl_bhandle = rman_get_bushandle(sc->xl_res);

	if (sc->xl_flags & XL_FLAG_FUNCREG) {
		rid = XL_PCI_FUNCMEM;
		sc->xl_fres = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
		    RF_ACTIVE);

		if (sc->xl_fres == NULL) {
			device_printf(dev, "couldn't map funcreg memory\n");
			error = ENXIO;
			goto fail;
		}

		sc->xl_ftag = rman_get_bustag(sc->xl_fres);
		sc->xl_fhandle = rman_get_bushandle(sc->xl_fres);
	}

	/* Allocate interrupt */
	rid = 0;
	sc->xl_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_SHAREABLE | RF_ACTIVE);
	if (sc->xl_irq == NULL) {
		device_printf(dev, "couldn't map interrupt\n");
		error = ENXIO;
		goto fail;
	}

	/* Initialize interface name. */
	ifp = sc->xl_ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(dev, "can not if_alloc()\n");
		error = ENOSPC;
		goto fail;
	}
	ifp->if_softc = sc;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));

	/* Reset the adapter. */
	XL_LOCK(sc);
	xl_reset(sc);
	XL_UNLOCK(sc);

	/*
	 * Get station address from the EEPROM.
	 */
	if (xl_read_eeprom(sc, (caddr_t)&eaddr, XL_EE_OEM_ADR0, 3, 1)) {
		device_printf(dev, "failed to read station address\n");
		error = ENXIO;
		goto fail;
	}

	callout_init_mtx(&sc->xl_tick_callout, &sc->xl_mtx, 0);
	TASK_INIT(&sc->xl_task, 0, xl_rxeof_task, sc);

	/*
	 * Now allocate a tag for the DMA descriptor lists and a chunk
	 * of DMA-able memory based on the tag.  Also obtain the DMA
	 * addresses of the RX and TX ring, which we'll need later.
	 * All of our lists are allocated as a contiguous block
	 * of memory.
	 */
	error = bus_dma_tag_create(bus_get_dma_tag(dev), 8, 0,
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL,
	    XL_RX_LIST_SZ, 1, XL_RX_LIST_SZ, 0, NULL, NULL,
	    &sc->xl_ldata.xl_rx_tag);
	if (error) {
		device_printf(dev, "failed to allocate rx dma tag\n");
		goto fail;
	}

	error = bus_dmamem_alloc(sc->xl_ldata.xl_rx_tag,
	    (void **)&sc->xl_ldata.xl_rx_list, BUS_DMA_NOWAIT |
	    BUS_DMA_COHERENT | BUS_DMA_ZERO, &sc->xl_ldata.xl_rx_dmamap);
	if (error) {
		device_printf(dev, "no memory for rx list buffers!\n");
		bus_dma_tag_destroy(sc->xl_ldata.xl_rx_tag);
		sc->xl_ldata.xl_rx_tag = NULL;
		goto fail;
	}

	error = bus_dmamap_load(sc->xl_ldata.xl_rx_tag,
	    sc->xl_ldata.xl_rx_dmamap, sc->xl_ldata.xl_rx_list,
	    XL_RX_LIST_SZ, xl_dma_map_addr,
	    &sc->xl_ldata.xl_rx_dmaaddr, BUS_DMA_NOWAIT);
	if (error) {
		device_printf(dev, "cannot get dma address of the rx ring!\n");
		bus_dmamem_free(sc->xl_ldata.xl_rx_tag, sc->xl_ldata.xl_rx_list,
		    sc->xl_ldata.xl_rx_dmamap);
		bus_dma_tag_destroy(sc->xl_ldata.xl_rx_tag);
		sc->xl_ldata.xl_rx_tag = NULL;
		goto fail;
	}

	error = bus_dma_tag_create(bus_get_dma_tag(dev), 8, 0,
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL,
	    XL_TX_LIST_SZ, 1, XL_TX_LIST_SZ, 0, NULL, NULL,
	    &sc->xl_ldata.xl_tx_tag);
	if (error) {
		device_printf(dev, "failed to allocate tx dma tag\n");
		goto fail;
	}

	error = bus_dmamem_alloc(sc->xl_ldata.xl_tx_tag,
	    (void **)&sc->xl_ldata.xl_tx_list, BUS_DMA_NOWAIT |
	    BUS_DMA_COHERENT | BUS_DMA_ZERO, &sc->xl_ldata.xl_tx_dmamap);
	if (error) {
		device_printf(dev, "no memory for list buffers!\n");
		bus_dma_tag_destroy(sc->xl_ldata.xl_tx_tag);
		sc->xl_ldata.xl_tx_tag = NULL;
		goto fail;
	}

	error = bus_dmamap_load(sc->xl_ldata.xl_tx_tag,
	    sc->xl_ldata.xl_tx_dmamap, sc->xl_ldata.xl_tx_list,
	    XL_TX_LIST_SZ, xl_dma_map_addr,
	    &sc->xl_ldata.xl_tx_dmaaddr, BUS_DMA_NOWAIT);
	if (error) {
		device_printf(dev, "cannot get dma address of the tx ring!\n");
		bus_dmamem_free(sc->xl_ldata.xl_tx_tag, sc->xl_ldata.xl_tx_list,
		    sc->xl_ldata.xl_tx_dmamap);
		bus_dma_tag_destroy(sc->xl_ldata.xl_tx_tag);
		sc->xl_ldata.xl_tx_tag = NULL;
		goto fail;
	}

	/*
	 * Allocate a DMA tag for the mapping of mbufs.
	 */
	error = bus_dma_tag_create(bus_get_dma_tag(dev), 1, 0,
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL,
	    MCLBYTES * XL_MAXFRAGS, XL_MAXFRAGS, MCLBYTES, 0, NULL,
	    NULL, &sc->xl_mtag);
	if (error) {
		device_printf(dev, "failed to allocate mbuf dma tag\n");
		goto fail;
	}

	/* We need a spare DMA map for the RX ring. */
	error = bus_dmamap_create(sc->xl_mtag, 0, &sc->xl_tmpmap);
	if (error)
		goto fail;

	/*
	 * Figure out the card type. 3c905B adapters have the
	 * 'supportsNoTxLength' bit set in the capabilities
	 * word in the EEPROM.
	 * Note: my 3c575C CardBus card lies. It returns a value
	 * of 0x1578 for its capabilities word, which is somewhat
	 * nonsensical. Another way to distinguish a 3c90x chip
	 * from a 3c90xB/C chip is to check for the 'supportsLargePackets'
	 * bit. This will only be set for 3c90x boomerage chips.
	 */
	xl_read_eeprom(sc, (caddr_t)&sc->xl_caps, XL_EE_CAPS, 1, 0);
	if (sc->xl_caps & XL_CAPS_NO_TXLENGTH ||
	    !(sc->xl_caps & XL_CAPS_LARGE_PKTS))
		sc->xl_type = XL_TYPE_905B;
	else
		sc->xl_type = XL_TYPE_90X;

	/* Check availability of WOL. */
	if ((sc->xl_caps & XL_CAPS_PWRMGMT) != 0 &&
	    pci_find_cap(dev, PCIY_PMG, &pmcap) == 0) {
		sc->xl_pmcap = pmcap;
		sc->xl_flags |= XL_FLAG_WOL;
		sinfo2 = 0;
		xl_read_eeprom(sc, (caddr_t)&sinfo2, XL_EE_SOFTINFO2, 1, 0);
		if ((sinfo2 & XL_SINFO2_AUX_WOL_CON) == 0 && bootverbose)
			device_printf(dev,
			    "No auxiliary remote wakeup connector!\n");
	}

	/* Set the TX start threshold for best performance. */
	sc->xl_tx_thresh = XL_MIN_FRAMELEN;

	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = xl_ioctl;
	ifp->if_capabilities = IFCAP_VLAN_MTU;
	if (sc->xl_type == XL_TYPE_905B) {
		ifp->if_hwassist = XL905B_CSUM_FEATURES;
#ifdef XL905B_TXCSUM_BROKEN
		ifp->if_capabilities |= IFCAP_RXCSUM;
#else
		ifp->if_capabilities |= IFCAP_HWCSUM;
#endif
	}
	if ((sc->xl_flags & XL_FLAG_WOL) != 0)
		ifp->if_capabilities |= IFCAP_WOL_MAGIC;
	ifp->if_capenable = ifp->if_capabilities;
#ifdef DEVICE_POLLING
	ifp->if_capabilities |= IFCAP_POLLING;
#endif
	ifp->if_start = xl_start;
	ifp->if_init = xl_init;
	IFQ_SET_MAXLEN(&ifp->if_snd, XL_TX_LIST_CNT - 1);
	ifp->if_snd.ifq_drv_maxlen = XL_TX_LIST_CNT - 1;
	IFQ_SET_READY(&ifp->if_snd);

	/*
	 * Now we have to see what sort of media we have.
	 * This includes probing for an MII interace and a
	 * possible PHY.
	 */
	XL_SEL_WIN(3);
	sc->xl_media = CSR_READ_2(sc, XL_W3_MEDIA_OPT);
	if (bootverbose)
		device_printf(dev, "media options word: %x\n", sc->xl_media);

	xl_read_eeprom(sc, (char *)&xcvr, XL_EE_ICFG_0, 2, 0);
	sc->xl_xcvr = xcvr[0] | xcvr[1] << 16;
	sc->xl_xcvr &= XL_ICFG_CONNECTOR_MASK;
	sc->xl_xcvr >>= XL_ICFG_CONNECTOR_BITS;

	xl_mediacheck(sc);

	if (sc->xl_media & XL_MEDIAOPT_MII ||
	    sc->xl_media & XL_MEDIAOPT_BTX ||
	    sc->xl_media & XL_MEDIAOPT_BT4) {
		if (bootverbose)
			device_printf(dev, "found MII/AUTO\n");
		xl_setcfg(sc);
		/*
		 * Attach PHYs only at MII address 24 if !XL_FLAG_PHYOK.
		 * This is to guard against problems with certain 3Com ASIC
		 * revisions that incorrectly map the internal transceiver
		 * control registers at all MII addresses.
		 */
		phy = MII_PHY_ANY;
		if ((sc->xl_flags & XL_FLAG_PHYOK) == 0)
			phy = 24;
		error = mii_attach(dev, &sc->xl_miibus, ifp, xl_ifmedia_upd,
		    xl_ifmedia_sts, BMSR_DEFCAPMASK, phy, MII_OFFSET_ANY,
		    sc->xl_type == XL_TYPE_905B ? MIIF_DOPAUSE : 0);
		if (error != 0) {
			device_printf(dev, "attaching PHYs failed\n");
			goto fail;
		}
		goto done;
	}

	/*
	 * Sanity check. If the user has selected "auto" and this isn't
	 * a 10/100 card of some kind, we need to force the transceiver
	 * type to something sane.
	 */
	if (sc->xl_xcvr == XL_XCVR_AUTO)
		xl_choose_xcvr(sc, bootverbose);

	/*
	 * Do ifmedia setup.
	 */
	if (sc->xl_media & XL_MEDIAOPT_BT) {
		if (bootverbose)
			device_printf(dev, "found 10baseT\n");
		ifmedia_add(&sc->ifmedia, IFM_ETHER|IFM_10_T, 0, NULL);
		ifmedia_add(&sc->ifmedia, IFM_ETHER|IFM_10_T|IFM_HDX, 0, NULL);
		if (sc->xl_caps & XL_CAPS_FULL_DUPLEX)
			ifmedia_add(&sc->ifmedia,
			    IFM_ETHER|IFM_10_T|IFM_FDX, 0, NULL);
	}

	if (sc->xl_media & (XL_MEDIAOPT_AUI|XL_MEDIAOPT_10FL)) {
		/*
		 * Check for a 10baseFL board in disguise.
		 */
		if (sc->xl_type == XL_TYPE_905B &&
		    sc->xl_media == XL_MEDIAOPT_10FL) {
			if (bootverbose)
				device_printf(dev, "found 10baseFL\n");
			ifmedia_add(&sc->ifmedia, IFM_ETHER|IFM_10_FL, 0, NULL);
			ifmedia_add(&sc->ifmedia, IFM_ETHER|IFM_10_FL|IFM_HDX,
			    0, NULL);
			if (sc->xl_caps & XL_CAPS_FULL_DUPLEX)
				ifmedia_add(&sc->ifmedia,
				    IFM_ETHER|IFM_10_FL|IFM_FDX, 0, NULL);
		} else {
			if (bootverbose)
				device_printf(dev, "found AUI\n");
			ifmedia_add(&sc->ifmedia, IFM_ETHER|IFM_10_5, 0, NULL);
		}
	}

	if (sc->xl_media & XL_MEDIAOPT_BNC) {
		if (bootverbose)
			device_printf(dev, "found BNC\n");
		ifmedia_add(&sc->ifmedia, IFM_ETHER|IFM_10_2, 0, NULL);
	}

	if (sc->xl_media & XL_MEDIAOPT_BFX) {
		if (bootverbose)
			device_printf(dev, "found 100baseFX\n");
		ifmedia_add(&sc->ifmedia, IFM_ETHER|IFM_100_FX, 0, NULL);
	}

	media = IFM_ETHER|IFM_100_TX|IFM_FDX;
	xl_choose_media(sc, &media);

	if (sc->xl_miibus == NULL)
		ifmedia_set(&sc->ifmedia, media);

done:
	if (sc->xl_flags & XL_FLAG_NO_XCVR_PWR) {
		XL_SEL_WIN(0);
		CSR_WRITE_2(sc, XL_W0_MFG_ID, XL_NO_XCVR_PWR_MAGICBITS);
	}

	/*
	 * Call MI attach routine.
	 */
	ether_ifattach(ifp, eaddr);

	error = bus_setup_intr(dev, sc->xl_irq, INTR_TYPE_NET | INTR_MPSAFE,
	    NULL, xl_intr, sc, &sc->xl_intrhand);
	if (error) {
		device_printf(dev, "couldn't set up irq\n");
		ether_ifdetach(ifp);
		goto fail;
	}

fail:
	if (error)
		xl_detach(dev);

	return (error);
}

/*
 * Choose a default media.
 * XXX This is a leaf function only called by xl_attach() and
 *     acquires/releases the non-recursible driver mutex to
 *     satisfy lock assertions.
 */
static void
xl_choose_media(struct xl_softc *sc, int *media)
{

	XL_LOCK(sc);

	switch (sc->xl_xcvr) {
	case XL_XCVR_10BT:
		*media = IFM_ETHER|IFM_10_T;
		xl_setmode(sc, *media);
		break;
	case XL_XCVR_AUI:
		if (sc->xl_type == XL_TYPE_905B &&
		    sc->xl_media == XL_MEDIAOPT_10FL) {
			*media = IFM_ETHER|IFM_10_FL;
			xl_setmode(sc, *media);
		} else {
			*media = IFM_ETHER|IFM_10_5;
			xl_setmode(sc, *media);
		}
		break;
	case XL_XCVR_COAX:
		*media = IFM_ETHER|IFM_10_2;
		xl_setmode(sc, *media);
		break;
	case XL_XCVR_AUTO:
	case XL_XCVR_100BTX:
	case XL_XCVR_MII:
		/* Chosen by miibus */
		break;
	case XL_XCVR_100BFX:
		*media = IFM_ETHER|IFM_100_FX;
		break;
	default:
		device_printf(sc->xl_dev, "unknown XCVR type: %d\n",
		    sc->xl_xcvr);
		/*
		 * This will probably be wrong, but it prevents
		 * the ifmedia code from panicking.
		 */
		*media = IFM_ETHER|IFM_10_T;
		break;
	}

	XL_UNLOCK(sc);
}

/*
 * Shutdown hardware and free up resources. This can be called any
 * time after the mutex has been initialized. It is called in both
 * the error case in attach and the normal detach case so it needs
 * to be careful about only freeing resources that have actually been
 * allocated.
 */
static int
xl_detach(device_t dev)
{
	struct xl_softc		*sc;
	struct ifnet		*ifp;
	int			rid, res;

	sc = device_get_softc(dev);
	ifp = sc->xl_ifp;

	KASSERT(mtx_initialized(&sc->xl_mtx), ("xl mutex not initialized"));

#ifdef DEVICE_POLLING
	if (ifp && ifp->if_capenable & IFCAP_POLLING)
		ether_poll_deregister(ifp);
#endif

	if (sc->xl_flags & XL_FLAG_USE_MMIO) {
		rid = XL_PCI_LOMEM;
		res = SYS_RES_MEMORY;
	} else {
		rid = XL_PCI_LOIO;
		res = SYS_RES_IOPORT;
	}

	/* These should only be active if attach succeeded */
	if (device_is_attached(dev)) {
		XL_LOCK(sc);
		xl_stop(sc);
		XL_UNLOCK(sc);
		taskqueue_drain(taskqueue_swi, &sc->xl_task);
		callout_drain(&sc->xl_tick_callout);
		ether_ifdetach(ifp);
	}
	if (sc->xl_miibus)
		device_delete_child(dev, sc->xl_miibus);
	bus_generic_detach(dev);
	ifmedia_removeall(&sc->ifmedia);

	if (sc->xl_intrhand)
		bus_teardown_intr(dev, sc->xl_irq, sc->xl_intrhand);
	if (sc->xl_irq)
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->xl_irq);
	if (sc->xl_fres != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY,
		    XL_PCI_FUNCMEM, sc->xl_fres);
	if (sc->xl_res)
		bus_release_resource(dev, res, rid, sc->xl_res);

	if (ifp)
		if_free(ifp);

	if (sc->xl_mtag) {
		bus_dmamap_destroy(sc->xl_mtag, sc->xl_tmpmap);
		bus_dma_tag_destroy(sc->xl_mtag);
	}
	if (sc->xl_ldata.xl_rx_tag) {
		bus_dmamap_unload(sc->xl_ldata.xl_rx_tag,
		    sc->xl_ldata.xl_rx_dmamap);
		bus_dmamem_free(sc->xl_ldata.xl_rx_tag, sc->xl_ldata.xl_rx_list,
		    sc->xl_ldata.xl_rx_dmamap);
		bus_dma_tag_destroy(sc->xl_ldata.xl_rx_tag);
	}
	if (sc->xl_ldata.xl_tx_tag) {
		bus_dmamap_unload(sc->xl_ldata.xl_tx_tag,
		    sc->xl_ldata.xl_tx_dmamap);
		bus_dmamem_free(sc->xl_ldata.xl_tx_tag, sc->xl_ldata.xl_tx_list,
		    sc->xl_ldata.xl_tx_dmamap);
		bus_dma_tag_destroy(sc->xl_ldata.xl_tx_tag);
	}

	mtx_destroy(&sc->xl_mtx);

	return (0);
}

/*
 * Initialize the transmit descriptors.
 */
static int
xl_list_tx_init(struct xl_softc *sc)
{
	struct xl_chain_data	*cd;
	struct xl_list_data	*ld;
	int			error, i;

	XL_LOCK_ASSERT(sc);

	cd = &sc->xl_cdata;
	ld = &sc->xl_ldata;
	for (i = 0; i < XL_TX_LIST_CNT; i++) {
		cd->xl_tx_chain[i].xl_ptr = &ld->xl_tx_list[i];
		error = bus_dmamap_create(sc->xl_mtag, 0,
		    &cd->xl_tx_chain[i].xl_map);
		if (error)
			return (error);
		cd->xl_tx_chain[i].xl_phys = ld->xl_tx_dmaaddr +
		    i * sizeof(struct xl_list);
		if (i == (XL_TX_LIST_CNT - 1))
			cd->xl_tx_chain[i].xl_next = NULL;
		else
			cd->xl_tx_chain[i].xl_next = &cd->xl_tx_chain[i + 1];
	}

	cd->xl_tx_free = &cd->xl_tx_chain[0];
	cd->xl_tx_tail = cd->xl_tx_head = NULL;

	bus_dmamap_sync(ld->xl_tx_tag, ld->xl_tx_dmamap, BUS_DMASYNC_PREWRITE);
	return (0);
}

/*
 * Initialize the transmit descriptors.
 */
static int
xl_list_tx_init_90xB(struct xl_softc *sc)
{
	struct xl_chain_data	*cd;
	struct xl_list_data	*ld;
	int			error, i;

	XL_LOCK_ASSERT(sc);

	cd = &sc->xl_cdata;
	ld = &sc->xl_ldata;
	for (i = 0; i < XL_TX_LIST_CNT; i++) {
		cd->xl_tx_chain[i].xl_ptr = &ld->xl_tx_list[i];
		error = bus_dmamap_create(sc->xl_mtag, 0,
		    &cd->xl_tx_chain[i].xl_map);
		if (error)
			return (error);
		cd->xl_tx_chain[i].xl_phys = ld->xl_tx_dmaaddr +
		    i * sizeof(struct xl_list);
		if (i == (XL_TX_LIST_CNT - 1))
			cd->xl_tx_chain[i].xl_next = &cd->xl_tx_chain[0];
		else
			cd->xl_tx_chain[i].xl_next = &cd->xl_tx_chain[i + 1];
		if (i == 0)
			cd->xl_tx_chain[i].xl_prev =
			    &cd->xl_tx_chain[XL_TX_LIST_CNT - 1];
		else
			cd->xl_tx_chain[i].xl_prev =
			    &cd->xl_tx_chain[i - 1];
	}

	bzero(ld->xl_tx_list, XL_TX_LIST_SZ);
	ld->xl_tx_list[0].xl_status = htole32(XL_TXSTAT_EMPTY);

	cd->xl_tx_prod = 1;
	cd->xl_tx_cons = 1;
	cd->xl_tx_cnt = 0;

	bus_dmamap_sync(ld->xl_tx_tag, ld->xl_tx_dmamap, BUS_DMASYNC_PREWRITE);
	return (0);
}

/*
 * Initialize the RX descriptors and allocate mbufs for them. Note that
 * we arrange the descriptors in a closed ring, so that the last descriptor
 * points back to the first.
 */
static int
xl_list_rx_init(struct xl_softc *sc)
{
	struct xl_chain_data	*cd;
	struct xl_list_data	*ld;
	int			error, i, next;
	u_int32_t		nextptr;

	XL_LOCK_ASSERT(sc);

	cd = &sc->xl_cdata;
	ld = &sc->xl_ldata;

	for (i = 0; i < XL_RX_LIST_CNT; i++) {
		cd->xl_rx_chain[i].xl_ptr = &ld->xl_rx_list[i];
		error = bus_dmamap_create(sc->xl_mtag, 0,
		    &cd->xl_rx_chain[i].xl_map);
		if (error)
			return (error);
		error = xl_newbuf(sc, &cd->xl_rx_chain[i]);
		if (error)
			return (error);
		if (i == (XL_RX_LIST_CNT - 1))
			next = 0;
		else
			next = i + 1;
		nextptr = ld->xl_rx_dmaaddr +
		    next * sizeof(struct xl_list_onefrag);
		cd->xl_rx_chain[i].xl_next = &cd->xl_rx_chain[next];
		ld->xl_rx_list[i].xl_next = htole32(nextptr);
	}

	bus_dmamap_sync(ld->xl_rx_tag, ld->xl_rx_dmamap, BUS_DMASYNC_PREWRITE);
	cd->xl_rx_head = &cd->xl_rx_chain[0];

	return (0);
}

/*
 * Initialize an RX descriptor and attach an MBUF cluster.
 * If we fail to do so, we need to leave the old mbuf and
 * the old DMA map untouched so that it can be reused.
 */
static int
xl_newbuf(struct xl_softc *sc, struct xl_chain_onefrag *c)
{
	struct mbuf		*m_new = NULL;
	bus_dmamap_t		map;
	bus_dma_segment_t	segs[1];
	int			error, nseg;

	XL_LOCK_ASSERT(sc);

	m_new = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
	if (m_new == NULL)
		return (ENOBUFS);

	m_new->m_len = m_new->m_pkthdr.len = MCLBYTES;

	/* Force longword alignment for packet payload. */
	m_adj(m_new, ETHER_ALIGN);

	error = bus_dmamap_load_mbuf_sg(sc->xl_mtag, sc->xl_tmpmap, m_new,
	    segs, &nseg, BUS_DMA_NOWAIT);
	if (error) {
		m_freem(m_new);
		device_printf(sc->xl_dev, "can't map mbuf (error %d)\n",
		    error);
		return (error);
	}
	KASSERT(nseg == 1,
	    ("%s: too many DMA segments (%d)", __func__, nseg));

	bus_dmamap_unload(sc->xl_mtag, c->xl_map);
	map = c->xl_map;
	c->xl_map = sc->xl_tmpmap;
	sc->xl_tmpmap = map;
	c->xl_mbuf = m_new;
	c->xl_ptr->xl_frag.xl_len = htole32(m_new->m_len | XL_LAST_FRAG);
	c->xl_ptr->xl_frag.xl_addr = htole32(segs->ds_addr);
	c->xl_ptr->xl_status = 0;
	bus_dmamap_sync(sc->xl_mtag, c->xl_map, BUS_DMASYNC_PREREAD);
	return (0);
}

static int
xl_rx_resync(struct xl_softc *sc)
{
	struct xl_chain_onefrag	*pos;
	int			i;

	XL_LOCK_ASSERT(sc);

	pos = sc->xl_cdata.xl_rx_head;

	for (i = 0; i < XL_RX_LIST_CNT; i++) {
		if (pos->xl_ptr->xl_status)
			break;
		pos = pos->xl_next;
	}

	if (i == XL_RX_LIST_CNT)
		return (0);

	sc->xl_cdata.xl_rx_head = pos;

	return (EAGAIN);
}

/*
 * A frame has been uploaded: pass the resulting mbuf chain up to
 * the higher level protocols.
 */
static int
xl_rxeof(struct xl_softc *sc)
{
	struct mbuf		*m;
	struct ifnet		*ifp = sc->xl_ifp;
	struct xl_chain_onefrag	*cur_rx;
	int			total_len;
	int			rx_npkts = 0;
	u_int32_t		rxstat;

	XL_LOCK_ASSERT(sc);
again:
	bus_dmamap_sync(sc->xl_ldata.xl_rx_tag, sc->xl_ldata.xl_rx_dmamap,
	    BUS_DMASYNC_POSTREAD);
	while ((rxstat = le32toh(sc->xl_cdata.xl_rx_head->xl_ptr->xl_status))) {
#ifdef DEVICE_POLLING
		if (ifp->if_capenable & IFCAP_POLLING) {
			if (sc->rxcycles <= 0)
				break;
			sc->rxcycles--;
		}
#endif
		cur_rx = sc->xl_cdata.xl_rx_head;
		sc->xl_cdata.xl_rx_head = cur_rx->xl_next;
		total_len = rxstat & XL_RXSTAT_LENMASK;
		rx_npkts++;

		/*
		 * Since we have told the chip to allow large frames,
		 * we need to trap giant frame errors in software. We allow
		 * a little more than the normal frame size to account for
		 * frames with VLAN tags.
		 */
		if (total_len > XL_MAX_FRAMELEN)
			rxstat |= (XL_RXSTAT_UP_ERROR|XL_RXSTAT_OVERSIZE);

		/*
		 * If an error occurs, update stats, clear the
		 * status word and leave the mbuf cluster in place:
		 * it should simply get re-used next time this descriptor
		 * comes up in the ring.
		 */
		if (rxstat & XL_RXSTAT_UP_ERROR) {
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			cur_rx->xl_ptr->xl_status = 0;
			bus_dmamap_sync(sc->xl_ldata.xl_rx_tag,
			    sc->xl_ldata.xl_rx_dmamap, BUS_DMASYNC_PREWRITE);
			continue;
		}

		/*
		 * If the error bit was not set, the upload complete
		 * bit should be set which means we have a valid packet.
		 * If not, something truly strange has happened.
		 */
		if (!(rxstat & XL_RXSTAT_UP_CMPLT)) {
			device_printf(sc->xl_dev,
			    "bad receive status -- packet dropped\n");
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			cur_rx->xl_ptr->xl_status = 0;
			bus_dmamap_sync(sc->xl_ldata.xl_rx_tag,
			    sc->xl_ldata.xl_rx_dmamap, BUS_DMASYNC_PREWRITE);
			continue;
		}

		/* No errors; receive the packet. */
		bus_dmamap_sync(sc->xl_mtag, cur_rx->xl_map,
		    BUS_DMASYNC_POSTREAD);
		m = cur_rx->xl_mbuf;

		/*
		 * Try to conjure up a new mbuf cluster. If that
		 * fails, it means we have an out of memory condition and
		 * should leave the buffer in place and continue. This will
		 * result in a lost packet, but there's little else we
		 * can do in this situation.
		 */
		if (xl_newbuf(sc, cur_rx)) {
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			cur_rx->xl_ptr->xl_status = 0;
			bus_dmamap_sync(sc->xl_ldata.xl_rx_tag,
			    sc->xl_ldata.xl_rx_dmamap, BUS_DMASYNC_PREWRITE);
			continue;
		}
		bus_dmamap_sync(sc->xl_ldata.xl_rx_tag,
		    sc->xl_ldata.xl_rx_dmamap, BUS_DMASYNC_PREWRITE);

		if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
		m->m_pkthdr.rcvif = ifp;
		m->m_pkthdr.len = m->m_len = total_len;

		if (ifp->if_capenable & IFCAP_RXCSUM) {
			/* Do IP checksum checking. */
			if (rxstat & XL_RXSTAT_IPCKOK)
				m->m_pkthdr.csum_flags |= CSUM_IP_CHECKED;
			if (!(rxstat & XL_RXSTAT_IPCKERR))
				m->m_pkthdr.csum_flags |= CSUM_IP_VALID;
			if ((rxstat & XL_RXSTAT_TCPCOK &&
			     !(rxstat & XL_RXSTAT_TCPCKERR)) ||
			    (rxstat & XL_RXSTAT_UDPCKOK &&
			     !(rxstat & XL_RXSTAT_UDPCKERR))) {
				m->m_pkthdr.csum_flags |=
					CSUM_DATA_VALID|CSUM_PSEUDO_HDR;
				m->m_pkthdr.csum_data = 0xffff;
			}
		}

		XL_UNLOCK(sc);
		(*ifp->if_input)(ifp, m);
		XL_LOCK(sc);

		/*
		 * If we are running from the taskqueue, the interface
		 * might have been stopped while we were passing the last
		 * packet up the network stack.
		 */
		if (!(ifp->if_drv_flags & IFF_DRV_RUNNING))
			return (rx_npkts);
	}

	/*
	 * Handle the 'end of channel' condition. When the upload
	 * engine hits the end of the RX ring, it will stall. This
	 * is our cue to flush the RX ring, reload the uplist pointer
	 * register and unstall the engine.
	 * XXX This is actually a little goofy. With the ThunderLAN
	 * chip, you get an interrupt when the receiver hits the end
	 * of the receive ring, which tells you exactly when you
	 * you need to reload the ring pointer. Here we have to
	 * fake it. I'm mad at myself for not being clever enough
	 * to avoid the use of a goto here.
	 */
	if (CSR_READ_4(sc, XL_UPLIST_PTR) == 0 ||
		CSR_READ_4(sc, XL_UPLIST_STATUS) & XL_PKTSTAT_UP_STALLED) {
		CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_UP_STALL);
		xl_wait(sc);
		CSR_WRITE_4(sc, XL_UPLIST_PTR, sc->xl_ldata.xl_rx_dmaaddr);
		sc->xl_cdata.xl_rx_head = &sc->xl_cdata.xl_rx_chain[0];
		CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_UP_UNSTALL);
		goto again;
	}
	return (rx_npkts);
}

/*
 * Taskqueue wrapper for xl_rxeof().
 */
static void
xl_rxeof_task(void *arg, int pending)
{
	struct xl_softc *sc = (struct xl_softc *)arg;

	XL_LOCK(sc);
	if (sc->xl_ifp->if_drv_flags & IFF_DRV_RUNNING)
		xl_rxeof(sc);
	XL_UNLOCK(sc);
}

/*
 * A frame was downloaded to the chip. It's safe for us to clean up
 * the list buffers.
 */
static void
xl_txeof(struct xl_softc *sc)
{
	struct xl_chain		*cur_tx;
	struct ifnet		*ifp = sc->xl_ifp;

	XL_LOCK_ASSERT(sc);

	/*
	 * Go through our tx list and free mbufs for those
	 * frames that have been uploaded. Note: the 3c905B
	 * sets a special bit in the status word to let us
	 * know that a frame has been downloaded, but the
	 * original 3c900/3c905 adapters don't do that.
	 * Consequently, we have to use a different test if
	 * xl_type != XL_TYPE_905B.
	 */
	while (sc->xl_cdata.xl_tx_head != NULL) {
		cur_tx = sc->xl_cdata.xl_tx_head;

		if (CSR_READ_4(sc, XL_DOWNLIST_PTR))
			break;

		sc->xl_cdata.xl_tx_head = cur_tx->xl_next;
		bus_dmamap_sync(sc->xl_mtag, cur_tx->xl_map,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->xl_mtag, cur_tx->xl_map);
		m_freem(cur_tx->xl_mbuf);
		cur_tx->xl_mbuf = NULL;
		if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

		cur_tx->xl_next = sc->xl_cdata.xl_tx_free;
		sc->xl_cdata.xl_tx_free = cur_tx;
	}

	if (sc->xl_cdata.xl_tx_head == NULL) {
		sc->xl_wdog_timer = 0;
		sc->xl_cdata.xl_tx_tail = NULL;
	} else {
		if (CSR_READ_4(sc, XL_DMACTL) & XL_DMACTL_DOWN_STALLED ||
			!CSR_READ_4(sc, XL_DOWNLIST_PTR)) {
			CSR_WRITE_4(sc, XL_DOWNLIST_PTR,
				sc->xl_cdata.xl_tx_head->xl_phys);
			CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_DOWN_UNSTALL);
		}
	}
}

static void
xl_txeof_90xB(struct xl_softc *sc)
{
	struct xl_chain		*cur_tx = NULL;
	struct ifnet		*ifp = sc->xl_ifp;
	int			idx;

	XL_LOCK_ASSERT(sc);

	bus_dmamap_sync(sc->xl_ldata.xl_tx_tag, sc->xl_ldata.xl_tx_dmamap,
	    BUS_DMASYNC_POSTREAD);
	idx = sc->xl_cdata.xl_tx_cons;
	while (idx != sc->xl_cdata.xl_tx_prod) {
		cur_tx = &sc->xl_cdata.xl_tx_chain[idx];

		if (!(le32toh(cur_tx->xl_ptr->xl_status) &
		      XL_TXSTAT_DL_COMPLETE))
			break;

		if (cur_tx->xl_mbuf != NULL) {
			bus_dmamap_sync(sc->xl_mtag, cur_tx->xl_map,
			    BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->xl_mtag, cur_tx->xl_map);
			m_freem(cur_tx->xl_mbuf);
			cur_tx->xl_mbuf = NULL;
		}

		if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);

		sc->xl_cdata.xl_tx_cnt--;
		XL_INC(idx, XL_TX_LIST_CNT);
	}

	if (sc->xl_cdata.xl_tx_cnt == 0)
		sc->xl_wdog_timer = 0;
	sc->xl_cdata.xl_tx_cons = idx;

	if (cur_tx != NULL)
		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
}

/*
 * TX 'end of channel' interrupt handler. Actually, we should
 * only get a 'TX complete' interrupt if there's a transmit error,
 * so this is really TX error handler.
 */
static void
xl_txeoc(struct xl_softc *sc)
{
	u_int8_t		txstat;

	XL_LOCK_ASSERT(sc);

	while ((txstat = CSR_READ_1(sc, XL_TX_STATUS))) {
		if (txstat & XL_TXSTATUS_UNDERRUN ||
			txstat & XL_TXSTATUS_JABBER ||
			txstat & XL_TXSTATUS_RECLAIM) {
			device_printf(sc->xl_dev,
			    "transmission error: 0x%02x\n", txstat);
			CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_TX_RESET);
			xl_wait(sc);
			if (sc->xl_type == XL_TYPE_905B) {
				if (sc->xl_cdata.xl_tx_cnt) {
					int			i;
					struct xl_chain		*c;

					i = sc->xl_cdata.xl_tx_cons;
					c = &sc->xl_cdata.xl_tx_chain[i];
					CSR_WRITE_4(sc, XL_DOWNLIST_PTR,
					    c->xl_phys);
					CSR_WRITE_1(sc, XL_DOWN_POLL, 64);
					sc->xl_wdog_timer = 5;
				}
			} else {
				if (sc->xl_cdata.xl_tx_head != NULL) {
					CSR_WRITE_4(sc, XL_DOWNLIST_PTR,
					    sc->xl_cdata.xl_tx_head->xl_phys);
					sc->xl_wdog_timer = 5;
				}
			}
			/*
			 * Remember to set this for the
			 * first generation 3c90X chips.
			 */
			CSR_WRITE_1(sc, XL_TX_FREETHRESH, XL_PACKET_SIZE >> 8);
			if (txstat & XL_TXSTATUS_UNDERRUN &&
			    sc->xl_tx_thresh < XL_PACKET_SIZE) {
				sc->xl_tx_thresh += XL_MIN_FRAMELEN;
				device_printf(sc->xl_dev,
"tx underrun, increasing tx start threshold to %d bytes\n", sc->xl_tx_thresh);
			}
			CSR_WRITE_2(sc, XL_COMMAND,
			    XL_CMD_TX_SET_START|sc->xl_tx_thresh);
			if (sc->xl_type == XL_TYPE_905B) {
				CSR_WRITE_2(sc, XL_COMMAND,
				XL_CMD_SET_TX_RECLAIM|(XL_PACKET_SIZE >> 4));
			}
			CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_TX_ENABLE);
			CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_DOWN_UNSTALL);
		} else {
			CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_TX_ENABLE);
			CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_DOWN_UNSTALL);
		}
		/*
		 * Write an arbitrary byte to the TX_STATUS register
		 * to clear this interrupt/error and advance to the next.
		 */
		CSR_WRITE_1(sc, XL_TX_STATUS, 0x01);
	}
}

static void
xl_intr(void *arg)
{
	struct xl_softc		*sc = arg;
	struct ifnet		*ifp = sc->xl_ifp;
	u_int16_t		status;

	XL_LOCK(sc);

#ifdef DEVICE_POLLING
	if (ifp->if_capenable & IFCAP_POLLING) {
		XL_UNLOCK(sc);
		return;
	}
#endif

	for (;;) {
		status = CSR_READ_2(sc, XL_STATUS);
		if ((status & XL_INTRS) == 0 || status == 0xFFFF)
			break;
		CSR_WRITE_2(sc, XL_COMMAND,
		    XL_CMD_INTR_ACK|(status & XL_INTRS));
		if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0)
			break;

		if (status & XL_STAT_UP_COMPLETE) {
			if (xl_rxeof(sc) == 0) {
				while (xl_rx_resync(sc))
					xl_rxeof(sc);
			}
		}

		if (status & XL_STAT_DOWN_COMPLETE) {
			if (sc->xl_type == XL_TYPE_905B)
				xl_txeof_90xB(sc);
			else
				xl_txeof(sc);
		}

		if (status & XL_STAT_TX_COMPLETE) {
			if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
			xl_txeoc(sc);
		}

		if (status & XL_STAT_ADFAIL) {
			ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
			xl_init_locked(sc);
			break;
		}

		if (status & XL_STAT_STATSOFLOW)
			xl_stats_update(sc);
	}

	if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd) &&
	    ifp->if_drv_flags & IFF_DRV_RUNNING) {
		if (sc->xl_type == XL_TYPE_905B)
			xl_start_90xB_locked(ifp);
		else
			xl_start_locked(ifp);
	}

	XL_UNLOCK(sc);
}

#ifdef DEVICE_POLLING
static int
xl_poll(struct ifnet *ifp, enum poll_cmd cmd, int count)
{
	struct xl_softc *sc = ifp->if_softc;
	int rx_npkts = 0;

	XL_LOCK(sc);
	if (ifp->if_drv_flags & IFF_DRV_RUNNING)
		rx_npkts = xl_poll_locked(ifp, cmd, count);
	XL_UNLOCK(sc);
	return (rx_npkts);
}

static int
xl_poll_locked(struct ifnet *ifp, enum poll_cmd cmd, int count)
{
	struct xl_softc *sc = ifp->if_softc;
	int rx_npkts;

	XL_LOCK_ASSERT(sc);

	sc->rxcycles = count;
	rx_npkts = xl_rxeof(sc);
	if (sc->xl_type == XL_TYPE_905B)
		xl_txeof_90xB(sc);
	else
		xl_txeof(sc);

	if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd)) {
		if (sc->xl_type == XL_TYPE_905B)
			xl_start_90xB_locked(ifp);
		else
			xl_start_locked(ifp);
	}

	if (cmd == POLL_AND_CHECK_STATUS) {
		u_int16_t status;

		status = CSR_READ_2(sc, XL_STATUS);
		if (status & XL_INTRS && status != 0xFFFF) {
			CSR_WRITE_2(sc, XL_COMMAND,
			    XL_CMD_INTR_ACK|(status & XL_INTRS));

			if (status & XL_STAT_TX_COMPLETE) {
				if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
				xl_txeoc(sc);
			}

			if (status & XL_STAT_ADFAIL) {
				ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
				xl_init_locked(sc);
			}

			if (status & XL_STAT_STATSOFLOW)
				xl_stats_update(sc);
		}
	}
	return (rx_npkts);
}
#endif /* DEVICE_POLLING */

static void
xl_tick(void *xsc)
{
	struct xl_softc *sc = xsc;
	struct mii_data *mii;

	XL_LOCK_ASSERT(sc);

	if (sc->xl_miibus != NULL) {
		mii = device_get_softc(sc->xl_miibus);
		mii_tick(mii);
	}

	xl_stats_update(sc);
	if (xl_watchdog(sc) == EJUSTRETURN)
		return;

	callout_reset(&sc->xl_tick_callout, hz, xl_tick, sc);
}

static void
xl_stats_update(struct xl_softc *sc)
{
	struct ifnet		*ifp = sc->xl_ifp;
	struct xl_stats		xl_stats;
	u_int8_t		*p;
	int			i;

	XL_LOCK_ASSERT(sc);

	bzero((char *)&xl_stats, sizeof(struct xl_stats));

	p = (u_int8_t *)&xl_stats;

	/* Read all the stats registers. */
	XL_SEL_WIN(6);

	for (i = 0; i < 16; i++)
		*p++ = CSR_READ_1(sc, XL_W6_CARRIER_LOST + i);

	if_inc_counter(ifp, IFCOUNTER_IERRORS, xl_stats.xl_rx_overrun);

	if_inc_counter(ifp, IFCOUNTER_COLLISIONS,
	    xl_stats.xl_tx_multi_collision +
	    xl_stats.xl_tx_single_collision +
	    xl_stats.xl_tx_late_collision);

	/*
	 * Boomerang and cyclone chips have an extra stats counter
	 * in window 4 (BadSSD). We have to read this too in order
	 * to clear out all the stats registers and avoid a statsoflow
	 * interrupt.
	 */
	XL_SEL_WIN(4);
	CSR_READ_1(sc, XL_W4_BADSSD);
	XL_SEL_WIN(7);
}

/*
 * Encapsulate an mbuf chain in a descriptor by coupling the mbuf data
 * pointers to the fragment pointers.
 */
static int
xl_encap(struct xl_softc *sc, struct xl_chain *c, struct mbuf **m_head)
{
	struct mbuf		*m_new;
	struct ifnet		*ifp = sc->xl_ifp;
	int			error, i, nseg, total_len;
	u_int32_t		status;

	XL_LOCK_ASSERT(sc);

	error = bus_dmamap_load_mbuf_sg(sc->xl_mtag, c->xl_map, *m_head,
	    sc->xl_cdata.xl_tx_segs, &nseg, BUS_DMA_NOWAIT);

	if (error && error != EFBIG) {
		if_printf(ifp, "can't map mbuf (error %d)\n", error);
		return (error);
	}

	/*
	 * Handle special case: we used up all 63 fragments,
	 * but we have more mbufs left in the chain. Copy the
	 * data into an mbuf cluster. Note that we don't
	 * bother clearing the values in the other fragment
	 * pointers/counters; it wouldn't gain us anything,
	 * and would waste cycles.
	 */
	if (error) {
		m_new = m_collapse(*m_head, M_NOWAIT, XL_MAXFRAGS);
		if (m_new == NULL) {
			m_freem(*m_head);
			*m_head = NULL;
			return (ENOBUFS);
		}
		*m_head = m_new;

		error = bus_dmamap_load_mbuf_sg(sc->xl_mtag, c->xl_map,
		    *m_head, sc->xl_cdata.xl_tx_segs, &nseg, BUS_DMA_NOWAIT);
		if (error) {
			m_freem(*m_head);
			*m_head = NULL;
			if_printf(ifp, "can't map mbuf (error %d)\n", error);
			return (error);
		}
	}

	KASSERT(nseg <= XL_MAXFRAGS,
	    ("%s: too many DMA segments (%d)", __func__, nseg));
	if (nseg == 0) {
		m_freem(*m_head);
		*m_head = NULL;
		return (EIO);
	}
	bus_dmamap_sync(sc->xl_mtag, c->xl_map, BUS_DMASYNC_PREWRITE);

	total_len = 0;
	for (i = 0; i < nseg; i++) {
		KASSERT(sc->xl_cdata.xl_tx_segs[i].ds_len <= MCLBYTES,
		    ("segment size too large"));
		c->xl_ptr->xl_frag[i].xl_addr =
		    htole32(sc->xl_cdata.xl_tx_segs[i].ds_addr);
		c->xl_ptr->xl_frag[i].xl_len =
		    htole32(sc->xl_cdata.xl_tx_segs[i].ds_len);
		total_len += sc->xl_cdata.xl_tx_segs[i].ds_len;
	}
	c->xl_ptr->xl_frag[nseg - 1].xl_len |= htole32(XL_LAST_FRAG);

	if (sc->xl_type == XL_TYPE_905B) {
		status = XL_TXSTAT_RND_DEFEAT;

#ifndef XL905B_TXCSUM_BROKEN
		if ((*m_head)->m_pkthdr.csum_flags) {
			if ((*m_head)->m_pkthdr.csum_flags & CSUM_IP)
				status |= XL_TXSTAT_IPCKSUM;
			if ((*m_head)->m_pkthdr.csum_flags & CSUM_TCP)
				status |= XL_TXSTAT_TCPCKSUM;
			if ((*m_head)->m_pkthdr.csum_flags & CSUM_UDP)
				status |= XL_TXSTAT_UDPCKSUM;
		}
#endif
	} else
		status = total_len;
	c->xl_ptr->xl_status = htole32(status);
	c->xl_ptr->xl_next = 0;

	c->xl_mbuf = *m_head;
	return (0);
}

/*
 * Main transmit routine. To avoid having to do mbuf copies, we put pointers
 * to the mbuf data regions directly in the transmit lists. We also save a
 * copy of the pointers since the transmit list fragment pointers are
 * physical addresses.
 */

static void
xl_start(struct ifnet *ifp)
{
	struct xl_softc		*sc = ifp->if_softc;

	XL_LOCK(sc);

	if (sc->xl_type == XL_TYPE_905B)
		xl_start_90xB_locked(ifp);
	else
		xl_start_locked(ifp);

	XL_UNLOCK(sc);
}

static void
xl_start_locked(struct ifnet *ifp)
{
	struct xl_softc		*sc = ifp->if_softc;
	struct mbuf		*m_head;
	struct xl_chain		*prev = NULL, *cur_tx = NULL, *start_tx;
	struct xl_chain		*prev_tx;
	int			error;

	XL_LOCK_ASSERT(sc);

	if ((ifp->if_drv_flags & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) !=
	    IFF_DRV_RUNNING)
		return;
	/*
	 * Check for an available queue slot. If there are none,
	 * punt.
	 */
	if (sc->xl_cdata.xl_tx_free == NULL) {
		xl_txeoc(sc);
		xl_txeof(sc);
		if (sc->xl_cdata.xl_tx_free == NULL) {
			ifp->if_drv_flags |= IFF_DRV_OACTIVE;
			return;
		}
	}

	start_tx = sc->xl_cdata.xl_tx_free;

	for (; !IFQ_DRV_IS_EMPTY(&ifp->if_snd) &&
	    sc->xl_cdata.xl_tx_free != NULL;) {
		IFQ_DRV_DEQUEUE(&ifp->if_snd, m_head);
		if (m_head == NULL)
			break;

		/* Pick a descriptor off the free list. */
		prev_tx = cur_tx;
		cur_tx = sc->xl_cdata.xl_tx_free;

		/* Pack the data into the descriptor. */
		error = xl_encap(sc, cur_tx, &m_head);
		if (error) {
			cur_tx = prev_tx;
			if (m_head == NULL)
				break;
			ifp->if_drv_flags |= IFF_DRV_OACTIVE;
			IFQ_DRV_PREPEND(&ifp->if_snd, m_head);
			break;
		}

		sc->xl_cdata.xl_tx_free = cur_tx->xl_next;
		cur_tx->xl_next = NULL;

		/* Chain it together. */
		if (prev != NULL) {
			prev->xl_next = cur_tx;
			prev->xl_ptr->xl_next = htole32(cur_tx->xl_phys);
		}
		prev = cur_tx;

		/*
		 * If there's a BPF listener, bounce a copy of this frame
		 * to him.
		 */
		BPF_MTAP(ifp, cur_tx->xl_mbuf);
	}

	/*
	 * If there are no packets queued, bail.
	 */
	if (cur_tx == NULL)
		return;

	/*
	 * Place the request for the upload interrupt
	 * in the last descriptor in the chain. This way, if
	 * we're chaining several packets at once, we'll only
	 * get an interrupt once for the whole chain rather than
	 * once for each packet.
	 */
	cur_tx->xl_ptr->xl_status |= htole32(XL_TXSTAT_DL_INTR);

	/*
	 * Queue the packets. If the TX channel is clear, update
	 * the downlist pointer register.
	 */
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_DOWN_STALL);
	xl_wait(sc);

	if (sc->xl_cdata.xl_tx_head != NULL) {
		sc->xl_cdata.xl_tx_tail->xl_next = start_tx;
		sc->xl_cdata.xl_tx_tail->xl_ptr->xl_next =
		    htole32(start_tx->xl_phys);
		sc->xl_cdata.xl_tx_tail->xl_ptr->xl_status &=
		    htole32(~XL_TXSTAT_DL_INTR);
		sc->xl_cdata.xl_tx_tail = cur_tx;
	} else {
		sc->xl_cdata.xl_tx_head = start_tx;
		sc->xl_cdata.xl_tx_tail = cur_tx;
	}
	bus_dmamap_sync(sc->xl_ldata.xl_tx_tag, sc->xl_ldata.xl_tx_dmamap,
	    BUS_DMASYNC_PREWRITE);
	if (!CSR_READ_4(sc, XL_DOWNLIST_PTR))
		CSR_WRITE_4(sc, XL_DOWNLIST_PTR, start_tx->xl_phys);

	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_DOWN_UNSTALL);

	XL_SEL_WIN(7);

	/*
	 * Set a timeout in case the chip goes out to lunch.
	 */
	sc->xl_wdog_timer = 5;

	/*
	 * XXX Under certain conditions, usually on slower machines
	 * where interrupts may be dropped, it's possible for the
	 * adapter to chew up all the buffers in the receive ring
	 * and stall, without us being able to do anything about it.
	 * To guard against this, we need to make a pass over the
	 * RX queue to make sure there aren't any packets pending.
	 * Doing it here means we can flush the receive ring at the
	 * same time the chip is DMAing the transmit descriptors we
	 * just gave it.
	 *
	 * 3Com goes to some lengths to emphasize the Parallel Tasking (tm)
	 * nature of their chips in all their marketing literature;
	 * we may as well take advantage of it. :)
	 */
	taskqueue_enqueue(taskqueue_swi, &sc->xl_task);
}

static void
xl_start_90xB_locked(struct ifnet *ifp)
{
	struct xl_softc		*sc = ifp->if_softc;
	struct mbuf		*m_head;
	struct xl_chain		*prev = NULL, *cur_tx = NULL, *start_tx;
	struct xl_chain		*prev_tx;
	int			error, idx;

	XL_LOCK_ASSERT(sc);

	if ((ifp->if_drv_flags & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) !=
	    IFF_DRV_RUNNING)
		return;

	idx = sc->xl_cdata.xl_tx_prod;
	start_tx = &sc->xl_cdata.xl_tx_chain[idx];

	for (; !IFQ_DRV_IS_EMPTY(&ifp->if_snd) &&
	    sc->xl_cdata.xl_tx_chain[idx].xl_mbuf == NULL;) {
		if ((XL_TX_LIST_CNT - sc->xl_cdata.xl_tx_cnt) < 3) {
			ifp->if_drv_flags |= IFF_DRV_OACTIVE;
			break;
		}

		IFQ_DRV_DEQUEUE(&ifp->if_snd, m_head);
		if (m_head == NULL)
			break;

		prev_tx = cur_tx;
		cur_tx = &sc->xl_cdata.xl_tx_chain[idx];

		/* Pack the data into the descriptor. */
		error = xl_encap(sc, cur_tx, &m_head);
		if (error) {
			cur_tx = prev_tx;
			if (m_head == NULL)
				break;
			ifp->if_drv_flags |= IFF_DRV_OACTIVE;
			IFQ_DRV_PREPEND(&ifp->if_snd, m_head);
			break;
		}

		/* Chain it together. */
		if (prev != NULL)
			prev->xl_ptr->xl_next = htole32(cur_tx->xl_phys);
		prev = cur_tx;

		/*
		 * If there's a BPF listener, bounce a copy of this frame
		 * to him.
		 */
		BPF_MTAP(ifp, cur_tx->xl_mbuf);

		XL_INC(idx, XL_TX_LIST_CNT);
		sc->xl_cdata.xl_tx_cnt++;
	}

	/*
	 * If there are no packets queued, bail.
	 */
	if (cur_tx == NULL)
		return;

	/*
	 * Place the request for the upload interrupt
	 * in the last descriptor in the chain. This way, if
	 * we're chaining several packets at once, we'll only
	 * get an interrupt once for the whole chain rather than
	 * once for each packet.
	 */
	cur_tx->xl_ptr->xl_status |= htole32(XL_TXSTAT_DL_INTR);

	/* Start transmission */
	sc->xl_cdata.xl_tx_prod = idx;
	start_tx->xl_prev->xl_ptr->xl_next = htole32(start_tx->xl_phys);
	bus_dmamap_sync(sc->xl_ldata.xl_tx_tag, sc->xl_ldata.xl_tx_dmamap,
	    BUS_DMASYNC_PREWRITE);

	/*
	 * Set a timeout in case the chip goes out to lunch.
	 */
	sc->xl_wdog_timer = 5;
}

static void
xl_init(void *xsc)
{
	struct xl_softc		*sc = xsc;

	XL_LOCK(sc);
	xl_init_locked(sc);
	XL_UNLOCK(sc);
}

static void
xl_init_locked(struct xl_softc *sc)
{
	struct ifnet		*ifp = sc->xl_ifp;
	int			error, i;
	struct mii_data		*mii = NULL;

	XL_LOCK_ASSERT(sc);

	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
		return;
	/*
	 * Cancel pending I/O and free all RX/TX buffers.
	 */
	xl_stop(sc);

	/* Reset the chip to a known state. */
	xl_reset(sc);

	if (sc->xl_miibus == NULL) {
		CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_RX_RESET);
		xl_wait(sc);
	}
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_TX_RESET);
	xl_wait(sc);
	DELAY(10000);

	if (sc->xl_miibus != NULL)
		mii = device_get_softc(sc->xl_miibus);

	/*
	 * Clear WOL status and disable all WOL feature as WOL
	 * would interfere Rx operation under normal environments.
	 */
	if ((sc->xl_flags & XL_FLAG_WOL) != 0) {
		XL_SEL_WIN(7);
		CSR_READ_2(sc, XL_W7_BM_PME);
		CSR_WRITE_2(sc, XL_W7_BM_PME, 0);
	}
	/* Init our MAC address */
	XL_SEL_WIN(2);
	for (i = 0; i < ETHER_ADDR_LEN; i++) {
		CSR_WRITE_1(sc, XL_W2_STATION_ADDR_LO + i,
				IF_LLADDR(sc->xl_ifp)[i]);
	}

	/* Clear the station mask. */
	for (i = 0; i < 3; i++)
		CSR_WRITE_2(sc, XL_W2_STATION_MASK_LO + (i * 2), 0);
#ifdef notdef
	/* Reset TX and RX. */
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_RX_RESET);
	xl_wait(sc);
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_TX_RESET);
	xl_wait(sc);
#endif
	/* Init circular RX list. */
	error = xl_list_rx_init(sc);
	if (error) {
		device_printf(sc->xl_dev, "initialization of the rx ring failed (%d)\n",
		    error);
		xl_stop(sc);
		return;
	}

	/* Init TX descriptors. */
	if (sc->xl_type == XL_TYPE_905B)
		error = xl_list_tx_init_90xB(sc);
	else
		error = xl_list_tx_init(sc);
	if (error) {
		device_printf(sc->xl_dev, "initialization of the tx ring failed (%d)\n",
		    error);
		xl_stop(sc);
		return;
	}

	/*
	 * Set the TX freethresh value.
	 * Note that this has no effect on 3c905B "cyclone"
	 * cards but is required for 3c900/3c905 "boomerang"
	 * cards in order to enable the download engine.
	 */
	CSR_WRITE_1(sc, XL_TX_FREETHRESH, XL_PACKET_SIZE >> 8);

	/* Set the TX start threshold for best performance. */
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_TX_SET_START|sc->xl_tx_thresh);

	/*
	 * If this is a 3c905B, also set the tx reclaim threshold.
	 * This helps cut down on the number of tx reclaim errors
	 * that could happen on a busy network. The chip multiplies
	 * the register value by 16 to obtain the actual threshold
	 * in bytes, so we divide by 16 when setting the value here.
	 * The existing threshold value can be examined by reading
	 * the register at offset 9 in window 5.
	 */
	if (sc->xl_type == XL_TYPE_905B) {
		CSR_WRITE_2(sc, XL_COMMAND,
		    XL_CMD_SET_TX_RECLAIM|(XL_PACKET_SIZE >> 4));
	}

	/* Set RX filter bits. */
	xl_rxfilter(sc);

	/*
	 * Load the address of the RX list. We have to
	 * stall the upload engine before we can manipulate
	 * the uplist pointer register, then unstall it when
	 * we're finished. We also have to wait for the
	 * stall command to complete before proceeding.
	 * Note that we have to do this after any RX resets
	 * have completed since the uplist register is cleared
	 * by a reset.
	 */
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_UP_STALL);
	xl_wait(sc);
	CSR_WRITE_4(sc, XL_UPLIST_PTR, sc->xl_ldata.xl_rx_dmaaddr);
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_UP_UNSTALL);
	xl_wait(sc);

	if (sc->xl_type == XL_TYPE_905B) {
		/* Set polling interval */
		CSR_WRITE_1(sc, XL_DOWN_POLL, 64);
		/* Load the address of the TX list */
		CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_DOWN_STALL);
		xl_wait(sc);
		CSR_WRITE_4(sc, XL_DOWNLIST_PTR,
		    sc->xl_cdata.xl_tx_chain[0].xl_phys);
		CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_DOWN_UNSTALL);
		xl_wait(sc);
	}

	/*
	 * If the coax transceiver is on, make sure to enable
	 * the DC-DC converter.
	 */
	XL_SEL_WIN(3);
	if (sc->xl_xcvr == XL_XCVR_COAX)
		CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_COAX_START);
	else
		CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_COAX_STOP);

	/*
	 * increase packet size to allow reception of 802.1q or ISL packets.
	 * For the 3c90x chip, set the 'allow large packets' bit in the MAC
	 * control register. For 3c90xB/C chips, use the RX packet size
	 * register.
	 */

	if (sc->xl_type == XL_TYPE_905B)
		CSR_WRITE_2(sc, XL_W3_MAXPKTSIZE, XL_PACKET_SIZE);
	else {
		u_int8_t macctl;
		macctl = CSR_READ_1(sc, XL_W3_MAC_CTRL);
		macctl |= XL_MACCTRL_ALLOW_LARGE_PACK;
		CSR_WRITE_1(sc, XL_W3_MAC_CTRL, macctl);
	}

	/* Clear out the stats counters. */
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_STATS_DISABLE);
	xl_stats_update(sc);
	XL_SEL_WIN(4);
	CSR_WRITE_2(sc, XL_W4_NET_DIAG, XL_NETDIAG_UPPER_BYTES_ENABLE);
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_STATS_ENABLE);

	/*
	 * Enable interrupts.
	 */
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_INTR_ACK|0xFF);
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_STAT_ENB|XL_INTRS);
#ifdef DEVICE_POLLING
	/* Disable interrupts if we are polling. */
	if (ifp->if_capenable & IFCAP_POLLING)
		CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_INTR_ENB|0);
	else
#endif
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_INTR_ENB|XL_INTRS);
	if (sc->xl_flags & XL_FLAG_FUNCREG)
	    bus_space_write_4(sc->xl_ftag, sc->xl_fhandle, 4, 0x8000);

	/* Set the RX early threshold */
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_RX_SET_THRESH|(XL_PACKET_SIZE >>2));
	CSR_WRITE_4(sc, XL_DMACTL, XL_DMACTL_UP_RX_EARLY);

	/* Enable receiver and transmitter. */
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_TX_ENABLE);
	xl_wait(sc);
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_RX_ENABLE);
	xl_wait(sc);

	/* XXX Downcall to miibus. */
	if (mii != NULL)
		mii_mediachg(mii);

	/* Select window 7 for normal operations. */
	XL_SEL_WIN(7);

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

	sc->xl_wdog_timer = 0;
	callout_reset(&sc->xl_tick_callout, hz, xl_tick, sc);
}

/*
 * Set media options.
 */
static int
xl_ifmedia_upd(struct ifnet *ifp)
{
	struct xl_softc		*sc = ifp->if_softc;
	struct ifmedia		*ifm = NULL;
	struct mii_data		*mii = NULL;

	XL_LOCK(sc);

	if (sc->xl_miibus != NULL)
		mii = device_get_softc(sc->xl_miibus);
	if (mii == NULL)
		ifm = &sc->ifmedia;
	else
		ifm = &mii->mii_media;

	switch (IFM_SUBTYPE(ifm->ifm_media)) {
	case IFM_100_FX:
	case IFM_10_FL:
	case IFM_10_2:
	case IFM_10_5:
		xl_setmode(sc, ifm->ifm_media);
		XL_UNLOCK(sc);
		return (0);
	}

	if (sc->xl_media & XL_MEDIAOPT_MII ||
	    sc->xl_media & XL_MEDIAOPT_BTX ||
	    sc->xl_media & XL_MEDIAOPT_BT4) {
		ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
		xl_init_locked(sc);
	} else {
		xl_setmode(sc, ifm->ifm_media);
	}

	XL_UNLOCK(sc);

	return (0);
}

/*
 * Report current media status.
 */
static void
xl_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct xl_softc		*sc = ifp->if_softc;
	u_int32_t		icfg;
	u_int16_t		status = 0;
	struct mii_data		*mii = NULL;

	XL_LOCK(sc);

	if (sc->xl_miibus != NULL)
		mii = device_get_softc(sc->xl_miibus);

	XL_SEL_WIN(4);
	status = CSR_READ_2(sc, XL_W4_MEDIA_STATUS);

	XL_SEL_WIN(3);
	icfg = CSR_READ_4(sc, XL_W3_INTERNAL_CFG) & XL_ICFG_CONNECTOR_MASK;
	icfg >>= XL_ICFG_CONNECTOR_BITS;

	ifmr->ifm_active = IFM_ETHER;
	ifmr->ifm_status = IFM_AVALID;

	if ((status & XL_MEDIASTAT_CARRIER) == 0)
		ifmr->ifm_status |= IFM_ACTIVE;

	switch (icfg) {
	case XL_XCVR_10BT:
		ifmr->ifm_active = IFM_ETHER|IFM_10_T;
		if (CSR_READ_1(sc, XL_W3_MAC_CTRL) & XL_MACCTRL_DUPLEX)
			ifmr->ifm_active |= IFM_FDX;
		else
			ifmr->ifm_active |= IFM_HDX;
		break;
	case XL_XCVR_AUI:
		if (sc->xl_type == XL_TYPE_905B &&
		    sc->xl_media == XL_MEDIAOPT_10FL) {
			ifmr->ifm_active = IFM_ETHER|IFM_10_FL;
			if (CSR_READ_1(sc, XL_W3_MAC_CTRL) & XL_MACCTRL_DUPLEX)
				ifmr->ifm_active |= IFM_FDX;
			else
				ifmr->ifm_active |= IFM_HDX;
		} else
			ifmr->ifm_active = IFM_ETHER|IFM_10_5;
		break;
	case XL_XCVR_COAX:
		ifmr->ifm_active = IFM_ETHER|IFM_10_2;
		break;
	/*
	 * XXX MII and BTX/AUTO should be separate cases.
	 */

	case XL_XCVR_100BTX:
	case XL_XCVR_AUTO:
	case XL_XCVR_MII:
		if (mii != NULL) {
			mii_pollstat(mii);
			ifmr->ifm_active = mii->mii_media_active;
			ifmr->ifm_status = mii->mii_media_status;
		}
		break;
	case XL_XCVR_100BFX:
		ifmr->ifm_active = IFM_ETHER|IFM_100_FX;
		break;
	default:
		if_printf(ifp, "unknown XCVR type: %d\n", icfg);
		break;
	}

	XL_UNLOCK(sc);
}

static int
xl_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct xl_softc		*sc = ifp->if_softc;
	struct ifreq		*ifr = (struct ifreq *) data;
	int			error = 0, mask;
	struct mii_data		*mii = NULL;

	switch (command) {
	case SIOCSIFFLAGS:
		XL_LOCK(sc);
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_drv_flags & IFF_DRV_RUNNING &&
			    (ifp->if_flags ^ sc->xl_if_flags) &
			    (IFF_PROMISC | IFF_ALLMULTI))
				xl_rxfilter(sc);
			else
				xl_init_locked(sc);
		} else {
			if (ifp->if_drv_flags & IFF_DRV_RUNNING)
				xl_stop(sc);
		}
		sc->xl_if_flags = ifp->if_flags;
		XL_UNLOCK(sc);
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		/* XXX Downcall from if_addmulti() possibly with locks held. */
		XL_LOCK(sc);
		if (ifp->if_drv_flags & IFF_DRV_RUNNING)
			xl_rxfilter(sc);
		XL_UNLOCK(sc);
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		if (sc->xl_miibus != NULL)
			mii = device_get_softc(sc->xl_miibus);
		if (mii == NULL)
			error = ifmedia_ioctl(ifp, ifr,
			    &sc->ifmedia, command);
		else
			error = ifmedia_ioctl(ifp, ifr,
			    &mii->mii_media, command);
		break;
	case SIOCSIFCAP:
		mask = ifr->ifr_reqcap ^ ifp->if_capenable;
#ifdef DEVICE_POLLING
		if ((mask & IFCAP_POLLING) != 0 &&
		    (ifp->if_capabilities & IFCAP_POLLING) != 0) {
			ifp->if_capenable ^= IFCAP_POLLING;
			if ((ifp->if_capenable & IFCAP_POLLING) != 0) {
				error = ether_poll_register(xl_poll, ifp);
				if (error)
					break;
				XL_LOCK(sc);
				/* Disable interrupts */
				CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_INTR_ENB|0);
				ifp->if_capenable |= IFCAP_POLLING;
				XL_UNLOCK(sc);
			} else {
				error = ether_poll_deregister(ifp);
				/* Enable interrupts. */
				XL_LOCK(sc);
				CSR_WRITE_2(sc, XL_COMMAND,
				    XL_CMD_INTR_ACK | 0xFF);
				CSR_WRITE_2(sc, XL_COMMAND,
				    XL_CMD_INTR_ENB | XL_INTRS);
				if (sc->xl_flags & XL_FLAG_FUNCREG)
					bus_space_write_4(sc->xl_ftag,
					    sc->xl_fhandle, 4, 0x8000);
				XL_UNLOCK(sc);
			}
		}
#endif /* DEVICE_POLLING */
		XL_LOCK(sc);
		if ((mask & IFCAP_TXCSUM) != 0 &&
		    (ifp->if_capabilities & IFCAP_TXCSUM) != 0) {
			ifp->if_capenable ^= IFCAP_TXCSUM;
			if ((ifp->if_capenable & IFCAP_TXCSUM) != 0)
				ifp->if_hwassist |= XL905B_CSUM_FEATURES;
			else
				ifp->if_hwassist &= ~XL905B_CSUM_FEATURES;
		}
		if ((mask & IFCAP_RXCSUM) != 0 &&
		    (ifp->if_capabilities & IFCAP_RXCSUM) != 0)
			ifp->if_capenable ^= IFCAP_RXCSUM;
		if ((mask & IFCAP_WOL_MAGIC) != 0 &&
		    (ifp->if_capabilities & IFCAP_WOL_MAGIC) != 0)
			ifp->if_capenable ^= IFCAP_WOL_MAGIC;
		XL_UNLOCK(sc);
		break;
	default:
		error = ether_ioctl(ifp, command, data);
		break;
	}

	return (error);
}

static int
xl_watchdog(struct xl_softc *sc)
{
	struct ifnet		*ifp = sc->xl_ifp;
	u_int16_t		status = 0;
	int			misintr;

	XL_LOCK_ASSERT(sc);

	if (sc->xl_wdog_timer == 0 || --sc->xl_wdog_timer != 0)
		return (0);

	xl_rxeof(sc);
	xl_txeoc(sc);
	misintr = 0;
	if (sc->xl_type == XL_TYPE_905B) {
		xl_txeof_90xB(sc);
		if (sc->xl_cdata.xl_tx_cnt == 0)
			misintr++;
	} else {
		xl_txeof(sc);
		if (sc->xl_cdata.xl_tx_head == NULL)
			misintr++;
	}
	if (misintr != 0) {
		device_printf(sc->xl_dev,
		    "watchdog timeout (missed Tx interrupts) -- recovering\n");
		return (0);
	}

	if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
	XL_SEL_WIN(4);
	status = CSR_READ_2(sc, XL_W4_MEDIA_STATUS);
	device_printf(sc->xl_dev, "watchdog timeout\n");

	if (status & XL_MEDIASTAT_CARRIER)
		device_printf(sc->xl_dev,
		    "no carrier - transceiver cable problem?\n");

	ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
	xl_init_locked(sc);

	if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd)) {
		if (sc->xl_type == XL_TYPE_905B)
			xl_start_90xB_locked(ifp);
		else
			xl_start_locked(ifp);
	}

	return (EJUSTRETURN);
}

/*
 * Stop the adapter and free any mbufs allocated to the
 * RX and TX lists.
 */
static void
xl_stop(struct xl_softc *sc)
{
	int			i;
	struct ifnet		*ifp = sc->xl_ifp;

	XL_LOCK_ASSERT(sc);

	sc->xl_wdog_timer = 0;

	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_RX_DISABLE);
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_STATS_DISABLE);
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_INTR_ENB);
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_RX_DISCARD);
	xl_wait(sc);
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_TX_DISABLE);
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_COAX_STOP);
	DELAY(800);

#ifdef foo
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_RX_RESET);
	xl_wait(sc);
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_TX_RESET);
	xl_wait(sc);
#endif

	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_INTR_ACK|XL_STAT_INTLATCH);
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_STAT_ENB|0);
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_INTR_ENB|0);
	if (sc->xl_flags & XL_FLAG_FUNCREG)
		bus_space_write_4(sc->xl_ftag, sc->xl_fhandle, 4, 0x8000);

	/* Stop the stats updater. */
	callout_stop(&sc->xl_tick_callout);

	/*
	 * Free data in the RX lists.
	 */
	for (i = 0; i < XL_RX_LIST_CNT; i++) {
		if (sc->xl_cdata.xl_rx_chain[i].xl_mbuf != NULL) {
			bus_dmamap_unload(sc->xl_mtag,
			    sc->xl_cdata.xl_rx_chain[i].xl_map);
			bus_dmamap_destroy(sc->xl_mtag,
			    sc->xl_cdata.xl_rx_chain[i].xl_map);
			m_freem(sc->xl_cdata.xl_rx_chain[i].xl_mbuf);
			sc->xl_cdata.xl_rx_chain[i].xl_mbuf = NULL;
		}
	}
	if (sc->xl_ldata.xl_rx_list != NULL)
		bzero(sc->xl_ldata.xl_rx_list, XL_RX_LIST_SZ);
	/*
	 * Free the TX list buffers.
	 */
	for (i = 0; i < XL_TX_LIST_CNT; i++) {
		if (sc->xl_cdata.xl_tx_chain[i].xl_mbuf != NULL) {
			bus_dmamap_unload(sc->xl_mtag,
			    sc->xl_cdata.xl_tx_chain[i].xl_map);
			bus_dmamap_destroy(sc->xl_mtag,
			    sc->xl_cdata.xl_tx_chain[i].xl_map);
			m_freem(sc->xl_cdata.xl_tx_chain[i].xl_mbuf);
			sc->xl_cdata.xl_tx_chain[i].xl_mbuf = NULL;
		}
	}
	if (sc->xl_ldata.xl_tx_list != NULL)
		bzero(sc->xl_ldata.xl_tx_list, XL_TX_LIST_SZ);

	ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);
}

/*
 * Stop all chip I/O so that the kernel's probe routines don't
 * get confused by errant DMAs when rebooting.
 */
static int
xl_shutdown(device_t dev)
{

	return (xl_suspend(dev));
}

static int
xl_suspend(device_t dev)
{
	struct xl_softc		*sc;

	sc = device_get_softc(dev);

	XL_LOCK(sc);
	xl_stop(sc);
	xl_setwol(sc);
	XL_UNLOCK(sc);

	return (0);
}

static int
xl_resume(device_t dev)
{
	struct xl_softc		*sc;
	struct ifnet		*ifp;

	sc = device_get_softc(dev);
	ifp = sc->xl_ifp;

	XL_LOCK(sc);

	if (ifp->if_flags & IFF_UP) {
		ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
		xl_init_locked(sc);
	}

	XL_UNLOCK(sc);

	return (0);
}

static void
xl_setwol(struct xl_softc *sc)
{
	struct ifnet		*ifp;
	u_int16_t		cfg, pmstat;

	if ((sc->xl_flags & XL_FLAG_WOL) == 0)
		return;

	ifp = sc->xl_ifp;
	XL_SEL_WIN(7);
	/* Clear any pending PME events. */
	CSR_READ_2(sc, XL_W7_BM_PME);
	cfg = 0;
	if ((ifp->if_capenable & IFCAP_WOL_MAGIC) != 0)
		cfg |= XL_BM_PME_MAGIC;
	CSR_WRITE_2(sc, XL_W7_BM_PME, cfg);
	/* Enable RX. */
	if ((ifp->if_capenable & IFCAP_WOL_MAGIC) != 0)
		CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_RX_ENABLE);
	/* Request PME. */
	pmstat = pci_read_config(sc->xl_dev,
	    sc->xl_pmcap + PCIR_POWER_STATUS, 2);
	if ((ifp->if_capenable & IFCAP_WOL_MAGIC) != 0)
		pmstat |= PCIM_PSTAT_PMEENABLE;
	else
		pmstat &= ~PCIM_PSTAT_PMEENABLE;
	pci_write_config(sc->xl_dev,
	    sc->xl_pmcap + PCIR_POWER_STATUS, pmstat, 2);
}
