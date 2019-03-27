/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2005 Poul-Henning Kamp <phk@FreeBSD.org>
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
 * SiS 900/SiS 7016 fast ethernet PCI NIC driver. Datasheets are
 * available from http://www.sis.com.tw.
 *
 * This driver also supports the NatSemi DP83815. Datasheets are
 * available from http://www.national.com.
 *
 * Written by Bill Paul <wpaul@ee.columbia.edu>
 * Electrical Engineering Department
 * Columbia University, New York City
 */
/*
 * The SiS 900 is a fairly simple chip. It uses bus master DMA with
 * simple TX and RX descriptors of 3 longwords in size. The receiver
 * has a single perfect filter entry for the station address and a
 * 128-bit multicast hash table. The SiS 900 has a built-in MII-based
 * transceiver while the 7016 requires an external transceiver chip.
 * Both chips offer the standard bit-bang MII interface as well as
 * an enchanced PHY interface which simplifies accessing MII registers.
 *
 * The only downside to this chipset is that RX descriptors must be
 * longword aligned.
 */

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_device_polling.h"
#endif

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/if_vlan_var.h>

#include <net/bpf.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <dev/mii/mii.h>
#include <dev/mii/mii_bitbang.h>
#include <dev/mii/miivar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#define SIS_USEIOSPACE

#include <dev/sis/if_sisreg.h>

MODULE_DEPEND(sis, pci, 1, 1, 1);
MODULE_DEPEND(sis, ether, 1, 1, 1);
MODULE_DEPEND(sis, miibus, 1, 1, 1);

/* "device miibus" required.  See GENERIC if you get errors here. */
#include "miibus_if.h"

#define	SIS_LOCK(_sc)		mtx_lock(&(_sc)->sis_mtx)
#define	SIS_UNLOCK(_sc)		mtx_unlock(&(_sc)->sis_mtx)
#define	SIS_LOCK_ASSERT(_sc)	mtx_assert(&(_sc)->sis_mtx, MA_OWNED)

/*
 * register space access macros
 */
#define CSR_WRITE_4(sc, reg, val)	bus_write_4(sc->sis_res[0], reg, val)

#define CSR_READ_4(sc, reg)		bus_read_4(sc->sis_res[0], reg)

#define CSR_READ_2(sc, reg)		bus_read_2(sc->sis_res[0], reg)

#define	CSR_BARRIER(sc, reg, length, flags)				\
	bus_barrier(sc->sis_res[0], reg, length, flags)

/*
 * Various supported device vendors/types and their names.
 */
static const struct sis_type sis_devs[] = {
	{ SIS_VENDORID, SIS_DEVICEID_900, "SiS 900 10/100BaseTX" },
	{ SIS_VENDORID, SIS_DEVICEID_7016, "SiS 7016 10/100BaseTX" },
	{ NS_VENDORID, NS_DEVICEID_DP83815, "NatSemi DP8381[56] 10/100BaseTX" },
	{ 0, 0, NULL }
};

static int sis_detach(device_t);
static __inline void sis_discard_rxbuf(struct sis_rxdesc *);
static int sis_dma_alloc(struct sis_softc *);
static void sis_dma_free(struct sis_softc *);
static int sis_dma_ring_alloc(struct sis_softc *, bus_size_t, bus_size_t,
    bus_dma_tag_t *, uint8_t **, bus_dmamap_t *, bus_addr_t *, const char *);
static void sis_dmamap_cb(void *, bus_dma_segment_t *, int, int);
#ifndef __NO_STRICT_ALIGNMENT
static __inline void sis_fixup_rx(struct mbuf *);
#endif
static void sis_ifmedia_sts(struct ifnet *, struct ifmediareq *);
static int sis_ifmedia_upd(struct ifnet *);
static void sis_init(void *);
static void sis_initl(struct sis_softc *);
static void sis_intr(void *);
static int sis_ioctl(struct ifnet *, u_long, caddr_t);
static uint32_t sis_mii_bitbang_read(device_t);
static void sis_mii_bitbang_write(device_t, uint32_t);
static int sis_newbuf(struct sis_softc *, struct sis_rxdesc *);
static int sis_resume(device_t);
static int sis_rxeof(struct sis_softc *);
static void sis_rxfilter(struct sis_softc *);
static void sis_rxfilter_ns(struct sis_softc *);
static void sis_rxfilter_sis(struct sis_softc *);
static void sis_start(struct ifnet *);
static void sis_startl(struct ifnet *);
static void sis_stop(struct sis_softc *);
static int sis_suspend(device_t);
static void sis_add_sysctls(struct sis_softc *);
static void sis_watchdog(struct sis_softc *);
static void sis_wol(struct sis_softc *);

/*
 * MII bit-bang glue
 */
static const struct mii_bitbang_ops sis_mii_bitbang_ops = {
	sis_mii_bitbang_read,
	sis_mii_bitbang_write,
	{
		SIS_MII_DATA,		/* MII_BIT_MDO */
		SIS_MII_DATA,		/* MII_BIT_MDI */
		SIS_MII_CLK,		/* MII_BIT_MDC */
		SIS_MII_DIR,		/* MII_BIT_DIR_HOST_PHY */
		0,			/* MII_BIT_DIR_PHY_HOST */
	}
};

static struct resource_spec sis_res_spec[] = {
#ifdef SIS_USEIOSPACE
	{ SYS_RES_IOPORT,	SIS_PCI_LOIO,	RF_ACTIVE},
#else
	{ SYS_RES_MEMORY,	SIS_PCI_LOMEM,	RF_ACTIVE},
#endif
	{ SYS_RES_IRQ,		0,		RF_ACTIVE | RF_SHAREABLE},
	{ -1, 0 }
};

#define SIS_SETBIT(sc, reg, x)				\
	CSR_WRITE_4(sc, reg,				\
		CSR_READ_4(sc, reg) | (x))

#define SIS_CLRBIT(sc, reg, x)				\
	CSR_WRITE_4(sc, reg,				\
		CSR_READ_4(sc, reg) & ~(x))

#define SIO_SET(x)					\
	CSR_WRITE_4(sc, SIS_EECTL, CSR_READ_4(sc, SIS_EECTL) | x)

#define SIO_CLR(x)					\
	CSR_WRITE_4(sc, SIS_EECTL, CSR_READ_4(sc, SIS_EECTL) & ~x)

/*
 * Routine to reverse the bits in a word. Stolen almost
 * verbatim from /usr/games/fortune.
 */
static uint16_t
sis_reverse(uint16_t n)
{
	n = ((n >>  1) & 0x5555) | ((n <<  1) & 0xaaaa);
	n = ((n >>  2) & 0x3333) | ((n <<  2) & 0xcccc);
	n = ((n >>  4) & 0x0f0f) | ((n <<  4) & 0xf0f0);
	n = ((n >>  8) & 0x00ff) | ((n <<  8) & 0xff00);

	return (n);
}

static void
sis_delay(struct sis_softc *sc)
{
	int			idx;

	for (idx = (300 / 33) + 1; idx > 0; idx--)
		CSR_READ_4(sc, SIS_CSR);
}

static void
sis_eeprom_idle(struct sis_softc *sc)
{
	int		i;

	SIO_SET(SIS_EECTL_CSEL);
	sis_delay(sc);
	SIO_SET(SIS_EECTL_CLK);
	sis_delay(sc);

	for (i = 0; i < 25; i++) {
		SIO_CLR(SIS_EECTL_CLK);
		sis_delay(sc);
		SIO_SET(SIS_EECTL_CLK);
		sis_delay(sc);
	}

	SIO_CLR(SIS_EECTL_CLK);
	sis_delay(sc);
	SIO_CLR(SIS_EECTL_CSEL);
	sis_delay(sc);
	CSR_WRITE_4(sc, SIS_EECTL, 0x00000000);
}

/*
 * Send a read command and address to the EEPROM, check for ACK.
 */
static void
sis_eeprom_putbyte(struct sis_softc *sc, int addr)
{
	int		d, i;

	d = addr | SIS_EECMD_READ;

	/*
	 * Feed in each bit and stobe the clock.
	 */
	for (i = 0x400; i; i >>= 1) {
		if (d & i) {
			SIO_SET(SIS_EECTL_DIN);
		} else {
			SIO_CLR(SIS_EECTL_DIN);
		}
		sis_delay(sc);
		SIO_SET(SIS_EECTL_CLK);
		sis_delay(sc);
		SIO_CLR(SIS_EECTL_CLK);
		sis_delay(sc);
	}
}

/*
 * Read a word of data stored in the EEPROM at address 'addr.'
 */
static void
sis_eeprom_getword(struct sis_softc *sc, int addr, uint16_t *dest)
{
	int		i;
	uint16_t	word = 0;

	/* Force EEPROM to idle state. */
	sis_eeprom_idle(sc);

	/* Enter EEPROM access mode. */
	sis_delay(sc);
	SIO_CLR(SIS_EECTL_CLK);
	sis_delay(sc);
	SIO_SET(SIS_EECTL_CSEL);
	sis_delay(sc);

	/*
	 * Send address of word we want to read.
	 */
	sis_eeprom_putbyte(sc, addr);

	/*
	 * Start reading bits from EEPROM.
	 */
	for (i = 0x8000; i; i >>= 1) {
		SIO_SET(SIS_EECTL_CLK);
		sis_delay(sc);
		if (CSR_READ_4(sc, SIS_EECTL) & SIS_EECTL_DOUT)
			word |= i;
		sis_delay(sc);
		SIO_CLR(SIS_EECTL_CLK);
		sis_delay(sc);
	}

	/* Turn off EEPROM access mode. */
	sis_eeprom_idle(sc);

	*dest = word;
}

/*
 * Read a sequence of words from the EEPROM.
 */
static void
sis_read_eeprom(struct sis_softc *sc, caddr_t dest, int off, int cnt, int swap)
{
	int			i;
	uint16_t		word = 0, *ptr;

	for (i = 0; i < cnt; i++) {
		sis_eeprom_getword(sc, off + i, &word);
		ptr = (uint16_t *)(dest + (i * 2));
		if (swap)
			*ptr = ntohs(word);
		else
			*ptr = word;
	}
}

#if defined(__i386__) || defined(__amd64__)
static device_t
sis_find_bridge(device_t dev)
{
	devclass_t		pci_devclass;
	device_t		*pci_devices;
	int			pci_count = 0;
	device_t		*pci_children;
	int			pci_childcount = 0;
	device_t		*busp, *childp;
	device_t		child = NULL;
	int			i, j;

	if ((pci_devclass = devclass_find("pci")) == NULL)
		return (NULL);

	devclass_get_devices(pci_devclass, &pci_devices, &pci_count);

	for (i = 0, busp = pci_devices; i < pci_count; i++, busp++) {
		if (device_get_children(*busp, &pci_children, &pci_childcount))
			continue;
		for (j = 0, childp = pci_children;
		    j < pci_childcount; j++, childp++) {
			if (pci_get_vendor(*childp) == SIS_VENDORID &&
			    pci_get_device(*childp) == 0x0008) {
				child = *childp;
				free(pci_children, M_TEMP);
				goto done;
			}
		}
		free(pci_children, M_TEMP);
	}

done:
	free(pci_devices, M_TEMP);
	return (child);
}

static void
sis_read_cmos(struct sis_softc *sc, device_t dev, caddr_t dest, int off, int cnt)
{
	device_t		bridge;
	uint8_t			reg;
	int			i;
	bus_space_tag_t		btag;

	bridge = sis_find_bridge(dev);
	if (bridge == NULL)
		return;
	reg = pci_read_config(bridge, 0x48, 1);
	pci_write_config(bridge, 0x48, reg|0x40, 1);

	/* XXX */
#if defined(__amd64__) || defined(__i386__)
	btag = X86_BUS_SPACE_IO;
#endif

	for (i = 0; i < cnt; i++) {
		bus_space_write_1(btag, 0x0, 0x70, i + off);
		*(dest + i) = bus_space_read_1(btag, 0x0, 0x71);
	}

	pci_write_config(bridge, 0x48, reg & ~0x40, 1);
}

static void
sis_read_mac(struct sis_softc *sc, device_t dev, caddr_t dest)
{
	uint32_t		filtsave, csrsave;

	filtsave = CSR_READ_4(sc, SIS_RXFILT_CTL);
	csrsave = CSR_READ_4(sc, SIS_CSR);

	CSR_WRITE_4(sc, SIS_CSR, SIS_CSR_RELOAD | filtsave);
	CSR_WRITE_4(sc, SIS_CSR, 0);

	CSR_WRITE_4(sc, SIS_RXFILT_CTL, filtsave & ~SIS_RXFILTCTL_ENABLE);

	CSR_WRITE_4(sc, SIS_RXFILT_CTL, SIS_FILTADDR_PAR0);
	((uint16_t *)dest)[0] = CSR_READ_2(sc, SIS_RXFILT_DATA);
	CSR_WRITE_4(sc, SIS_RXFILT_CTL,SIS_FILTADDR_PAR1);
	((uint16_t *)dest)[1] = CSR_READ_2(sc, SIS_RXFILT_DATA);
	CSR_WRITE_4(sc, SIS_RXFILT_CTL, SIS_FILTADDR_PAR2);
	((uint16_t *)dest)[2] = CSR_READ_2(sc, SIS_RXFILT_DATA);

	CSR_WRITE_4(sc, SIS_RXFILT_CTL, filtsave);
	CSR_WRITE_4(sc, SIS_CSR, csrsave);
}
#endif

/*
 * Read the MII serial port for the MII bit-bang module.
 */
static uint32_t
sis_mii_bitbang_read(device_t dev)
{
	struct sis_softc	*sc;
	uint32_t		val;

	sc = device_get_softc(dev);

	val = CSR_READ_4(sc, SIS_EECTL);
	CSR_BARRIER(sc, SIS_EECTL, 4,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	return (val);
}

/*
 * Write the MII serial port for the MII bit-bang module.
 */
static void
sis_mii_bitbang_write(device_t dev, uint32_t val)
{
	struct sis_softc	*sc;

	sc = device_get_softc(dev);

	CSR_WRITE_4(sc, SIS_EECTL, val);
	CSR_BARRIER(sc, SIS_EECTL, 4,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
}

static int
sis_miibus_readreg(device_t dev, int phy, int reg)
{
	struct sis_softc	*sc;

	sc = device_get_softc(dev);

	if (sc->sis_type == SIS_TYPE_83815) {
		if (phy != 0)
			return (0);
		/*
		 * The NatSemi chip can take a while after
		 * a reset to come ready, during which the BMSR
		 * returns a value of 0. This is *never* supposed
		 * to happen: some of the BMSR bits are meant to
		 * be hardwired in the on position, and this can
		 * confuse the miibus code a bit during the probe
		 * and attach phase. So we make an effort to check
		 * for this condition and wait for it to clear.
		 */
		if (!CSR_READ_4(sc, NS_BMSR))
			DELAY(1000);
		return CSR_READ_4(sc, NS_BMCR + (reg * 4));
	}

	/*
	 * Chipsets < SIS_635 seem not to be able to read/write
	 * through mdio. Use the enhanced PHY access register
	 * again for them.
	 */
	if (sc->sis_type == SIS_TYPE_900 &&
	    sc->sis_rev < SIS_REV_635) {
		int i, val = 0;

		if (phy != 0)
			return (0);

		CSR_WRITE_4(sc, SIS_PHYCTL,
		    (phy << 11) | (reg << 6) | SIS_PHYOP_READ);
		SIS_SETBIT(sc, SIS_PHYCTL, SIS_PHYCTL_ACCESS);

		for (i = 0; i < SIS_TIMEOUT; i++) {
			if (!(CSR_READ_4(sc, SIS_PHYCTL) & SIS_PHYCTL_ACCESS))
				break;
		}

		if (i == SIS_TIMEOUT) {
			device_printf(sc->sis_dev,
			    "PHY failed to come ready\n");
			return (0);
		}

		val = (CSR_READ_4(sc, SIS_PHYCTL) >> 16) & 0xFFFF;

		if (val == 0xFFFF)
			return (0);

		return (val);
	} else
		return (mii_bitbang_readreg(dev, &sis_mii_bitbang_ops, phy,
		    reg));
}

static int
sis_miibus_writereg(device_t dev, int phy, int reg, int data)
{
	struct sis_softc	*sc;

	sc = device_get_softc(dev);

	if (sc->sis_type == SIS_TYPE_83815) {
		if (phy != 0)
			return (0);
		CSR_WRITE_4(sc, NS_BMCR + (reg * 4), data);
		return (0);
	}

	/*
	 * Chipsets < SIS_635 seem not to be able to read/write
	 * through mdio. Use the enhanced PHY access register
	 * again for them.
	 */
	if (sc->sis_type == SIS_TYPE_900 &&
	    sc->sis_rev < SIS_REV_635) {
		int i;

		if (phy != 0)
			return (0);

		CSR_WRITE_4(sc, SIS_PHYCTL, (data << 16) | (phy << 11) |
		    (reg << 6) | SIS_PHYOP_WRITE);
		SIS_SETBIT(sc, SIS_PHYCTL, SIS_PHYCTL_ACCESS);

		for (i = 0; i < SIS_TIMEOUT; i++) {
			if (!(CSR_READ_4(sc, SIS_PHYCTL) & SIS_PHYCTL_ACCESS))
				break;
		}

		if (i == SIS_TIMEOUT)
			device_printf(sc->sis_dev,
			    "PHY failed to come ready\n");
	} else
		mii_bitbang_writereg(dev, &sis_mii_bitbang_ops, phy, reg,
		    data);
	return (0);
}

static void
sis_miibus_statchg(device_t dev)
{
	struct sis_softc	*sc;
	struct mii_data		*mii;
	struct ifnet		*ifp;
	uint32_t		reg;

	sc = device_get_softc(dev);
	SIS_LOCK_ASSERT(sc);

	mii = device_get_softc(sc->sis_miibus);
	ifp = sc->sis_ifp;
	if (mii == NULL || ifp == NULL ||
	    (ifp->if_drv_flags & IFF_DRV_RUNNING) == 0)
		return;

	sc->sis_flags &= ~SIS_FLAG_LINK;
	if ((mii->mii_media_status & (IFM_ACTIVE | IFM_AVALID)) ==
	    (IFM_ACTIVE | IFM_AVALID)) {
		switch (IFM_SUBTYPE(mii->mii_media_active)) {
		case IFM_10_T:
			CSR_WRITE_4(sc, SIS_TX_CFG, SIS_TXCFG_10);
			sc->sis_flags |= SIS_FLAG_LINK;
			break;
		case IFM_100_TX:
			CSR_WRITE_4(sc, SIS_TX_CFG, SIS_TXCFG_100);
			sc->sis_flags |= SIS_FLAG_LINK;
			break;
		default:
			break;
		}
	}

	if ((sc->sis_flags & SIS_FLAG_LINK) == 0) {
		/*
		 * Stopping MACs seem to reset SIS_TX_LISTPTR and
		 * SIS_RX_LISTPTR which in turn requires resetting
		 * TX/RX buffers.  So just don't do anything for
		 * lost link.
		 */
		return;
	}

	/* Set full/half duplex mode. */
	if ((IFM_OPTIONS(mii->mii_media_active) & IFM_FDX) != 0) {
		SIS_SETBIT(sc, SIS_TX_CFG,
		    (SIS_TXCFG_IGN_HBEAT | SIS_TXCFG_IGN_CARR));
		SIS_SETBIT(sc, SIS_RX_CFG, SIS_RXCFG_RX_TXPKTS);
	} else {
		SIS_CLRBIT(sc, SIS_TX_CFG,
		    (SIS_TXCFG_IGN_HBEAT | SIS_TXCFG_IGN_CARR));
		SIS_CLRBIT(sc, SIS_RX_CFG, SIS_RXCFG_RX_TXPKTS);
	}

	if (sc->sis_type == SIS_TYPE_83815 && sc->sis_srr >= NS_SRR_16A) {
		/*
		 * MPII03.D: Half Duplex Excessive Collisions.
		 * Also page 49 in 83816 manual
		 */
		SIS_SETBIT(sc, SIS_TX_CFG, SIS_TXCFG_MPII03D);
	}

	if (sc->sis_type == SIS_TYPE_83815 && sc->sis_srr < NS_SRR_16A &&
	    IFM_SUBTYPE(mii->mii_media_active) == IFM_100_TX) {
		/*
		 * Short Cable Receive Errors (MP21.E)
		 */
		CSR_WRITE_4(sc, NS_PHY_PAGE, 0x0001);
		reg = CSR_READ_4(sc, NS_PHY_DSPCFG) & 0xfff;
		CSR_WRITE_4(sc, NS_PHY_DSPCFG, reg | 0x1000);
		DELAY(100);
		reg = CSR_READ_4(sc, NS_PHY_TDATA) & 0xff;
		if ((reg & 0x0080) == 0 || (reg > 0xd8 && reg <= 0xff)) {
			device_printf(sc->sis_dev,
			    "Applying short cable fix (reg=%x)\n", reg);
			CSR_WRITE_4(sc, NS_PHY_TDATA, 0x00e8);
			SIS_SETBIT(sc, NS_PHY_DSPCFG, 0x20);
		}
		CSR_WRITE_4(sc, NS_PHY_PAGE, 0);
	}
	/* Enable TX/RX MACs. */
	SIS_CLRBIT(sc, SIS_CSR, SIS_CSR_TX_DISABLE | SIS_CSR_RX_DISABLE);
	SIS_SETBIT(sc, SIS_CSR, SIS_CSR_TX_ENABLE | SIS_CSR_RX_ENABLE);
}

static uint32_t
sis_mchash(struct sis_softc *sc, const uint8_t *addr)
{
	uint32_t		crc;

	/* Compute CRC for the address value. */
	crc = ether_crc32_be(addr, ETHER_ADDR_LEN);

	/*
	 * return the filter bit position
	 *
	 * The NatSemi chip has a 512-bit filter, which is
	 * different than the SiS, so we special-case it.
	 */
	if (sc->sis_type == SIS_TYPE_83815)
		return (crc >> 23);
	else if (sc->sis_rev >= SIS_REV_635 ||
	    sc->sis_rev == SIS_REV_900B)
		return (crc >> 24);
	else
		return (crc >> 25);
}

static void
sis_rxfilter(struct sis_softc *sc)
{

	SIS_LOCK_ASSERT(sc);

	if (sc->sis_type == SIS_TYPE_83815)
		sis_rxfilter_ns(sc);
	else
		sis_rxfilter_sis(sc);
}

static void
sis_rxfilter_ns(struct sis_softc *sc)
{
	struct ifnet		*ifp;
	struct ifmultiaddr	*ifma;
	uint32_t		h, i, filter;
	int			bit, index;

	ifp = sc->sis_ifp;
	filter = CSR_READ_4(sc, SIS_RXFILT_CTL);
	if (filter & SIS_RXFILTCTL_ENABLE) {
		/*
		 * Filter should be disabled to program other bits.
		 */
		CSR_WRITE_4(sc, SIS_RXFILT_CTL, filter & ~SIS_RXFILTCTL_ENABLE);
		CSR_READ_4(sc, SIS_RXFILT_CTL);
	}
	filter &= ~(NS_RXFILTCTL_ARP | NS_RXFILTCTL_PERFECT |
	    NS_RXFILTCTL_MCHASH | SIS_RXFILTCTL_ALLPHYS | SIS_RXFILTCTL_BROAD |
	    SIS_RXFILTCTL_ALLMULTI);

	if (ifp->if_flags & IFF_BROADCAST)
		filter |= SIS_RXFILTCTL_BROAD;
	/*
	 * For the NatSemi chip, we have to explicitly enable the
	 * reception of ARP frames, as well as turn on the 'perfect
	 * match' filter where we store the station address, otherwise
	 * we won't receive unicasts meant for this host.
	 */
	filter |= NS_RXFILTCTL_ARP | NS_RXFILTCTL_PERFECT;

	if (ifp->if_flags & (IFF_ALLMULTI | IFF_PROMISC)) {
		filter |= SIS_RXFILTCTL_ALLMULTI;
		if (ifp->if_flags & IFF_PROMISC)
			filter |= SIS_RXFILTCTL_ALLPHYS;
	} else {
		/*
		 * We have to explicitly enable the multicast hash table
		 * on the NatSemi chip if we want to use it, which we do.
		 */
		filter |= NS_RXFILTCTL_MCHASH;

		/* first, zot all the existing hash bits */
		for (i = 0; i < 32; i++) {
			CSR_WRITE_4(sc, SIS_RXFILT_CTL, NS_FILTADDR_FMEM_LO +
			    (i * 2));
			CSR_WRITE_4(sc, SIS_RXFILT_DATA, 0);
		}

		if_maddr_rlock(ifp);
		CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
			if (ifma->ifma_addr->sa_family != AF_LINK)
				continue;
			h = sis_mchash(sc,
			    LLADDR((struct sockaddr_dl *)ifma->ifma_addr));
			index = h >> 3;
			bit = h & 0x1F;
			CSR_WRITE_4(sc, SIS_RXFILT_CTL, NS_FILTADDR_FMEM_LO +
			    index);
			if (bit > 0xF)
				bit -= 0x10;
			SIS_SETBIT(sc, SIS_RXFILT_DATA, (1 << bit));
		}
		if_maddr_runlock(ifp);
	}

	/* Turn the receive filter on */
	CSR_WRITE_4(sc, SIS_RXFILT_CTL, filter | SIS_RXFILTCTL_ENABLE);
	CSR_READ_4(sc, SIS_RXFILT_CTL);
}

static void
sis_rxfilter_sis(struct sis_softc *sc)
{
	struct ifnet		*ifp;
	struct ifmultiaddr	*ifma;
	uint32_t		filter, h, i, n;
	uint16_t		hashes[16];

	ifp = sc->sis_ifp;

	/* hash table size */
	if (sc->sis_rev >= SIS_REV_635 || sc->sis_rev == SIS_REV_900B)
		n = 16;
	else
		n = 8;

	filter = CSR_READ_4(sc, SIS_RXFILT_CTL);
	if (filter & SIS_RXFILTCTL_ENABLE) {
		CSR_WRITE_4(sc, SIS_RXFILT_CTL, filter & ~SIS_RXFILTCTL_ENABLE);
		CSR_READ_4(sc, SIS_RXFILT_CTL);
	}
	filter &= ~(SIS_RXFILTCTL_ALLPHYS | SIS_RXFILTCTL_BROAD |
	    SIS_RXFILTCTL_ALLMULTI);
	if (ifp->if_flags & IFF_BROADCAST)
		filter |= SIS_RXFILTCTL_BROAD;

	if (ifp->if_flags & (IFF_ALLMULTI | IFF_PROMISC)) {
		filter |= SIS_RXFILTCTL_ALLMULTI;
		if (ifp->if_flags & IFF_PROMISC)
			filter |= SIS_RXFILTCTL_ALLPHYS;
		for (i = 0; i < n; i++)
			hashes[i] = ~0;
	} else {
		for (i = 0; i < n; i++)
			hashes[i] = 0;
		i = 0;
		if_maddr_rlock(ifp);
		CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
			if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
			h = sis_mchash(sc,
			    LLADDR((struct sockaddr_dl *)ifma->ifma_addr));
			hashes[h >> 4] |= 1 << (h & 0xf);
			i++;
		}
		if_maddr_runlock(ifp);
		if (i > n) {
			filter |= SIS_RXFILTCTL_ALLMULTI;
			for (i = 0; i < n; i++)
				hashes[i] = ~0;
		}
	}

	for (i = 0; i < n; i++) {
		CSR_WRITE_4(sc, SIS_RXFILT_CTL, (4 + i) << 16);
		CSR_WRITE_4(sc, SIS_RXFILT_DATA, hashes[i]);
	}

	/* Turn the receive filter on */
	CSR_WRITE_4(sc, SIS_RXFILT_CTL, filter | SIS_RXFILTCTL_ENABLE);
	CSR_READ_4(sc, SIS_RXFILT_CTL);
}

static void
sis_reset(struct sis_softc *sc)
{
	int		i;

	SIS_SETBIT(sc, SIS_CSR, SIS_CSR_RESET);

	for (i = 0; i < SIS_TIMEOUT; i++) {
		if (!(CSR_READ_4(sc, SIS_CSR) & SIS_CSR_RESET))
			break;
	}

	if (i == SIS_TIMEOUT)
		device_printf(sc->sis_dev, "reset never completed\n");

	/* Wait a little while for the chip to get its brains in order. */
	DELAY(1000);

	/*
	 * If this is a NetSemi chip, make sure to clear
	 * PME mode.
	 */
	if (sc->sis_type == SIS_TYPE_83815) {
		CSR_WRITE_4(sc, NS_CLKRUN, NS_CLKRUN_PMESTS);
		CSR_WRITE_4(sc, NS_CLKRUN, 0);
	} else {
		/* Disable WOL functions. */
		CSR_WRITE_4(sc, SIS_PWRMAN_CTL, 0);
	}
}

/*
 * Probe for an SiS chip. Check the PCI vendor and device
 * IDs against our list and return a device name if we find a match.
 */
static int
sis_probe(device_t dev)
{
	const struct sis_type	*t;

	t = sis_devs;

	while (t->sis_name != NULL) {
		if ((pci_get_vendor(dev) == t->sis_vid) &&
		    (pci_get_device(dev) == t->sis_did)) {
			device_set_desc(dev, t->sis_name);
			return (BUS_PROBE_DEFAULT);
		}
		t++;
	}

	return (ENXIO);
}

/*
 * Attach the interface. Allocate softc structures, do ifmedia
 * setup and ethernet/BPF attach.
 */
static int
sis_attach(device_t dev)
{
	u_char			eaddr[ETHER_ADDR_LEN];
	struct sis_softc	*sc;
	struct ifnet		*ifp;
	int			error = 0, pmc, waittime = 0;

	waittime = 0;
	sc = device_get_softc(dev);

	sc->sis_dev = dev;

	mtx_init(&sc->sis_mtx, device_get_nameunit(dev), MTX_NETWORK_LOCK,
	    MTX_DEF);
	callout_init_mtx(&sc->sis_stat_ch, &sc->sis_mtx, 0);

	if (pci_get_device(dev) == SIS_DEVICEID_900)
		sc->sis_type = SIS_TYPE_900;
	if (pci_get_device(dev) == SIS_DEVICEID_7016)
		sc->sis_type = SIS_TYPE_7016;
	if (pci_get_vendor(dev) == NS_VENDORID)
		sc->sis_type = SIS_TYPE_83815;

	sc->sis_rev = pci_read_config(dev, PCIR_REVID, 1);
	/*
	 * Map control/status registers.
	 */
	pci_enable_busmaster(dev);

	error = bus_alloc_resources(dev, sis_res_spec, sc->sis_res);
	if (error) {
		device_printf(dev, "couldn't allocate resources\n");
		goto fail;
	}

	/* Reset the adapter. */
	sis_reset(sc);

	if (sc->sis_type == SIS_TYPE_900 &&
	    (sc->sis_rev == SIS_REV_635 ||
	    sc->sis_rev == SIS_REV_900B)) {
		SIO_SET(SIS_CFG_RND_CNT);
		SIO_SET(SIS_CFG_PERR_DETECT);
	}

	/*
	 * Get station address from the EEPROM.
	 */
	switch (pci_get_vendor(dev)) {
	case NS_VENDORID:
		sc->sis_srr = CSR_READ_4(sc, NS_SRR);

		/* We can't update the device description, so spew */
		if (sc->sis_srr == NS_SRR_15C)
			device_printf(dev, "Silicon Revision: DP83815C\n");
		else if (sc->sis_srr == NS_SRR_15D)
			device_printf(dev, "Silicon Revision: DP83815D\n");
		else if (sc->sis_srr == NS_SRR_16A)
			device_printf(dev, "Silicon Revision: DP83816A\n");
		else
			device_printf(dev, "Silicon Revision %x\n", sc->sis_srr);

		/*
		 * Reading the MAC address out of the EEPROM on
		 * the NatSemi chip takes a bit more work than
		 * you'd expect. The address spans 4 16-bit words,
		 * with the first word containing only a single bit.
		 * You have to shift everything over one bit to
		 * get it aligned properly. Also, the bits are
		 * stored backwards (the LSB is really the MSB,
		 * and so on) so you have to reverse them in order
		 * to get the MAC address into the form we want.
		 * Why? Who the hell knows.
		 */
		{
			uint16_t		tmp[4];

			sis_read_eeprom(sc, (caddr_t)&tmp,
			    NS_EE_NODEADDR, 4, 0);

			/* Shift everything over one bit. */
			tmp[3] = tmp[3] >> 1;
			tmp[3] |= tmp[2] << 15;
			tmp[2] = tmp[2] >> 1;
			tmp[2] |= tmp[1] << 15;
			tmp[1] = tmp[1] >> 1;
			tmp[1] |= tmp[0] << 15;

			/* Now reverse all the bits. */
			tmp[3] = sis_reverse(tmp[3]);
			tmp[2] = sis_reverse(tmp[2]);
			tmp[1] = sis_reverse(tmp[1]);

			eaddr[0] = (tmp[1] >> 0) & 0xFF;
			eaddr[1] = (tmp[1] >> 8) & 0xFF;
			eaddr[2] = (tmp[2] >> 0) & 0xFF;
			eaddr[3] = (tmp[2] >> 8) & 0xFF;
			eaddr[4] = (tmp[3] >> 0) & 0xFF;
			eaddr[5] = (tmp[3] >> 8) & 0xFF;
		}
		break;
	case SIS_VENDORID:
	default:
#if defined(__i386__) || defined(__amd64__)
		/*
		 * If this is a SiS 630E chipset with an embedded
		 * SiS 900 controller, we have to read the MAC address
		 * from the APC CMOS RAM. Our method for doing this
		 * is very ugly since we have to reach out and grab
		 * ahold of hardware for which we cannot properly
		 * allocate resources. This code is only compiled on
		 * the i386 architecture since the SiS 630E chipset
		 * is for x86 motherboards only. Note that there are
		 * a lot of magic numbers in this hack. These are
		 * taken from SiS's Linux driver. I'd like to replace
		 * them with proper symbolic definitions, but that
		 * requires some datasheets that I don't have access
		 * to at the moment.
		 */
		if (sc->sis_rev == SIS_REV_630S ||
		    sc->sis_rev == SIS_REV_630E ||
		    sc->sis_rev == SIS_REV_630EA1)
			sis_read_cmos(sc, dev, (caddr_t)&eaddr, 0x9, 6);

		else if (sc->sis_rev == SIS_REV_635 ||
			 sc->sis_rev == SIS_REV_630ET)
			sis_read_mac(sc, dev, (caddr_t)&eaddr);
		else if (sc->sis_rev == SIS_REV_96x) {
			/* Allow to read EEPROM from LAN. It is shared
			 * between a 1394 controller and the NIC and each
			 * time we access it, we need to set SIS_EECMD_REQ.
			 */
			SIO_SET(SIS_EECMD_REQ);
			for (waittime = 0; waittime < SIS_TIMEOUT;
			    waittime++) {
				/* Force EEPROM to idle state. */
				sis_eeprom_idle(sc);
				if (CSR_READ_4(sc, SIS_EECTL) & SIS_EECMD_GNT) {
					sis_read_eeprom(sc, (caddr_t)&eaddr,
					    SIS_EE_NODEADDR, 3, 0);
					break;
				}
				DELAY(1);
			}
			/*
			 * Set SIS_EECTL_CLK to high, so a other master
			 * can operate on the i2c bus.
			 */
			SIO_SET(SIS_EECTL_CLK);
			/* Refuse EEPROM access by LAN */
			SIO_SET(SIS_EECMD_DONE);
		} else
#endif
			sis_read_eeprom(sc, (caddr_t)&eaddr,
			    SIS_EE_NODEADDR, 3, 0);
		break;
	}

	sis_add_sysctls(sc);

	/* Allocate DMA'able memory. */
	if ((error = sis_dma_alloc(sc)) != 0)
		goto fail;

	ifp = sc->sis_ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(dev, "can not if_alloc()\n");
		error = ENOSPC;
		goto fail;
	}
	ifp->if_softc = sc;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = sis_ioctl;
	ifp->if_start = sis_start;
	ifp->if_init = sis_init;
	IFQ_SET_MAXLEN(&ifp->if_snd, SIS_TX_LIST_CNT - 1);
	ifp->if_snd.ifq_drv_maxlen = SIS_TX_LIST_CNT - 1;
	IFQ_SET_READY(&ifp->if_snd);

	if (pci_find_cap(sc->sis_dev, PCIY_PMG, &pmc) == 0) {
		if (sc->sis_type == SIS_TYPE_83815)
			ifp->if_capabilities |= IFCAP_WOL;
		else
			ifp->if_capabilities |= IFCAP_WOL_MAGIC;
		ifp->if_capenable = ifp->if_capabilities;
	}

	/*
	 * Do MII setup.
	 */
	error = mii_attach(dev, &sc->sis_miibus, ifp, sis_ifmedia_upd,
	    sis_ifmedia_sts, BMSR_DEFCAPMASK, MII_PHY_ANY, MII_OFFSET_ANY, 0);
	if (error != 0) {
		device_printf(dev, "attaching PHYs failed\n");
		goto fail;
	}

	/*
	 * Call MI attach routine.
	 */
	ether_ifattach(ifp, eaddr);

	/*
	 * Tell the upper layer(s) we support long frames.
	 */
	ifp->if_hdrlen = sizeof(struct ether_vlan_header);
	ifp->if_capabilities |= IFCAP_VLAN_MTU;
	ifp->if_capenable = ifp->if_capabilities;
#ifdef DEVICE_POLLING
	ifp->if_capabilities |= IFCAP_POLLING;
#endif

	/* Hook interrupt last to avoid having to lock softc */
	error = bus_setup_intr(dev, sc->sis_res[1], INTR_TYPE_NET | INTR_MPSAFE,
	    NULL, sis_intr, sc, &sc->sis_intrhand);

	if (error) {
		device_printf(dev, "couldn't set up irq\n");
		ether_ifdetach(ifp);
		goto fail;
	}

fail:
	if (error)
		sis_detach(dev);

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
sis_detach(device_t dev)
{
	struct sis_softc	*sc;
	struct ifnet		*ifp;

	sc = device_get_softc(dev);
	KASSERT(mtx_initialized(&sc->sis_mtx), ("sis mutex not initialized"));
	ifp = sc->sis_ifp;

#ifdef DEVICE_POLLING
	if (ifp->if_capenable & IFCAP_POLLING)
		ether_poll_deregister(ifp);
#endif

	/* These should only be active if attach succeeded. */
	if (device_is_attached(dev)) {
		SIS_LOCK(sc);
		sis_stop(sc);
		SIS_UNLOCK(sc);
		callout_drain(&sc->sis_stat_ch);
		ether_ifdetach(ifp);
	}
	if (sc->sis_miibus)
		device_delete_child(dev, sc->sis_miibus);
	bus_generic_detach(dev);

	if (sc->sis_intrhand)
		bus_teardown_intr(dev, sc->sis_res[1], sc->sis_intrhand);
	bus_release_resources(dev, sis_res_spec, sc->sis_res);

	if (ifp)
		if_free(ifp);

	sis_dma_free(sc);

	mtx_destroy(&sc->sis_mtx);

	return (0);
}

struct sis_dmamap_arg {
	bus_addr_t	sis_busaddr;
};

static void
sis_dmamap_cb(void *arg, bus_dma_segment_t *segs, int nsegs, int error)
{
	struct sis_dmamap_arg	*ctx;

	if (error != 0)
		return;

	KASSERT(nsegs == 1, ("%s: %d segments returned!", __func__, nsegs));

	ctx = (struct sis_dmamap_arg *)arg;
	ctx->sis_busaddr = segs[0].ds_addr;
}

static int
sis_dma_ring_alloc(struct sis_softc *sc, bus_size_t alignment,
    bus_size_t maxsize, bus_dma_tag_t *tag, uint8_t **ring, bus_dmamap_t *map,
    bus_addr_t *paddr, const char *msg)
{
	struct sis_dmamap_arg	ctx;
	int			error;

	error = bus_dma_tag_create(sc->sis_parent_tag, alignment, 0,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR, NULL, NULL, maxsize, 1,
	    maxsize, 0, NULL, NULL, tag);
	if (error != 0) {
		device_printf(sc->sis_dev,
		    "could not create %s dma tag\n", msg);
		return (ENOMEM);
	}
	/* Allocate DMA'able memory for ring. */
	error = bus_dmamem_alloc(*tag, (void **)ring,
	    BUS_DMA_NOWAIT | BUS_DMA_ZERO | BUS_DMA_COHERENT, map);
	if (error != 0) {
		device_printf(sc->sis_dev,
		    "could not allocate DMA'able memory for %s\n", msg);
		return (ENOMEM);
	}
	/* Load the address of the ring. */
	ctx.sis_busaddr = 0;
	error = bus_dmamap_load(*tag, *map, *ring, maxsize, sis_dmamap_cb,
	    &ctx, BUS_DMA_NOWAIT);
	if (error != 0) {
		device_printf(sc->sis_dev,
		    "could not load DMA'able memory for %s\n", msg);
		return (ENOMEM);
	}
	*paddr = ctx.sis_busaddr;
	return (0);
}

static int
sis_dma_alloc(struct sis_softc *sc)
{
	struct sis_rxdesc	*rxd;
	struct sis_txdesc	*txd;
	int			error, i;

	/* Allocate the parent bus DMA tag appropriate for PCI. */
	error = bus_dma_tag_create(bus_get_dma_tag(sc->sis_dev),
	    1, 0, BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL,
	    NULL, BUS_SPACE_MAXSIZE_32BIT, 0, BUS_SPACE_MAXSIZE_32BIT,
	    0, NULL, NULL, &sc->sis_parent_tag);
	if (error != 0) {
		device_printf(sc->sis_dev,
		    "could not allocate parent dma tag\n");
		return (ENOMEM);
	}

	/* Create RX ring. */
	error = sis_dma_ring_alloc(sc, SIS_DESC_ALIGN, SIS_RX_LIST_SZ,
	    &sc->sis_rx_list_tag, (uint8_t **)&sc->sis_rx_list,
	    &sc->sis_rx_list_map, &sc->sis_rx_paddr, "RX ring");
	if (error)
		return (error);

	/* Create TX ring. */
	error = sis_dma_ring_alloc(sc, SIS_DESC_ALIGN, SIS_TX_LIST_SZ,
	    &sc->sis_tx_list_tag, (uint8_t **)&sc->sis_tx_list,
	    &sc->sis_tx_list_map, &sc->sis_tx_paddr, "TX ring");
	if (error)
		return (error);

	/* Create tag for RX mbufs. */
	error = bus_dma_tag_create(sc->sis_parent_tag, SIS_RX_BUF_ALIGN, 0,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR, NULL, NULL, MCLBYTES, 1,
	    MCLBYTES, 0, NULL, NULL, &sc->sis_rx_tag);
	if (error) {
		device_printf(sc->sis_dev, "could not allocate RX dma tag\n");
		return (error);
	}

	/* Create tag for TX mbufs. */
	error = bus_dma_tag_create(sc->sis_parent_tag, 1, 0,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR, NULL, NULL,
	    MCLBYTES * SIS_MAXTXSEGS, SIS_MAXTXSEGS, MCLBYTES, 0, NULL, NULL,
	    &sc->sis_tx_tag);
	if (error) {
		device_printf(sc->sis_dev, "could not allocate TX dma tag\n");
		return (error);
	}

	/* Create DMA maps for RX buffers. */
	error = bus_dmamap_create(sc->sis_rx_tag, 0, &sc->sis_rx_sparemap);
	if (error) {
		device_printf(sc->sis_dev,
		    "can't create spare DMA map for RX\n");
		return (error);
	}
	for (i = 0; i < SIS_RX_LIST_CNT; i++) {
		rxd = &sc->sis_rxdesc[i];
		rxd->rx_m = NULL;
		error = bus_dmamap_create(sc->sis_rx_tag, 0, &rxd->rx_dmamap);
		if (error) {
			device_printf(sc->sis_dev,
			    "can't create DMA map for RX\n");
			return (error);
		}
	}

	/* Create DMA maps for TX buffers. */
	for (i = 0; i < SIS_TX_LIST_CNT; i++) {
		txd = &sc->sis_txdesc[i];
		txd->tx_m = NULL;
		error = bus_dmamap_create(sc->sis_tx_tag, 0, &txd->tx_dmamap);
		if (error) {
			device_printf(sc->sis_dev,
			    "can't create DMA map for TX\n");
			return (error);
		}
	}

	return (0);
}

static void
sis_dma_free(struct sis_softc *sc)
{
	struct sis_rxdesc	*rxd;
	struct sis_txdesc	*txd;
	int			i;

	/* Destroy DMA maps for RX buffers. */
	for (i = 0; i < SIS_RX_LIST_CNT; i++) {
		rxd = &sc->sis_rxdesc[i];
		if (rxd->rx_dmamap)
			bus_dmamap_destroy(sc->sis_rx_tag, rxd->rx_dmamap);
	}
	if (sc->sis_rx_sparemap)
		bus_dmamap_destroy(sc->sis_rx_tag, sc->sis_rx_sparemap);

	/* Destroy DMA maps for TX buffers. */
	for (i = 0; i < SIS_TX_LIST_CNT; i++) {
		txd = &sc->sis_txdesc[i];
		if (txd->tx_dmamap)
			bus_dmamap_destroy(sc->sis_tx_tag, txd->tx_dmamap);
	}

	if (sc->sis_rx_tag)
		bus_dma_tag_destroy(sc->sis_rx_tag);
	if (sc->sis_tx_tag)
		bus_dma_tag_destroy(sc->sis_tx_tag);

	/* Destroy RX ring. */
	if (sc->sis_rx_paddr)
		bus_dmamap_unload(sc->sis_rx_list_tag, sc->sis_rx_list_map);
	if (sc->sis_rx_list)
		bus_dmamem_free(sc->sis_rx_list_tag, sc->sis_rx_list,
		    sc->sis_rx_list_map);

	if (sc->sis_rx_list_tag)
		bus_dma_tag_destroy(sc->sis_rx_list_tag);

	/* Destroy TX ring. */
	if (sc->sis_tx_paddr)
		bus_dmamap_unload(sc->sis_tx_list_tag, sc->sis_tx_list_map);

	if (sc->sis_tx_list)
		bus_dmamem_free(sc->sis_tx_list_tag, sc->sis_tx_list,
		    sc->sis_tx_list_map);

	if (sc->sis_tx_list_tag)
		bus_dma_tag_destroy(sc->sis_tx_list_tag);

	/* Destroy the parent tag. */
	if (sc->sis_parent_tag)
		bus_dma_tag_destroy(sc->sis_parent_tag);
}

/*
 * Initialize the TX and RX descriptors and allocate mbufs for them. Note that
 * we arrange the descriptors in a closed ring, so that the last descriptor
 * points back to the first.
 */
static int
sis_ring_init(struct sis_softc *sc)
{
	struct sis_rxdesc	*rxd;
	struct sis_txdesc	*txd;
	bus_addr_t		next;
	int			error, i;

	bzero(&sc->sis_tx_list[0], SIS_TX_LIST_SZ);
	for (i = 0; i < SIS_TX_LIST_CNT; i++) {
		txd = &sc->sis_txdesc[i];
		txd->tx_m = NULL;
		if (i == SIS_TX_LIST_CNT - 1)
			next = SIS_TX_RING_ADDR(sc, 0);
		else
			next = SIS_TX_RING_ADDR(sc, i + 1);
		sc->sis_tx_list[i].sis_next = htole32(SIS_ADDR_LO(next));
	}
	sc->sis_tx_prod = sc->sis_tx_cons = sc->sis_tx_cnt = 0;
	bus_dmamap_sync(sc->sis_tx_list_tag, sc->sis_tx_list_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	sc->sis_rx_cons = 0;
	bzero(&sc->sis_rx_list[0], SIS_RX_LIST_SZ);
	for (i = 0; i < SIS_RX_LIST_CNT; i++) {
		rxd = &sc->sis_rxdesc[i];
		rxd->rx_desc = &sc->sis_rx_list[i];
		if (i == SIS_RX_LIST_CNT - 1)
			next = SIS_RX_RING_ADDR(sc, 0);
		else
			next = SIS_RX_RING_ADDR(sc, i + 1);
		rxd->rx_desc->sis_next = htole32(SIS_ADDR_LO(next));
		error = sis_newbuf(sc, rxd);
		if (error)
			return (error);
	}
	bus_dmamap_sync(sc->sis_rx_list_tag, sc->sis_rx_list_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	return (0);
}

/*
 * Initialize an RX descriptor and attach an MBUF cluster.
 */
static int
sis_newbuf(struct sis_softc *sc, struct sis_rxdesc *rxd)
{
	struct mbuf		*m;
	bus_dma_segment_t	segs[1];
	bus_dmamap_t		map;
	int nsegs;

	m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
	if (m == NULL)
		return (ENOBUFS);
	m->m_len = m->m_pkthdr.len = SIS_RXLEN;
#ifndef __NO_STRICT_ALIGNMENT
	m_adj(m, SIS_RX_BUF_ALIGN);
#endif

	if (bus_dmamap_load_mbuf_sg(sc->sis_rx_tag, sc->sis_rx_sparemap, m,
	    segs, &nsegs, 0) != 0) {
		m_freem(m);
		return (ENOBUFS);
	}
	KASSERT(nsegs == 1, ("%s: %d segments returned!", __func__, nsegs));

	if (rxd->rx_m != NULL) {
		bus_dmamap_sync(sc->sis_rx_tag, rxd->rx_dmamap,
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->sis_rx_tag, rxd->rx_dmamap);
	}
	map = rxd->rx_dmamap;
	rxd->rx_dmamap = sc->sis_rx_sparemap;
	sc->sis_rx_sparemap = map;
	bus_dmamap_sync(sc->sis_rx_tag, rxd->rx_dmamap, BUS_DMASYNC_PREREAD);
	rxd->rx_m = m;
	rxd->rx_desc->sis_ptr = htole32(SIS_ADDR_LO(segs[0].ds_addr));
	rxd->rx_desc->sis_cmdsts = htole32(SIS_RXLEN);
	return (0);
}

static __inline void
sis_discard_rxbuf(struct sis_rxdesc *rxd)
{

	rxd->rx_desc->sis_cmdsts = htole32(SIS_RXLEN);
}

#ifndef __NO_STRICT_ALIGNMENT
static __inline void
sis_fixup_rx(struct mbuf *m)
{
	uint16_t		*src, *dst;
	int			i;

	src = mtod(m, uint16_t *);
	dst = src - (SIS_RX_BUF_ALIGN - ETHER_ALIGN) / sizeof(*src);

	for (i = 0; i < (m->m_len / sizeof(uint16_t) + 1); i++)
		*dst++ = *src++;

	m->m_data -= SIS_RX_BUF_ALIGN - ETHER_ALIGN;
}
#endif

/*
 * A frame has been uploaded: pass the resulting mbuf chain up to
 * the higher level protocols.
 */
static int
sis_rxeof(struct sis_softc *sc)
{
	struct mbuf		*m;
	struct ifnet		*ifp;
	struct sis_rxdesc	*rxd;
	struct sis_desc		*cur_rx;
	int			prog, rx_cons, rx_npkts = 0, total_len;
	uint32_t		rxstat;

	SIS_LOCK_ASSERT(sc);

	bus_dmamap_sync(sc->sis_rx_list_tag, sc->sis_rx_list_map,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	rx_cons = sc->sis_rx_cons;
	ifp = sc->sis_ifp;

	for (prog = 0; (ifp->if_drv_flags & IFF_DRV_RUNNING) != 0;
	    SIS_INC(rx_cons, SIS_RX_LIST_CNT), prog++) {
#ifdef DEVICE_POLLING
		if (ifp->if_capenable & IFCAP_POLLING) {
			if (sc->rxcycles <= 0)
				break;
			sc->rxcycles--;
		}
#endif
		cur_rx = &sc->sis_rx_list[rx_cons];
		rxstat = le32toh(cur_rx->sis_cmdsts);
		if ((rxstat & SIS_CMDSTS_OWN) == 0)
			break;
		rxd = &sc->sis_rxdesc[rx_cons];

		total_len = (rxstat & SIS_CMDSTS_BUFLEN) - ETHER_CRC_LEN;
		if ((ifp->if_capenable & IFCAP_VLAN_MTU) != 0 &&
		    total_len <= (ETHER_MAX_LEN + ETHER_VLAN_ENCAP_LEN -
		    ETHER_CRC_LEN))
			rxstat &= ~SIS_RXSTAT_GIANT;
		if (SIS_RXSTAT_ERROR(rxstat) != 0) {
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			if (rxstat & SIS_RXSTAT_COLL)
				if_inc_counter(ifp, IFCOUNTER_COLLISIONS, 1);
			sis_discard_rxbuf(rxd);
			continue;
		}

		/* Add a new receive buffer to the ring. */
		m = rxd->rx_m;
		if (sis_newbuf(sc, rxd) != 0) {
			if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
			sis_discard_rxbuf(rxd);
			continue;
		}

		/* No errors; receive the packet. */
		m->m_pkthdr.len = m->m_len = total_len;
#ifndef __NO_STRICT_ALIGNMENT
		/*
		 * On architectures without alignment problems we try to
		 * allocate a new buffer for the receive ring, and pass up
		 * the one where the packet is already, saving the expensive
		 * copy operation.
		 */
		sis_fixup_rx(m);
#endif
		if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
		m->m_pkthdr.rcvif = ifp;

		SIS_UNLOCK(sc);
		(*ifp->if_input)(ifp, m);
		SIS_LOCK(sc);
		rx_npkts++;
	}

	if (prog > 0) {
		sc->sis_rx_cons = rx_cons;
		bus_dmamap_sync(sc->sis_rx_list_tag, sc->sis_rx_list_map,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	}

	return (rx_npkts);
}

/*
 * A frame was downloaded to the chip. It's safe for us to clean up
 * the list buffers.
 */

static void
sis_txeof(struct sis_softc *sc)
{
	struct ifnet		*ifp;
	struct sis_desc		*cur_tx;
	struct sis_txdesc	*txd;
	uint32_t		cons, txstat;

	SIS_LOCK_ASSERT(sc);

	cons = sc->sis_tx_cons;
	if (cons == sc->sis_tx_prod)
		return;

	ifp = sc->sis_ifp;
	bus_dmamap_sync(sc->sis_tx_list_tag, sc->sis_tx_list_map,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	/*
	 * Go through our tx list and free mbufs for those
	 * frames that have been transmitted.
	 */
	for (; cons != sc->sis_tx_prod; SIS_INC(cons, SIS_TX_LIST_CNT)) {
		cur_tx = &sc->sis_tx_list[cons];
		txstat = le32toh(cur_tx->sis_cmdsts);
		if ((txstat & SIS_CMDSTS_OWN) != 0)
			break;
		txd = &sc->sis_txdesc[cons];
		if (txd->tx_m != NULL) {
			bus_dmamap_sync(sc->sis_tx_tag, txd->tx_dmamap,
			    BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sis_tx_tag, txd->tx_dmamap);
			m_freem(txd->tx_m);
			txd->tx_m = NULL;
			if ((txstat & SIS_CMDSTS_PKT_OK) != 0) {
				if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
				if_inc_counter(ifp, IFCOUNTER_COLLISIONS,
				    (txstat & SIS_TXSTAT_COLLCNT) >> 16);
			} else {
				if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
				if (txstat & SIS_TXSTAT_EXCESSCOLLS)
					if_inc_counter(ifp, IFCOUNTER_COLLISIONS, 1);
				if (txstat & SIS_TXSTAT_OUTOFWINCOLL)
					if_inc_counter(ifp, IFCOUNTER_COLLISIONS, 1);
			}
		}
		sc->sis_tx_cnt--;
		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
	}
	sc->sis_tx_cons = cons;
	if (sc->sis_tx_cnt == 0)
		sc->sis_watchdog_timer = 0;
}

static void
sis_tick(void *xsc)
{
	struct sis_softc	*sc;
	struct mii_data		*mii;

	sc = xsc;
	SIS_LOCK_ASSERT(sc);

	mii = device_get_softc(sc->sis_miibus);
	mii_tick(mii);
	sis_watchdog(sc);
	if ((sc->sis_flags & SIS_FLAG_LINK) == 0)
		sis_miibus_statchg(sc->sis_dev);
	callout_reset(&sc->sis_stat_ch, hz,  sis_tick, sc);
}

#ifdef DEVICE_POLLING
static poll_handler_t sis_poll;

static int
sis_poll(struct ifnet *ifp, enum poll_cmd cmd, int count)
{
	struct	sis_softc *sc = ifp->if_softc;
	int rx_npkts = 0;

	SIS_LOCK(sc);
	if (!(ifp->if_drv_flags & IFF_DRV_RUNNING)) {
		SIS_UNLOCK(sc);
		return (rx_npkts);
	}

	/*
	 * On the sis, reading the status register also clears it.
	 * So before returning to intr mode we must make sure that all
	 * possible pending sources of interrupts have been served.
	 * In practice this means run to completion the *eof routines,
	 * and then call the interrupt routine
	 */
	sc->rxcycles = count;
	rx_npkts = sis_rxeof(sc);
	sis_txeof(sc);
	if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd))
		sis_startl(ifp);

	if (sc->rxcycles > 0 || cmd == POLL_AND_CHECK_STATUS) {
		uint32_t	status;

		/* Reading the ISR register clears all interrupts. */
		status = CSR_READ_4(sc, SIS_ISR);

		if (status & (SIS_ISR_RX_ERR|SIS_ISR_RX_OFLOW))
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);

		if (status & (SIS_ISR_RX_IDLE))
			SIS_SETBIT(sc, SIS_CSR, SIS_CSR_RX_ENABLE);

		if (status & SIS_ISR_SYSERR) {
			ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
			sis_initl(sc);
		}
	}

	SIS_UNLOCK(sc);
	return (rx_npkts);
}
#endif /* DEVICE_POLLING */

static void
sis_intr(void *arg)
{
	struct sis_softc	*sc;
	struct ifnet		*ifp;
	uint32_t		status;

	sc = arg;
	ifp = sc->sis_ifp;

	SIS_LOCK(sc);
#ifdef DEVICE_POLLING
	if (ifp->if_capenable & IFCAP_POLLING) {
		SIS_UNLOCK(sc);
		return;
	}
#endif

	/* Reading the ISR register clears all interrupts. */
	status = CSR_READ_4(sc, SIS_ISR);
	if ((status & SIS_INTRS) == 0) {
		/* Not ours. */
		SIS_UNLOCK(sc);
		return;
	}

	/* Disable interrupts. */
	CSR_WRITE_4(sc, SIS_IER, 0);

	for (;(status & SIS_INTRS) != 0;) {
		if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0)
			break;
		if (status &
		    (SIS_ISR_TX_DESC_OK | SIS_ISR_TX_ERR |
		    SIS_ISR_TX_OK | SIS_ISR_TX_IDLE) )
			sis_txeof(sc);

		if (status & (SIS_ISR_RX_DESC_OK | SIS_ISR_RX_OK |
		    SIS_ISR_RX_ERR | SIS_ISR_RX_IDLE))
			sis_rxeof(sc);

		if (status & SIS_ISR_RX_OFLOW)
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);

		if (status & (SIS_ISR_RX_IDLE))
			SIS_SETBIT(sc, SIS_CSR, SIS_CSR_RX_ENABLE);

		if (status & SIS_ISR_SYSERR) {
			ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
			sis_initl(sc);
			SIS_UNLOCK(sc);
			return;
		}
		status = CSR_READ_4(sc, SIS_ISR);
	}

	if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
		/* Re-enable interrupts. */
		CSR_WRITE_4(sc, SIS_IER, 1);

		if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd))
			sis_startl(ifp);
	}

	SIS_UNLOCK(sc);
}

/*
 * Encapsulate an mbuf chain in a descriptor by coupling the mbuf data
 * pointers to the fragment pointers.
 */
static int
sis_encap(struct sis_softc *sc, struct mbuf **m_head)
{
	struct mbuf		*m;
	struct sis_txdesc	*txd;
	struct sis_desc		*f;
	bus_dma_segment_t	segs[SIS_MAXTXSEGS];
	bus_dmamap_t		map;
	int			error, i, frag, nsegs, prod;
	int			padlen;

	prod = sc->sis_tx_prod;
	txd = &sc->sis_txdesc[prod];
	if ((sc->sis_flags & SIS_FLAG_MANUAL_PAD) != 0 &&
	    (*m_head)->m_pkthdr.len < SIS_MIN_FRAMELEN) {
		m = *m_head;
		padlen = SIS_MIN_FRAMELEN - m->m_pkthdr.len;
		if (M_WRITABLE(m) == 0) {
			/* Get a writable copy. */
			m = m_dup(*m_head, M_NOWAIT);
			m_freem(*m_head);
			if (m == NULL) {
				*m_head = NULL;
				return (ENOBUFS);
			}
			*m_head = m;
		}
		if (m->m_next != NULL || M_TRAILINGSPACE(m) < padlen) {
			m = m_defrag(m, M_NOWAIT);
			if (m == NULL) {
				m_freem(*m_head);
				*m_head = NULL;
				return (ENOBUFS);
			}
		}
		/*
		 * Manually pad short frames, and zero the pad space
		 * to avoid leaking data.
		 */
		bzero(mtod(m, char *) + m->m_pkthdr.len, padlen);
		m->m_pkthdr.len += padlen;
		m->m_len = m->m_pkthdr.len;
		*m_head = m;
	}
	error = bus_dmamap_load_mbuf_sg(sc->sis_tx_tag, txd->tx_dmamap,
	    *m_head, segs, &nsegs, 0);
	if (error == EFBIG) {
		m = m_collapse(*m_head, M_NOWAIT, SIS_MAXTXSEGS);
		if (m == NULL) {
			m_freem(*m_head);
			*m_head = NULL;
			return (ENOBUFS);
		}
		*m_head = m;
		error = bus_dmamap_load_mbuf_sg(sc->sis_tx_tag, txd->tx_dmamap,
		    *m_head, segs, &nsegs, 0);
		if (error != 0) {
			m_freem(*m_head);
			*m_head = NULL;
			return (error);
		}
	} else if (error != 0)
		return (error);

	/* Check for descriptor overruns. */
	if (sc->sis_tx_cnt + nsegs > SIS_TX_LIST_CNT - 1) {
		bus_dmamap_unload(sc->sis_tx_tag, txd->tx_dmamap);
		return (ENOBUFS);
	}

	bus_dmamap_sync(sc->sis_tx_tag, txd->tx_dmamap, BUS_DMASYNC_PREWRITE);

	frag = prod;
	for (i = 0; i < nsegs; i++) {
		f = &sc->sis_tx_list[prod];
		if (i == 0)
			f->sis_cmdsts = htole32(segs[i].ds_len |
			    SIS_CMDSTS_MORE);
		else
			f->sis_cmdsts = htole32(segs[i].ds_len |
			    SIS_CMDSTS_OWN | SIS_CMDSTS_MORE);
		f->sis_ptr = htole32(SIS_ADDR_LO(segs[i].ds_addr));
		SIS_INC(prod, SIS_TX_LIST_CNT);
		sc->sis_tx_cnt++;
	}

	/* Update producer index. */
	sc->sis_tx_prod = prod;

	/* Remove MORE flag on the last descriptor. */
	prod = (prod - 1) & (SIS_TX_LIST_CNT - 1);
	f = &sc->sis_tx_list[prod];
	f->sis_cmdsts &= ~htole32(SIS_CMDSTS_MORE);

	/* Lastly transfer ownership of packet to the controller. */
	f = &sc->sis_tx_list[frag];
	f->sis_cmdsts |= htole32(SIS_CMDSTS_OWN);

	/* Swap the last and the first dmamaps. */
	map = txd->tx_dmamap;
	txd->tx_dmamap = sc->sis_txdesc[prod].tx_dmamap;
	sc->sis_txdesc[prod].tx_dmamap = map;
	sc->sis_txdesc[prod].tx_m = *m_head;

	return (0);
}

static void
sis_start(struct ifnet *ifp)
{
	struct sis_softc	*sc;

	sc = ifp->if_softc;
	SIS_LOCK(sc);
	sis_startl(ifp);
	SIS_UNLOCK(sc);
}

static void
sis_startl(struct ifnet *ifp)
{
	struct sis_softc	*sc;
	struct mbuf		*m_head;
	int			queued;

	sc = ifp->if_softc;

	SIS_LOCK_ASSERT(sc);

	if ((ifp->if_drv_flags & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) !=
	    IFF_DRV_RUNNING || (sc->sis_flags & SIS_FLAG_LINK) == 0)
		return;

	for (queued = 0; !IFQ_DRV_IS_EMPTY(&ifp->if_snd) &&
	    sc->sis_tx_cnt < SIS_TX_LIST_CNT - 4;) {
		IFQ_DRV_DEQUEUE(&ifp->if_snd, m_head);
		if (m_head == NULL)
			break;

		if (sis_encap(sc, &m_head) != 0) {
			if (m_head == NULL)
				break;
			IFQ_DRV_PREPEND(&ifp->if_snd, m_head);
			ifp->if_drv_flags |= IFF_DRV_OACTIVE;
			break;
		}

		queued++;

		/*
		 * If there's a BPF listener, bounce a copy of this frame
		 * to him.
		 */
		BPF_MTAP(ifp, m_head);
	}

	if (queued) {
		/* Transmit */
		bus_dmamap_sync(sc->sis_tx_list_tag, sc->sis_tx_list_map,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
		SIS_SETBIT(sc, SIS_CSR, SIS_CSR_TX_ENABLE);

		/*
		 * Set a timeout in case the chip goes out to lunch.
		 */
		sc->sis_watchdog_timer = 5;
	}
}

static void
sis_init(void *xsc)
{
	struct sis_softc	*sc = xsc;

	SIS_LOCK(sc);
	sis_initl(sc);
	SIS_UNLOCK(sc);
}

static void
sis_initl(struct sis_softc *sc)
{
	struct ifnet		*ifp = sc->sis_ifp;
	struct mii_data		*mii;
	uint8_t			*eaddr;

	SIS_LOCK_ASSERT(sc);

	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
		return;

	/*
	 * Cancel pending I/O and free all RX/TX buffers.
	 */
	sis_stop(sc);
	/*
	 * Reset the chip to a known state.
	 */
	sis_reset(sc);
#ifdef notyet
	if (sc->sis_type == SIS_TYPE_83815 && sc->sis_srr >= NS_SRR_16A) {
		/*
		 * Configure 400usec of interrupt holdoff.  This is based
		 * on emperical tests on a Soekris 4801.
 		 */
		CSR_WRITE_4(sc, NS_IHR, 0x100 | 4);
	}
#endif

	mii = device_get_softc(sc->sis_miibus);

	/* Set MAC address */
	eaddr = IF_LLADDR(sc->sis_ifp);
	if (sc->sis_type == SIS_TYPE_83815) {
		CSR_WRITE_4(sc, SIS_RXFILT_CTL, NS_FILTADDR_PAR0);
		CSR_WRITE_4(sc, SIS_RXFILT_DATA, eaddr[0] | eaddr[1] << 8);
		CSR_WRITE_4(sc, SIS_RXFILT_CTL, NS_FILTADDR_PAR1);
		CSR_WRITE_4(sc, SIS_RXFILT_DATA, eaddr[2] | eaddr[3] << 8);
		CSR_WRITE_4(sc, SIS_RXFILT_CTL, NS_FILTADDR_PAR2);
		CSR_WRITE_4(sc, SIS_RXFILT_DATA, eaddr[4] | eaddr[5] << 8);
	} else {
		CSR_WRITE_4(sc, SIS_RXFILT_CTL, SIS_FILTADDR_PAR0);
		CSR_WRITE_4(sc, SIS_RXFILT_DATA, eaddr[0] | eaddr[1] << 8);
		CSR_WRITE_4(sc, SIS_RXFILT_CTL, SIS_FILTADDR_PAR1);
		CSR_WRITE_4(sc, SIS_RXFILT_DATA, eaddr[2] | eaddr[3] << 8);
		CSR_WRITE_4(sc, SIS_RXFILT_CTL, SIS_FILTADDR_PAR2);
		CSR_WRITE_4(sc, SIS_RXFILT_DATA, eaddr[4] | eaddr[5] << 8);
	}

	/* Init circular TX/RX lists. */
	if (sis_ring_init(sc) != 0) {
		device_printf(sc->sis_dev,
		    "initialization failed: no memory for rx buffers\n");
		sis_stop(sc);
		return;
	}

	if (sc->sis_type == SIS_TYPE_83815) {
		if (sc->sis_manual_pad != 0)
			sc->sis_flags |= SIS_FLAG_MANUAL_PAD;
		else
			sc->sis_flags &= ~SIS_FLAG_MANUAL_PAD;
	}

	/*
	 * Short Cable Receive Errors (MP21.E)
	 * also: Page 78 of the DP83815 data sheet (september 2002 version)
	 * recommends the following register settings "for optimum
	 * performance." for rev 15C.  Set this also for 15D parts as
	 * they require it in practice.
	 */
	if (sc->sis_type == SIS_TYPE_83815 && sc->sis_srr <= NS_SRR_15D) {
		CSR_WRITE_4(sc, NS_PHY_PAGE, 0x0001);
		CSR_WRITE_4(sc, NS_PHY_CR, 0x189C);
		/* set val for c2 */
		CSR_WRITE_4(sc, NS_PHY_TDATA, 0x0000);
		/* load/kill c2 */
		CSR_WRITE_4(sc, NS_PHY_DSPCFG, 0x5040);
		/* rais SD off, from 4 to c */
		CSR_WRITE_4(sc, NS_PHY_SDCFG, 0x008C);
		CSR_WRITE_4(sc, NS_PHY_PAGE, 0);
	}

	sis_rxfilter(sc);

	/*
	 * Load the address of the RX and TX lists.
	 */
	CSR_WRITE_4(sc, SIS_RX_LISTPTR, SIS_ADDR_LO(sc->sis_rx_paddr));
	CSR_WRITE_4(sc, SIS_TX_LISTPTR, SIS_ADDR_LO(sc->sis_tx_paddr));

	/* SIS_CFG_EDB_MASTER_EN indicates the EDB bus is used instead of
	 * the PCI bus. When this bit is set, the Max DMA Burst Size
	 * for TX/RX DMA should be no larger than 16 double words.
	 */
	if (CSR_READ_4(sc, SIS_CFG) & SIS_CFG_EDB_MASTER_EN) {
		CSR_WRITE_4(sc, SIS_RX_CFG, SIS_RXCFG64);
	} else {
		CSR_WRITE_4(sc, SIS_RX_CFG, SIS_RXCFG256);
	}

	/* Accept Long Packets for VLAN support */
	SIS_SETBIT(sc, SIS_RX_CFG, SIS_RXCFG_RX_JABBER);

	/*
	 * Assume 100Mbps link, actual MAC configuration is done
	 * after getting a valid link.
	 */
	CSR_WRITE_4(sc, SIS_TX_CFG, SIS_TXCFG_100);

	/*
	 * Enable interrupts.
	 */
	CSR_WRITE_4(sc, SIS_IMR, SIS_INTRS);
#ifdef DEVICE_POLLING
	/*
	 * ... only enable interrupts if we are not polling, make sure
	 * they are off otherwise.
	 */
	if (ifp->if_capenable & IFCAP_POLLING)
		CSR_WRITE_4(sc, SIS_IER, 0);
	else
#endif
	CSR_WRITE_4(sc, SIS_IER, 1);

	/* Clear MAC disable. */
	SIS_CLRBIT(sc, SIS_CSR, SIS_CSR_TX_DISABLE | SIS_CSR_RX_DISABLE);

	sc->sis_flags &= ~SIS_FLAG_LINK;
	mii_mediachg(mii);

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

	callout_reset(&sc->sis_stat_ch, hz,  sis_tick, sc);
}

/*
 * Set media options.
 */
static int
sis_ifmedia_upd(struct ifnet *ifp)
{
	struct sis_softc	*sc;
	struct mii_data		*mii;
	struct mii_softc	*miisc;
	int			error;

	sc = ifp->if_softc;

	SIS_LOCK(sc);
	mii = device_get_softc(sc->sis_miibus);
	LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
		PHY_RESET(miisc);
	error = mii_mediachg(mii);
	SIS_UNLOCK(sc);

	return (error);
}

/*
 * Report current media status.
 */
static void
sis_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct sis_softc	*sc;
	struct mii_data		*mii;

	sc = ifp->if_softc;

	SIS_LOCK(sc);
	mii = device_get_softc(sc->sis_miibus);
	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
	SIS_UNLOCK(sc);
}

static int
sis_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct sis_softc	*sc = ifp->if_softc;
	struct ifreq		*ifr = (struct ifreq *) data;
	struct mii_data		*mii;
	int			error = 0, mask;

	switch (command) {
	case SIOCSIFFLAGS:
		SIS_LOCK(sc);
		if (ifp->if_flags & IFF_UP) {
			if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0 &&
			    ((ifp->if_flags ^ sc->sis_if_flags) &
			    (IFF_PROMISC | IFF_ALLMULTI)) != 0)
				sis_rxfilter(sc);
			else
				sis_initl(sc);
		} else if (ifp->if_drv_flags & IFF_DRV_RUNNING)
			sis_stop(sc);
		sc->sis_if_flags = ifp->if_flags;
		SIS_UNLOCK(sc);
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		SIS_LOCK(sc);
		if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
			sis_rxfilter(sc);
		SIS_UNLOCK(sc);
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		mii = device_get_softc(sc->sis_miibus);
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, command);
		break;
	case SIOCSIFCAP:
		SIS_LOCK(sc);
		mask = ifr->ifr_reqcap ^ ifp->if_capenable;
#ifdef DEVICE_POLLING
		if ((mask & IFCAP_POLLING) != 0 &&
		    (IFCAP_POLLING & ifp->if_capabilities) != 0) {
			ifp->if_capenable ^= IFCAP_POLLING;
			if ((IFCAP_POLLING & ifp->if_capenable) != 0) {
				error = ether_poll_register(sis_poll, ifp);
				if (error != 0) {
					SIS_UNLOCK(sc);
					break;
				}
				/* Disable interrupts. */
				CSR_WRITE_4(sc, SIS_IER, 0);
                        } else {
                                error = ether_poll_deregister(ifp);
                                /* Enable interrupts. */
				CSR_WRITE_4(sc, SIS_IER, 1);
                        }
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
		SIS_UNLOCK(sc);
		break;
	default:
		error = ether_ioctl(ifp, command, data);
		break;
	}

	return (error);
}

static void
sis_watchdog(struct sis_softc *sc)
{

	SIS_LOCK_ASSERT(sc);

	if (sc->sis_watchdog_timer == 0 || --sc->sis_watchdog_timer >0)
		return;

	device_printf(sc->sis_dev, "watchdog timeout\n");
	if_inc_counter(sc->sis_ifp, IFCOUNTER_OERRORS, 1);

	sc->sis_ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
	sis_initl(sc);

	if (!IFQ_DRV_IS_EMPTY(&sc->sis_ifp->if_snd))
		sis_startl(sc->sis_ifp);
}

/*
 * Stop the adapter and free any mbufs allocated to the
 * RX and TX lists.
 */
static void
sis_stop(struct sis_softc *sc)
{
	struct ifnet *ifp;
	struct sis_rxdesc *rxd;
	struct sis_txdesc *txd;
	int i;

	SIS_LOCK_ASSERT(sc);

	ifp = sc->sis_ifp;
	sc->sis_watchdog_timer = 0;

	callout_stop(&sc->sis_stat_ch);

	ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);
	CSR_WRITE_4(sc, SIS_IER, 0);
	CSR_WRITE_4(sc, SIS_IMR, 0);
	CSR_READ_4(sc, SIS_ISR); /* clear any interrupts already pending */
	SIS_SETBIT(sc, SIS_CSR, SIS_CSR_TX_DISABLE|SIS_CSR_RX_DISABLE);
	DELAY(1000);
	CSR_WRITE_4(sc, SIS_TX_LISTPTR, 0);
	CSR_WRITE_4(sc, SIS_RX_LISTPTR, 0);

	sc->sis_flags &= ~SIS_FLAG_LINK;

	/*
	 * Free data in the RX lists.
	 */
	for (i = 0; i < SIS_RX_LIST_CNT; i++) {
		rxd = &sc->sis_rxdesc[i];
		if (rxd->rx_m != NULL) {
			bus_dmamap_sync(sc->sis_rx_tag, rxd->rx_dmamap,
			    BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(sc->sis_rx_tag, rxd->rx_dmamap);
			m_freem(rxd->rx_m);
			rxd->rx_m = NULL;
		}
	}

	/*
	 * Free the TX list buffers.
	 */
	for (i = 0; i < SIS_TX_LIST_CNT; i++) {
		txd = &sc->sis_txdesc[i];
		if (txd->tx_m != NULL) {
			bus_dmamap_sync(sc->sis_tx_tag, txd->tx_dmamap,
			    BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sis_tx_tag, txd->tx_dmamap);
			m_freem(txd->tx_m);
			txd->tx_m = NULL;
		}
	}
}

/*
 * Stop all chip I/O so that the kernel's probe routines don't
 * get confused by errant DMAs when rebooting.
 */
static int
sis_shutdown(device_t dev)
{

	return (sis_suspend(dev));
}

static int
sis_suspend(device_t dev)
{
	struct sis_softc	*sc;

	sc = device_get_softc(dev);
	SIS_LOCK(sc);
	sis_stop(sc);
	sis_wol(sc);
	SIS_UNLOCK(sc);
	return (0);
}

static int
sis_resume(device_t dev)
{
	struct sis_softc	*sc;
	struct ifnet		*ifp;

	sc = device_get_softc(dev);
	SIS_LOCK(sc);
	ifp = sc->sis_ifp;
	if ((ifp->if_flags & IFF_UP) != 0) {
		ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
		sis_initl(sc);
	}
	SIS_UNLOCK(sc);
	return (0);
}

static void
sis_wol(struct sis_softc *sc)
{
	struct ifnet		*ifp;
	uint32_t		val;
	uint16_t		pmstat;
	int			pmc;

	ifp = sc->sis_ifp;
	if ((ifp->if_capenable & IFCAP_WOL) == 0)
		return;

	if (sc->sis_type == SIS_TYPE_83815) {
		/* Reset RXDP. */
		CSR_WRITE_4(sc, SIS_RX_LISTPTR, 0);

		/* Configure WOL events. */
		CSR_READ_4(sc, NS_WCSR);
		val = 0;
		if ((ifp->if_capenable & IFCAP_WOL_UCAST) != 0)
			val |= NS_WCSR_WAKE_UCAST;
		if ((ifp->if_capenable & IFCAP_WOL_MCAST) != 0)
			val |= NS_WCSR_WAKE_MCAST;
		if ((ifp->if_capenable & IFCAP_WOL_MAGIC) != 0)
			val |= NS_WCSR_WAKE_MAGIC;
		CSR_WRITE_4(sc, NS_WCSR, val);
		/* Enable PME and clear PMESTS. */
		val = CSR_READ_4(sc, NS_CLKRUN);
		val |= NS_CLKRUN_PMEENB | NS_CLKRUN_PMESTS;
		CSR_WRITE_4(sc, NS_CLKRUN, val);
		/* Enable silent RX mode. */
		SIS_SETBIT(sc, SIS_CSR, SIS_CSR_RX_ENABLE);
	} else {
		if (pci_find_cap(sc->sis_dev, PCIY_PMG, &pmc) != 0)
			return;
		val = 0;
		if ((ifp->if_capenable & IFCAP_WOL_MAGIC) != 0)
			val |= SIS_PWRMAN_WOL_MAGIC;
		CSR_WRITE_4(sc, SIS_PWRMAN_CTL, val);
		/* Request PME. */
		pmstat = pci_read_config(sc->sis_dev,
		    pmc + PCIR_POWER_STATUS, 2);
		pmstat &= ~(PCIM_PSTAT_PME | PCIM_PSTAT_PMEENABLE);
		if ((ifp->if_capenable & IFCAP_WOL_MAGIC) != 0)
			pmstat |= PCIM_PSTAT_PME | PCIM_PSTAT_PMEENABLE;
		pci_write_config(sc->sis_dev,
		    pmc + PCIR_POWER_STATUS, pmstat, 2);
	}
}

static void
sis_add_sysctls(struct sis_softc *sc)
{
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid_list *children;
	int unit;

	ctx = device_get_sysctl_ctx(sc->sis_dev);
	children = SYSCTL_CHILDREN(device_get_sysctl_tree(sc->sis_dev));

	unit = device_get_unit(sc->sis_dev);
	/*
	 * Unlike most other controllers, NS DP83815/DP83816 controllers
	 * seem to pad with 0xFF when it encounter short frames.  According
	 * to RFC 1042 the pad bytes should be 0x00.  Turning this tunable
	 * on will have driver pad manully but it's disabled by default
	 * because it will consume extra CPU cycles for short frames.
	 */
	sc->sis_manual_pad = 0;
	SYSCTL_ADD_INT(ctx, children, OID_AUTO, "manual_pad",
	    CTLFLAG_RWTUN, &sc->sis_manual_pad, 0, "Manually pad short frames");
}

static device_method_t sis_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		sis_probe),
	DEVMETHOD(device_attach,	sis_attach),
	DEVMETHOD(device_detach,	sis_detach),
	DEVMETHOD(device_shutdown,	sis_shutdown),
	DEVMETHOD(device_suspend,	sis_suspend),
	DEVMETHOD(device_resume,	sis_resume),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	sis_miibus_readreg),
	DEVMETHOD(miibus_writereg,	sis_miibus_writereg),
	DEVMETHOD(miibus_statchg,	sis_miibus_statchg),

	DEVMETHOD_END
};

static driver_t sis_driver = {
	"sis",
	sis_methods,
	sizeof(struct sis_softc)
};

static devclass_t sis_devclass;

DRIVER_MODULE(sis, pci, sis_driver, sis_devclass, 0, 0);
DRIVER_MODULE(miibus, sis, miibus_driver, miibus_devclass, 0, 0);
