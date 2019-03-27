/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2001 Wind River Systems
 * Copyright (c) 1997, 1998, 1999, 2000, 2001
 *	Bill Paul <wpaul@bsdi.com>.  All rights reserved.
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
 * National Semiconductor DP83820/DP83821 gigabit ethernet driver
 * for FreeBSD. Datasheets are available from:
 *
 * http://www.national.com/ds/DP/DP83820.pdf
 * http://www.national.com/ds/DP/DP83821.pdf
 *
 * These chips are used on several low cost gigabit ethernet NICs
 * sold by D-Link, Addtron, SMC and Asante. Both parts are
 * virtually the same, except the 83820 is a 64-bit/32-bit part,
 * while the 83821 is 32-bit only.
 *
 * Many cards also use National gigE transceivers, such as the
 * DP83891, DP83861 and DP83862 gigPHYTER parts. The DP83861 datasheet
 * contains a full register description that applies to all of these
 * components:
 *
 * http://www.national.com/ds/DP/DP83861.pdf
 *
 * Written by Bill Paul <wpaul@bsdi.com>
 * BSDi Open Source Solutions
 */

/*
 * The NatSemi DP83820 and 83821 controllers are enhanced versions
 * of the NatSemi MacPHYTER 10/100 devices. They support 10, 100
 * and 1000Mbps speeds with 1000baseX (ten bit interface), MII and GMII
 * ports. Other features include 8K TX FIFO and 32K RX FIFO, TCP/IP
 * hardware checksum offload (IPv4 only), VLAN tagging and filtering,
 * priority TX and RX queues, a 2048 bit multicast hash filter, 4 RX pattern
 * matching buffers, one perfect address filter buffer and interrupt
 * moderation. The 83820 supports both 64-bit and 32-bit addressing
 * and data transfers: the 64-bit support can be toggled on or off
 * via software. This affects the size of certain fields in the DMA
 * descriptors.
 *
 * There are two bugs/misfeatures in the 83820/83821 that I have
 * discovered so far:
 *
 * - Receive buffers must be aligned on 64-bit boundaries, which means
 *   you must resort to copying data in order to fix up the payload
 *   alignment.
 *
 * - In order to transmit jumbo frames larger than 8170 bytes, you have
 *   to turn off transmit checksum offloading, because the chip can't
 *   compute the checksum on an outgoing frame unless it fits entirely
 *   within the TX FIFO, which is only 8192 bytes in size. If you have
 *   TX checksum offload enabled and you transmit attempt to transmit a
 *   frame larger than 8170 bytes, the transmitter will wedge.
 *
 * To work around the latter problem, TX checksum offload is disabled
 * if the user selects an MTU larger than 8152 (8170 - 18).
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
#include <sys/mutex.h>
#include <sys/rman.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>

#include <net/bpf.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/if_vlan_var.h>

#include <dev/mii/mii.h>
#include <dev/mii/mii_bitbang.h>
#include <dev/mii/miivar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <machine/bus.h>

#include <dev/nge/if_ngereg.h>

/* "device miibus" required.  See GENERIC if you get errors here. */
#include "miibus_if.h"

MODULE_DEPEND(nge, pci, 1, 1, 1);
MODULE_DEPEND(nge, ether, 1, 1, 1);
MODULE_DEPEND(nge, miibus, 1, 1, 1);

#define NGE_CSUM_FEATURES	(CSUM_IP | CSUM_TCP | CSUM_UDP)

/*
 * Various supported device vendors/types and their names.
 */
static const struct nge_type nge_devs[] = {
	{ NGE_VENDORID, NGE_DEVICEID,
	    "National Semiconductor Gigabit Ethernet" },
	{ 0, 0, NULL }
};

static int nge_probe(device_t);
static int nge_attach(device_t);
static int nge_detach(device_t);
static int nge_shutdown(device_t);
static int nge_suspend(device_t);
static int nge_resume(device_t);

static __inline void nge_discard_rxbuf(struct nge_softc *, int);
static int nge_newbuf(struct nge_softc *, int);
static int nge_encap(struct nge_softc *, struct mbuf **);
#ifndef __NO_STRICT_ALIGNMENT
static __inline void nge_fixup_rx(struct mbuf *);
#endif
static int nge_rxeof(struct nge_softc *);
static void nge_txeof(struct nge_softc *);
static void nge_intr(void *);
static void nge_tick(void *);
static void nge_stats_update(struct nge_softc *);
static void nge_start(struct ifnet *);
static void nge_start_locked(struct ifnet *);
static int nge_ioctl(struct ifnet *, u_long, caddr_t);
static void nge_init(void *);
static void nge_init_locked(struct nge_softc *);
static int nge_stop_mac(struct nge_softc *);
static void nge_stop(struct nge_softc *);
static void nge_wol(struct nge_softc *);
static void nge_watchdog(struct nge_softc *);
static int nge_mediachange(struct ifnet *);
static void nge_mediastatus(struct ifnet *, struct ifmediareq *);

static void nge_delay(struct nge_softc *);
static void nge_eeprom_idle(struct nge_softc *);
static void nge_eeprom_putbyte(struct nge_softc *, int);
static void nge_eeprom_getword(struct nge_softc *, int, uint16_t *);
static void nge_read_eeprom(struct nge_softc *, caddr_t, int, int);

static int nge_miibus_readreg(device_t, int, int);
static int nge_miibus_writereg(device_t, int, int, int);
static void nge_miibus_statchg(device_t);

static void nge_rxfilter(struct nge_softc *);
static void nge_reset(struct nge_softc *);
static void nge_dmamap_cb(void *, bus_dma_segment_t *, int, int);
static int nge_dma_alloc(struct nge_softc *);
static void nge_dma_free(struct nge_softc *);
static int nge_list_rx_init(struct nge_softc *);
static int nge_list_tx_init(struct nge_softc *);
static void nge_sysctl_node(struct nge_softc *);
static int sysctl_int_range(SYSCTL_HANDLER_ARGS, int, int);
static int sysctl_hw_nge_int_holdoff(SYSCTL_HANDLER_ARGS);

/*
 * MII bit-bang glue
 */
static uint32_t nge_mii_bitbang_read(device_t);
static void nge_mii_bitbang_write(device_t, uint32_t);

static const struct mii_bitbang_ops nge_mii_bitbang_ops = {
	nge_mii_bitbang_read,
	nge_mii_bitbang_write,
	{
		NGE_MEAR_MII_DATA,	/* MII_BIT_MDO */
		NGE_MEAR_MII_DATA,	/* MII_BIT_MDI */
		NGE_MEAR_MII_CLK,	/* MII_BIT_MDC */
		NGE_MEAR_MII_DIR,	/* MII_BIT_DIR_HOST_PHY */
		0,			/* MII_BIT_DIR_PHY_HOST */
	}
};

static device_method_t nge_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		nge_probe),
	DEVMETHOD(device_attach,	nge_attach),
	DEVMETHOD(device_detach,	nge_detach),
	DEVMETHOD(device_shutdown,	nge_shutdown),
	DEVMETHOD(device_suspend,	nge_suspend),
	DEVMETHOD(device_resume,	nge_resume),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	nge_miibus_readreg),
	DEVMETHOD(miibus_writereg,	nge_miibus_writereg),
	DEVMETHOD(miibus_statchg,	nge_miibus_statchg),

	DEVMETHOD_END
};

static driver_t nge_driver = {
	"nge",
	nge_methods,
	sizeof(struct nge_softc)
};

static devclass_t nge_devclass;

DRIVER_MODULE(nge, pci, nge_driver, nge_devclass, 0, 0);
DRIVER_MODULE(miibus, nge, miibus_driver, miibus_devclass, 0, 0);

#define NGE_SETBIT(sc, reg, x)				\
	CSR_WRITE_4(sc, reg,				\
		CSR_READ_4(sc, reg) | (x))

#define NGE_CLRBIT(sc, reg, x)				\
	CSR_WRITE_4(sc, reg,				\
		CSR_READ_4(sc, reg) & ~(x))

#define SIO_SET(x)					\
	CSR_WRITE_4(sc, NGE_MEAR, CSR_READ_4(sc, NGE_MEAR) | (x))

#define SIO_CLR(x)					\
	CSR_WRITE_4(sc, NGE_MEAR, CSR_READ_4(sc, NGE_MEAR) & ~(x))

static void
nge_delay(struct nge_softc *sc)
{
	int idx;

	for (idx = (300 / 33) + 1; idx > 0; idx--)
		CSR_READ_4(sc, NGE_CSR);
}

static void
nge_eeprom_idle(struct nge_softc *sc)
{
	int i;

	SIO_SET(NGE_MEAR_EE_CSEL);
	nge_delay(sc);
	SIO_SET(NGE_MEAR_EE_CLK);
	nge_delay(sc);

	for (i = 0; i < 25; i++) {
		SIO_CLR(NGE_MEAR_EE_CLK);
		nge_delay(sc);
		SIO_SET(NGE_MEAR_EE_CLK);
		nge_delay(sc);
	}

	SIO_CLR(NGE_MEAR_EE_CLK);
	nge_delay(sc);
	SIO_CLR(NGE_MEAR_EE_CSEL);
	nge_delay(sc);
	CSR_WRITE_4(sc, NGE_MEAR, 0x00000000);
}

/*
 * Send a read command and address to the EEPROM, check for ACK.
 */
static void
nge_eeprom_putbyte(struct nge_softc *sc, int addr)
{
	int d, i;

	d = addr | NGE_EECMD_READ;

	/*
	 * Feed in each bit and stobe the clock.
	 */
	for (i = 0x400; i; i >>= 1) {
		if (d & i) {
			SIO_SET(NGE_MEAR_EE_DIN);
		} else {
			SIO_CLR(NGE_MEAR_EE_DIN);
		}
		nge_delay(sc);
		SIO_SET(NGE_MEAR_EE_CLK);
		nge_delay(sc);
		SIO_CLR(NGE_MEAR_EE_CLK);
		nge_delay(sc);
	}
}

/*
 * Read a word of data stored in the EEPROM at address 'addr.'
 */
static void
nge_eeprom_getword(struct nge_softc *sc, int addr, uint16_t *dest)
{
	int i;
	uint16_t word = 0;

	/* Force EEPROM to idle state. */
	nge_eeprom_idle(sc);

	/* Enter EEPROM access mode. */
	nge_delay(sc);
	SIO_CLR(NGE_MEAR_EE_CLK);
	nge_delay(sc);
	SIO_SET(NGE_MEAR_EE_CSEL);
	nge_delay(sc);

	/*
	 * Send address of word we want to read.
	 */
	nge_eeprom_putbyte(sc, addr);

	/*
	 * Start reading bits from EEPROM.
	 */
	for (i = 0x8000; i; i >>= 1) {
		SIO_SET(NGE_MEAR_EE_CLK);
		nge_delay(sc);
		if (CSR_READ_4(sc, NGE_MEAR) & NGE_MEAR_EE_DOUT)
			word |= i;
		nge_delay(sc);
		SIO_CLR(NGE_MEAR_EE_CLK);
		nge_delay(sc);
	}

	/* Turn off EEPROM access mode. */
	nge_eeprom_idle(sc);

	*dest = word;
}

/*
 * Read a sequence of words from the EEPROM.
 */
static void
nge_read_eeprom(struct nge_softc *sc, caddr_t dest, int off, int cnt)
{
	int i;
	uint16_t word = 0, *ptr;

	for (i = 0; i < cnt; i++) {
		nge_eeprom_getword(sc, off + i, &word);
		ptr = (uint16_t *)(dest + (i * 2));
		*ptr = word;
	}
}

/*
 * Read the MII serial port for the MII bit-bang module.
 */
static uint32_t
nge_mii_bitbang_read(device_t dev)
{
	struct nge_softc *sc;
	uint32_t val;

	sc = device_get_softc(dev);

	val = CSR_READ_4(sc, NGE_MEAR);
	CSR_BARRIER_4(sc, NGE_MEAR,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);

	return (val);
}

/*
 * Write the MII serial port for the MII bit-bang module.
 */
static void
nge_mii_bitbang_write(device_t dev, uint32_t val)
{
	struct nge_softc *sc;

	sc = device_get_softc(dev);

	CSR_WRITE_4(sc, NGE_MEAR, val);
	CSR_BARRIER_4(sc, NGE_MEAR,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
}

static int
nge_miibus_readreg(device_t dev, int phy, int reg)
{
	struct nge_softc *sc;
	int rv;

	sc = device_get_softc(dev);
	if ((sc->nge_flags & NGE_FLAG_TBI) != 0) {
		/* Pretend PHY is at address 0. */
		if (phy != 0)
			return (0);
		switch (reg) {
		case MII_BMCR:
			reg = NGE_TBI_BMCR;
			break;
		case MII_BMSR:
			/* 83820/83821 has different bit layout for BMSR. */
			rv = BMSR_ANEG | BMSR_EXTCAP | BMSR_EXTSTAT;
			reg = CSR_READ_4(sc, NGE_TBI_BMSR);
			if ((reg & NGE_TBIBMSR_ANEG_DONE) != 0)
				rv |= BMSR_ACOMP;
			if ((reg & NGE_TBIBMSR_LINKSTAT) != 0)
				rv |= BMSR_LINK;
			return (rv);
		case MII_ANAR:
			reg = NGE_TBI_ANAR;
			break;
		case MII_ANLPAR:
			reg = NGE_TBI_ANLPAR;
			break;
		case MII_ANER:
			reg = NGE_TBI_ANER;
			break;
		case MII_EXTSR:
			reg = NGE_TBI_ESR;
			break;
		case MII_PHYIDR1:
		case MII_PHYIDR2:
			return (0);
		default:
			device_printf(sc->nge_dev,
			    "bad phy register read : %d\n", reg);
			return (0);
		}
		return (CSR_READ_4(sc, reg));
	}

	return (mii_bitbang_readreg(dev, &nge_mii_bitbang_ops, phy, reg));
}

static int
nge_miibus_writereg(device_t dev, int phy, int reg, int data)
{
	struct nge_softc *sc;

	sc = device_get_softc(dev);
	if ((sc->nge_flags & NGE_FLAG_TBI) != 0) {
		/* Pretend PHY is at address 0. */
		if (phy != 0)
			return (0);
		switch (reg) {
		case MII_BMCR:
			reg = NGE_TBI_BMCR;
			break;
		case MII_BMSR:
			return (0);
		case MII_ANAR:
			reg = NGE_TBI_ANAR;
			break;
		case MII_ANLPAR:
			reg = NGE_TBI_ANLPAR;
			break;
		case MII_ANER:
			reg = NGE_TBI_ANER;
			break;
		case MII_EXTSR:
			reg = NGE_TBI_ESR;
			break;
		case MII_PHYIDR1:
		case MII_PHYIDR2:
			return (0);
		default:
			device_printf(sc->nge_dev,
			    "bad phy register write : %d\n", reg);
			return (0);
		}
		CSR_WRITE_4(sc, reg, data);
		return (0);
	}

	mii_bitbang_writereg(dev, &nge_mii_bitbang_ops, phy, reg, data);

	return (0);
}

/*
 * media status/link state change handler.
 */
static void
nge_miibus_statchg(device_t dev)
{
	struct nge_softc *sc;
	struct mii_data *mii;
	struct ifnet *ifp;
	struct nge_txdesc *txd;
	uint32_t done, reg, status;
	int i;

	sc = device_get_softc(dev);
	NGE_LOCK_ASSERT(sc);

	mii = device_get_softc(sc->nge_miibus);
	ifp = sc->nge_ifp;
	if (mii == NULL || ifp == NULL ||
	    (ifp->if_drv_flags & IFF_DRV_RUNNING) == 0)
		return;

	sc->nge_flags &= ~NGE_FLAG_LINK;
	if ((mii->mii_media_status & (IFM_AVALID | IFM_ACTIVE)) ==
	    (IFM_AVALID | IFM_ACTIVE)) {
		switch (IFM_SUBTYPE(mii->mii_media_active)) {
		case IFM_10_T:
		case IFM_100_TX:
		case IFM_1000_T:
		case IFM_1000_SX:
		case IFM_1000_LX:
		case IFM_1000_CX:
			sc->nge_flags |= NGE_FLAG_LINK;
			break;
		default:
			break;
		}
	}

	/* Stop Tx/Rx MACs. */
	if (nge_stop_mac(sc) == ETIMEDOUT)
		device_printf(sc->nge_dev,
		    "%s: unable to stop Tx/Rx MAC\n", __func__);
	nge_txeof(sc);
	nge_rxeof(sc);
	if (sc->nge_head != NULL) {
		m_freem(sc->nge_head);
		sc->nge_head = sc->nge_tail = NULL;
	}

	/* Release queued frames. */
	for (i = 0; i < NGE_TX_RING_CNT; i++) {
		txd = &sc->nge_cdata.nge_txdesc[i];
		if (txd->tx_m != NULL) {
			bus_dmamap_sync(sc->nge_cdata.nge_tx_tag,
			    txd->tx_dmamap, BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->nge_cdata.nge_tx_tag,
			    txd->tx_dmamap);
			m_freem(txd->tx_m);
			txd->tx_m = NULL;
		}
	}

	/* Program MAC with resolved speed/duplex. */
	if ((sc->nge_flags & NGE_FLAG_LINK) != 0) {
		if ((IFM_OPTIONS(mii->mii_media_active) & IFM_FDX) != 0) {
			NGE_SETBIT(sc, NGE_TX_CFG,
			    (NGE_TXCFG_IGN_HBEAT | NGE_TXCFG_IGN_CARR));
			NGE_SETBIT(sc, NGE_RX_CFG, NGE_RXCFG_RX_FDX);
#ifdef notyet
			/* Enable flow-control. */
			if ((IFM_OPTIONS(mii->mii_media_active) &
			    (IFM_ETH_RXPAUSE | IFM_ETH_TXPAUSE)) != 0)
				NGE_SETBIT(sc, NGE_PAUSECSR,
				    NGE_PAUSECSR_PAUSE_ENB);
#endif
		} else {
			NGE_CLRBIT(sc, NGE_TX_CFG,
			    (NGE_TXCFG_IGN_HBEAT | NGE_TXCFG_IGN_CARR));
			NGE_CLRBIT(sc, NGE_RX_CFG, NGE_RXCFG_RX_FDX);
			NGE_CLRBIT(sc, NGE_PAUSECSR, NGE_PAUSECSR_PAUSE_ENB);
		}
		/* If we have a 1000Mbps link, set the mode_1000 bit. */
		reg = CSR_READ_4(sc, NGE_CFG);
		switch (IFM_SUBTYPE(mii->mii_media_active)) {
		case IFM_1000_SX:
		case IFM_1000_LX:
		case IFM_1000_CX:
		case IFM_1000_T:
			reg |= NGE_CFG_MODE_1000;
			break;
		default:
			reg &= ~NGE_CFG_MODE_1000;
			break;
		}
		CSR_WRITE_4(sc, NGE_CFG, reg);

		/* Reset Tx/Rx MAC. */
		reg = CSR_READ_4(sc, NGE_CSR);
		reg |= NGE_CSR_TX_RESET | NGE_CSR_RX_RESET;
		CSR_WRITE_4(sc, NGE_CSR, reg);
		/* Check the completion of reset. */
		done = 0;
		for (i = 0; i < NGE_TIMEOUT; i++) {
			DELAY(1);
			status = CSR_READ_4(sc, NGE_ISR);
			if ((status & NGE_ISR_RX_RESET_DONE) != 0)
				done |= NGE_ISR_RX_RESET_DONE;
			if ((status & NGE_ISR_TX_RESET_DONE) != 0)
				done |= NGE_ISR_TX_RESET_DONE;
			if (done ==
			    (NGE_ISR_TX_RESET_DONE | NGE_ISR_RX_RESET_DONE))
				break;
		}
		if (i == NGE_TIMEOUT)
			device_printf(sc->nge_dev,
			    "%s: unable to reset Tx/Rx MAC\n", __func__);
		/* Reuse Rx buffer and reset consumer pointer. */
		sc->nge_cdata.nge_rx_cons = 0;
		/*
		 * It seems that resetting Rx/Tx MAC results in
		 * resetting Tx/Rx descriptor pointer registers such
		 * that reloading Tx/Rx lists address are needed.
		 */
		CSR_WRITE_4(sc, NGE_RX_LISTPTR_HI,
		    NGE_ADDR_HI(sc->nge_rdata.nge_rx_ring_paddr));
		CSR_WRITE_4(sc, NGE_RX_LISTPTR_LO,
		    NGE_ADDR_LO(sc->nge_rdata.nge_rx_ring_paddr));
		CSR_WRITE_4(sc, NGE_TX_LISTPTR_HI,
		    NGE_ADDR_HI(sc->nge_rdata.nge_tx_ring_paddr));
		CSR_WRITE_4(sc, NGE_TX_LISTPTR_LO,
		    NGE_ADDR_LO(sc->nge_rdata.nge_tx_ring_paddr));
		/* Reinitialize Tx buffers. */
		nge_list_tx_init(sc);

		/* Restart Rx MAC. */
		reg = CSR_READ_4(sc, NGE_CSR);
		reg |= NGE_CSR_RX_ENABLE;
		CSR_WRITE_4(sc, NGE_CSR, reg);
		for (i = 0; i < NGE_TIMEOUT; i++) {
			if ((CSR_READ_4(sc, NGE_CSR) & NGE_CSR_RX_ENABLE) != 0)
				break;
			DELAY(1);
		}
		if (i == NGE_TIMEOUT)
			device_printf(sc->nge_dev,
			    "%s: unable to restart Rx MAC\n", __func__);
	}

	/* Data LED off for TBI mode */
	if ((sc->nge_flags & NGE_FLAG_TBI) != 0)
		CSR_WRITE_4(sc, NGE_GPIO,
		    CSR_READ_4(sc, NGE_GPIO) & ~NGE_GPIO_GP3_OUT);
}

static void
nge_rxfilter(struct nge_softc *sc)
{
	struct ifnet *ifp;
	struct ifmultiaddr *ifma;
	uint32_t h, i, rxfilt;
	int bit, index;

	NGE_LOCK_ASSERT(sc);
	ifp = sc->nge_ifp;

	/* Make sure to stop Rx filtering. */
	rxfilt = CSR_READ_4(sc, NGE_RXFILT_CTL);
	rxfilt &= ~NGE_RXFILTCTL_ENABLE;
	CSR_WRITE_4(sc, NGE_RXFILT_CTL, rxfilt);
	CSR_BARRIER_4(sc, NGE_RXFILT_CTL, BUS_SPACE_BARRIER_WRITE);

	rxfilt &= ~(NGE_RXFILTCTL_ALLMULTI | NGE_RXFILTCTL_ALLPHYS);
	rxfilt &= ~NGE_RXFILTCTL_BROAD;
	/*
	 * We don't want to use the hash table for matching unicast
	 * addresses.
	 */
	rxfilt &= ~(NGE_RXFILTCTL_MCHASH | NGE_RXFILTCTL_UCHASH);

	/*
	 * For the NatSemi chip, we have to explicitly enable the
	 * reception of ARP frames, as well as turn on the 'perfect
	 * match' filter where we store the station address, otherwise
	 * we won't receive unicasts meant for this host.
	 */
	rxfilt |= NGE_RXFILTCTL_ARP | NGE_RXFILTCTL_PERFECT;

	/*
	 * Set the capture broadcast bit to capture broadcast frames.
	 */
	if ((ifp->if_flags & IFF_BROADCAST) != 0)
		rxfilt |= NGE_RXFILTCTL_BROAD;

	if ((ifp->if_flags & IFF_PROMISC) != 0 ||
	    (ifp->if_flags & IFF_ALLMULTI) != 0) {
		rxfilt |= NGE_RXFILTCTL_ALLMULTI;
		if ((ifp->if_flags & IFF_PROMISC) != 0)
			rxfilt |= NGE_RXFILTCTL_ALLPHYS;
		goto done;
	}

	/*
	 * We have to explicitly enable the multicast hash table
	 * on the NatSemi chip if we want to use it, which we do.
	 */
	rxfilt |= NGE_RXFILTCTL_MCHASH;

	/* first, zot all the existing hash bits */
	for (i = 0; i < NGE_MCAST_FILTER_LEN; i += 2) {
		CSR_WRITE_4(sc, NGE_RXFILT_CTL, NGE_FILTADDR_MCAST_LO + i);
		CSR_WRITE_4(sc, NGE_RXFILT_DATA, 0);
	}

	/*
	 * From the 11 bits returned by the crc routine, the top 7
	 * bits represent the 16-bit word in the mcast hash table
	 * that needs to be updated, and the lower 4 bits represent
	 * which bit within that byte needs to be set.
	 */
	if_maddr_rlock(ifp);
	CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		h = ether_crc32_be(LLADDR((struct sockaddr_dl *)
		    ifma->ifma_addr), ETHER_ADDR_LEN) >> 21;
		index = (h >> 4) & 0x7F;
		bit = h & 0xF;
		CSR_WRITE_4(sc, NGE_RXFILT_CTL,
		    NGE_FILTADDR_MCAST_LO + (index * 2));
		NGE_SETBIT(sc, NGE_RXFILT_DATA, (1 << bit));
	}
	if_maddr_runlock(ifp);

done:
	CSR_WRITE_4(sc, NGE_RXFILT_CTL, rxfilt);
	/* Turn the receive filter on. */
	rxfilt |= NGE_RXFILTCTL_ENABLE;
	CSR_WRITE_4(sc, NGE_RXFILT_CTL, rxfilt);
	CSR_BARRIER_4(sc, NGE_RXFILT_CTL, BUS_SPACE_BARRIER_WRITE);
}

static void
nge_reset(struct nge_softc *sc)
{
	uint32_t v;
	int i;

	NGE_SETBIT(sc, NGE_CSR, NGE_CSR_RESET);

	for (i = 0; i < NGE_TIMEOUT; i++) {
		if (!(CSR_READ_4(sc, NGE_CSR) & NGE_CSR_RESET))
			break;
		DELAY(1);
	}

	if (i == NGE_TIMEOUT)
		device_printf(sc->nge_dev, "reset never completed\n");

	/* Wait a little while for the chip to get its brains in order. */
	DELAY(1000);

	/*
	 * If this is a NetSemi chip, make sure to clear
	 * PME mode.
	 */
	CSR_WRITE_4(sc, NGE_CLKRUN, NGE_CLKRUN_PMESTS);
	CSR_WRITE_4(sc, NGE_CLKRUN, 0);

	/* Clear WOL events which may interfere normal Rx filter opertaion. */
	CSR_WRITE_4(sc, NGE_WOLCSR, 0);

	/*
	 * Only DP83820 supports 64bits addressing/data transfers and
	 * 64bit addressing requires different descriptor structures.
	 * To make it simple, disable 64bit addressing/data transfers.
	 */
	v = CSR_READ_4(sc, NGE_CFG);
	v &= ~(NGE_CFG_64BIT_ADDR_ENB | NGE_CFG_64BIT_DATA_ENB);
	CSR_WRITE_4(sc, NGE_CFG, v);
}

/*
 * Probe for a NatSemi chip. Check the PCI vendor and device
 * IDs against our list and return a device name if we find a match.
 */
static int
nge_probe(device_t dev)
{
	const struct nge_type *t;

	t = nge_devs;

	while (t->nge_name != NULL) {
		if ((pci_get_vendor(dev) == t->nge_vid) &&
		    (pci_get_device(dev) == t->nge_did)) {
			device_set_desc(dev, t->nge_name);
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
nge_attach(device_t dev)
{
	uint8_t eaddr[ETHER_ADDR_LEN];
	uint16_t ea[ETHER_ADDR_LEN/2], ea_temp, reg;
	struct nge_softc *sc;
	struct ifnet *ifp;
	int error, i, rid;

	error = 0;
	sc = device_get_softc(dev);
	sc->nge_dev = dev;

	NGE_LOCK_INIT(sc, device_get_nameunit(dev));
	callout_init_mtx(&sc->nge_stat_ch, &sc->nge_mtx, 0);

	/*
	 * Map control/status registers.
	 */
	pci_enable_busmaster(dev);

#ifdef NGE_USEIOSPACE
	sc->nge_res_type = SYS_RES_IOPORT;
	sc->nge_res_id = PCIR_BAR(0);
#else
	sc->nge_res_type = SYS_RES_MEMORY;
	sc->nge_res_id = PCIR_BAR(1);
#endif
	sc->nge_res = bus_alloc_resource_any(dev, sc->nge_res_type,
	    &sc->nge_res_id, RF_ACTIVE);

	if (sc->nge_res == NULL) {
		if (sc->nge_res_type == SYS_RES_MEMORY) {
			sc->nge_res_type = SYS_RES_IOPORT;
			sc->nge_res_id = PCIR_BAR(0);
		} else {
			sc->nge_res_type = SYS_RES_MEMORY;
			sc->nge_res_id = PCIR_BAR(1);
		}
		sc->nge_res = bus_alloc_resource_any(dev, sc->nge_res_type,
		    &sc->nge_res_id, RF_ACTIVE);
		if (sc->nge_res == NULL) {
			device_printf(dev, "couldn't allocate %s resources\n",
			    sc->nge_res_type == SYS_RES_MEMORY ? "memory" :
			    "I/O");
			NGE_LOCK_DESTROY(sc);
			return (ENXIO);
		}
	}

	/* Allocate interrupt */
	rid = 0;
	sc->nge_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_SHAREABLE | RF_ACTIVE);

	if (sc->nge_irq == NULL) {
		device_printf(dev, "couldn't map interrupt\n");
		error = ENXIO;
		goto fail;
	}

	/* Enable MWI. */
	reg = pci_read_config(dev, PCIR_COMMAND, 2);
	reg |= PCIM_CMD_MWRICEN;
	pci_write_config(dev, PCIR_COMMAND, reg, 2);

	/* Reset the adapter. */
	nge_reset(sc);

	/*
	 * Get station address from the EEPROM.
	 */
	nge_read_eeprom(sc, (caddr_t)ea, NGE_EE_NODEADDR, 3);
	for (i = 0; i < ETHER_ADDR_LEN / 2; i++)
		ea[i] = le16toh(ea[i]);
	ea_temp = ea[0];
	ea[0] = ea[2];
	ea[2] = ea_temp;
	bcopy(ea, eaddr, sizeof(eaddr));

	if (nge_dma_alloc(sc) != 0) {
		error = ENXIO;
		goto fail;
	}

	nge_sysctl_node(sc);

	ifp = sc->nge_ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(dev, "can not allocate ifnet structure\n");
		error = ENOSPC;
		goto fail;
	}
	ifp->if_softc = sc;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = nge_ioctl;
	ifp->if_start = nge_start;
	ifp->if_init = nge_init;
	ifp->if_snd.ifq_drv_maxlen = NGE_TX_RING_CNT - 1;
	IFQ_SET_MAXLEN(&ifp->if_snd, ifp->if_snd.ifq_drv_maxlen);
	IFQ_SET_READY(&ifp->if_snd);
	ifp->if_hwassist = NGE_CSUM_FEATURES;
	ifp->if_capabilities = IFCAP_HWCSUM;
	/*
	 * It seems that some hardwares doesn't provide 3.3V auxiliary
	 * supply(3VAUX) to drive PME such that checking PCI power
	 * management capability is necessary.
	 */
	if (pci_find_cap(sc->nge_dev, PCIY_PMG, &i) == 0)
		ifp->if_capabilities |= IFCAP_WOL;
	ifp->if_capenable = ifp->if_capabilities;

	if ((CSR_READ_4(sc, NGE_CFG) & NGE_CFG_TBI_EN) != 0) {
		sc->nge_flags |= NGE_FLAG_TBI;
		device_printf(dev, "Using TBI\n");
		/* Configure GPIO. */
		CSR_WRITE_4(sc, NGE_GPIO, CSR_READ_4(sc, NGE_GPIO)
		    | NGE_GPIO_GP4_OUT
		    | NGE_GPIO_GP1_OUTENB | NGE_GPIO_GP2_OUTENB
		    | NGE_GPIO_GP3_OUTENB
		    | NGE_GPIO_GP3_IN | NGE_GPIO_GP4_IN);
	}

	/*
	 * Do MII setup.
	 */
	error = mii_attach(dev, &sc->nge_miibus, ifp, nge_mediachange,
	    nge_mediastatus, BMSR_DEFCAPMASK, MII_PHY_ANY, MII_OFFSET_ANY, 0);
	if (error != 0) {
		device_printf(dev, "attaching PHYs failed\n");
		goto fail;
	}

	/*
	 * Call MI attach routine.
	 */
	ether_ifattach(ifp, eaddr);

	/* VLAN capability setup. */
	ifp->if_capabilities |= IFCAP_VLAN_MTU | IFCAP_VLAN_HWTAGGING;
	ifp->if_capabilities |= IFCAP_VLAN_HWCSUM;
	ifp->if_capenable = ifp->if_capabilities;
#ifdef DEVICE_POLLING
	ifp->if_capabilities |= IFCAP_POLLING;
#endif
	/*
	 * Tell the upper layer(s) we support long frames.
	 * Must appear after the call to ether_ifattach() because
	 * ether_ifattach() sets ifi_hdrlen to the default value.
	 */
	ifp->if_hdrlen = sizeof(struct ether_vlan_header);

	/*
	 * Hookup IRQ last.
	 */
	error = bus_setup_intr(dev, sc->nge_irq, INTR_TYPE_NET | INTR_MPSAFE,
	    NULL, nge_intr, sc, &sc->nge_intrhand);
	if (error) {
		device_printf(dev, "couldn't set up irq\n");
		goto fail;
	}

fail:
	if (error != 0)
		nge_detach(dev);
	return (error);
}

static int
nge_detach(device_t dev)
{
	struct nge_softc *sc;
	struct ifnet *ifp;

	sc = device_get_softc(dev);
	ifp = sc->nge_ifp;

#ifdef DEVICE_POLLING
	if (ifp != NULL && ifp->if_capenable & IFCAP_POLLING)
		ether_poll_deregister(ifp);
#endif

	if (device_is_attached(dev)) {
		NGE_LOCK(sc);
		sc->nge_flags |= NGE_FLAG_DETACH;
		nge_stop(sc);
		NGE_UNLOCK(sc);
		callout_drain(&sc->nge_stat_ch);
		if (ifp != NULL)
			ether_ifdetach(ifp);
	}

	if (sc->nge_miibus != NULL) {
		device_delete_child(dev, sc->nge_miibus);
		sc->nge_miibus = NULL;
	}
	bus_generic_detach(dev);
	if (sc->nge_intrhand != NULL)
		bus_teardown_intr(dev, sc->nge_irq, sc->nge_intrhand);
	if (sc->nge_irq != NULL)
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->nge_irq);
	if (sc->nge_res != NULL)
		bus_release_resource(dev, sc->nge_res_type, sc->nge_res_id,
		    sc->nge_res);

	nge_dma_free(sc);
	if (ifp != NULL)
		if_free(ifp);

	NGE_LOCK_DESTROY(sc);

	return (0);
}

struct nge_dmamap_arg {
	bus_addr_t	nge_busaddr;
};

static void
nge_dmamap_cb(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	struct nge_dmamap_arg *ctx;

	if (error != 0)
		return;
	ctx = arg;
	ctx->nge_busaddr = segs[0].ds_addr;
}

static int
nge_dma_alloc(struct nge_softc *sc)
{
	struct nge_dmamap_arg ctx;
	struct nge_txdesc *txd;
	struct nge_rxdesc *rxd;
	int error, i;

	/* Create parent DMA tag. */
	error = bus_dma_tag_create(
	    bus_get_dma_tag(sc->nge_dev),	/* parent */
	    1, 0,			/* alignment, boundary */
	    BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    BUS_SPACE_MAXSIZE_32BIT,	/* maxsize */
	    0,				/* nsegments */
	    BUS_SPACE_MAXSIZE_32BIT,	/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->nge_cdata.nge_parent_tag);
	if (error != 0) {
		device_printf(sc->nge_dev, "failed to create parent DMA tag\n");
		goto fail;
	}
	/* Create tag for Tx ring. */
	error = bus_dma_tag_create(sc->nge_cdata.nge_parent_tag,/* parent */
	    NGE_RING_ALIGN, 0,		/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    NGE_TX_RING_SIZE,		/* maxsize */
	    1,				/* nsegments */
	    NGE_TX_RING_SIZE,		/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->nge_cdata.nge_tx_ring_tag);
	if (error != 0) {
		device_printf(sc->nge_dev, "failed to create Tx ring DMA tag\n");
		goto fail;
	}

	/* Create tag for Rx ring. */
	error = bus_dma_tag_create(sc->nge_cdata.nge_parent_tag,/* parent */
	    NGE_RING_ALIGN, 0,		/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    NGE_RX_RING_SIZE,		/* maxsize */
	    1,				/* nsegments */
	    NGE_RX_RING_SIZE,		/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->nge_cdata.nge_rx_ring_tag);
	if (error != 0) {
		device_printf(sc->nge_dev,
		    "failed to create Rx ring DMA tag\n");
		goto fail;
	}

	/* Create tag for Tx buffers. */
	error = bus_dma_tag_create(sc->nge_cdata.nge_parent_tag,/* parent */
	    1, 0,			/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    MCLBYTES * NGE_MAXTXSEGS,	/* maxsize */
	    NGE_MAXTXSEGS,		/* nsegments */
	    MCLBYTES,			/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->nge_cdata.nge_tx_tag);
	if (error != 0) {
		device_printf(sc->nge_dev, "failed to create Tx DMA tag\n");
		goto fail;
	}

	/* Create tag for Rx buffers. */
	error = bus_dma_tag_create(sc->nge_cdata.nge_parent_tag,/* parent */
	    NGE_RX_ALIGN, 0,		/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    MCLBYTES,			/* maxsize */
	    1,				/* nsegments */
	    MCLBYTES,			/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->nge_cdata.nge_rx_tag);
	if (error != 0) {
		device_printf(sc->nge_dev, "failed to create Rx DMA tag\n");
		goto fail;
	}

	/* Allocate DMA'able memory and load the DMA map for Tx ring. */
	error = bus_dmamem_alloc(sc->nge_cdata.nge_tx_ring_tag,
	    (void **)&sc->nge_rdata.nge_tx_ring, BUS_DMA_WAITOK |
	    BUS_DMA_COHERENT | BUS_DMA_ZERO, &sc->nge_cdata.nge_tx_ring_map);
	if (error != 0) {
		device_printf(sc->nge_dev,
		    "failed to allocate DMA'able memory for Tx ring\n");
		goto fail;
	}

	ctx.nge_busaddr = 0;
	error = bus_dmamap_load(sc->nge_cdata.nge_tx_ring_tag,
	    sc->nge_cdata.nge_tx_ring_map, sc->nge_rdata.nge_tx_ring,
	    NGE_TX_RING_SIZE, nge_dmamap_cb, &ctx, 0);
	if (error != 0 || ctx.nge_busaddr == 0) {
		device_printf(sc->nge_dev,
		    "failed to load DMA'able memory for Tx ring\n");
		goto fail;
	}
	sc->nge_rdata.nge_tx_ring_paddr = ctx.nge_busaddr;

	/* Allocate DMA'able memory and load the DMA map for Rx ring. */
	error = bus_dmamem_alloc(sc->nge_cdata.nge_rx_ring_tag,
	    (void **)&sc->nge_rdata.nge_rx_ring, BUS_DMA_WAITOK |
	    BUS_DMA_COHERENT | BUS_DMA_ZERO, &sc->nge_cdata.nge_rx_ring_map);
	if (error != 0) {
		device_printf(sc->nge_dev,
		    "failed to allocate DMA'able memory for Rx ring\n");
		goto fail;
	}

	ctx.nge_busaddr = 0;
	error = bus_dmamap_load(sc->nge_cdata.nge_rx_ring_tag,
	    sc->nge_cdata.nge_rx_ring_map, sc->nge_rdata.nge_rx_ring,
	    NGE_RX_RING_SIZE, nge_dmamap_cb, &ctx, 0);
	if (error != 0 || ctx.nge_busaddr == 0) {
		device_printf(sc->nge_dev,
		    "failed to load DMA'able memory for Rx ring\n");
		goto fail;
	}
	sc->nge_rdata.nge_rx_ring_paddr = ctx.nge_busaddr;

	/* Create DMA maps for Tx buffers. */
	for (i = 0; i < NGE_TX_RING_CNT; i++) {
		txd = &sc->nge_cdata.nge_txdesc[i];
		txd->tx_m = NULL;
		txd->tx_dmamap = NULL;
		error = bus_dmamap_create(sc->nge_cdata.nge_tx_tag, 0,
		    &txd->tx_dmamap);
		if (error != 0) {
			device_printf(sc->nge_dev,
			    "failed to create Tx dmamap\n");
			goto fail;
		}
	}
	/* Create DMA maps for Rx buffers. */
	if ((error = bus_dmamap_create(sc->nge_cdata.nge_rx_tag, 0,
	    &sc->nge_cdata.nge_rx_sparemap)) != 0) {
		device_printf(sc->nge_dev,
		    "failed to create spare Rx dmamap\n");
		goto fail;
	}
	for (i = 0; i < NGE_RX_RING_CNT; i++) {
		rxd = &sc->nge_cdata.nge_rxdesc[i];
		rxd->rx_m = NULL;
		rxd->rx_dmamap = NULL;
		error = bus_dmamap_create(sc->nge_cdata.nge_rx_tag, 0,
		    &rxd->rx_dmamap);
		if (error != 0) {
			device_printf(sc->nge_dev,
			    "failed to create Rx dmamap\n");
			goto fail;
		}
	}

fail:
	return (error);
}

static void
nge_dma_free(struct nge_softc *sc)
{
	struct nge_txdesc *txd;
	struct nge_rxdesc *rxd;
	int i;

	/* Tx ring. */
	if (sc->nge_cdata.nge_tx_ring_tag) {
		if (sc->nge_rdata.nge_tx_ring_paddr)
			bus_dmamap_unload(sc->nge_cdata.nge_tx_ring_tag,
			    sc->nge_cdata.nge_tx_ring_map);
		if (sc->nge_rdata.nge_tx_ring)
			bus_dmamem_free(sc->nge_cdata.nge_tx_ring_tag,
			    sc->nge_rdata.nge_tx_ring,
			    sc->nge_cdata.nge_tx_ring_map);
		sc->nge_rdata.nge_tx_ring = NULL;
		sc->nge_rdata.nge_tx_ring_paddr = 0;
		bus_dma_tag_destroy(sc->nge_cdata.nge_tx_ring_tag);
		sc->nge_cdata.nge_tx_ring_tag = NULL;
	}
	/* Rx ring. */
	if (sc->nge_cdata.nge_rx_ring_tag) {
		if (sc->nge_rdata.nge_rx_ring_paddr)
			bus_dmamap_unload(sc->nge_cdata.nge_rx_ring_tag,
			    sc->nge_cdata.nge_rx_ring_map);
		if (sc->nge_rdata.nge_rx_ring)
			bus_dmamem_free(sc->nge_cdata.nge_rx_ring_tag,
			    sc->nge_rdata.nge_rx_ring,
			    sc->nge_cdata.nge_rx_ring_map);
		sc->nge_rdata.nge_rx_ring = NULL;
		sc->nge_rdata.nge_rx_ring_paddr = 0;
		bus_dma_tag_destroy(sc->nge_cdata.nge_rx_ring_tag);
		sc->nge_cdata.nge_rx_ring_tag = NULL;
	}
	/* Tx buffers. */
	if (sc->nge_cdata.nge_tx_tag) {
		for (i = 0; i < NGE_TX_RING_CNT; i++) {
			txd = &sc->nge_cdata.nge_txdesc[i];
			if (txd->tx_dmamap) {
				bus_dmamap_destroy(sc->nge_cdata.nge_tx_tag,
				    txd->tx_dmamap);
				txd->tx_dmamap = NULL;
			}
		}
		bus_dma_tag_destroy(sc->nge_cdata.nge_tx_tag);
		sc->nge_cdata.nge_tx_tag = NULL;
	}
	/* Rx buffers. */
	if (sc->nge_cdata.nge_rx_tag) {
		for (i = 0; i < NGE_RX_RING_CNT; i++) {
			rxd = &sc->nge_cdata.nge_rxdesc[i];
			if (rxd->rx_dmamap) {
				bus_dmamap_destroy(sc->nge_cdata.nge_rx_tag,
				    rxd->rx_dmamap);
				rxd->rx_dmamap = NULL;
			}
		}
		if (sc->nge_cdata.nge_rx_sparemap) {
			bus_dmamap_destroy(sc->nge_cdata.nge_rx_tag,
			    sc->nge_cdata.nge_rx_sparemap);
			sc->nge_cdata.nge_rx_sparemap = 0;
		}
		bus_dma_tag_destroy(sc->nge_cdata.nge_rx_tag);
		sc->nge_cdata.nge_rx_tag = NULL;
	}

	if (sc->nge_cdata.nge_parent_tag) {
		bus_dma_tag_destroy(sc->nge_cdata.nge_parent_tag);
		sc->nge_cdata.nge_parent_tag = NULL;
	}
}

/*
 * Initialize the transmit descriptors.
 */
static int
nge_list_tx_init(struct nge_softc *sc)
{
	struct nge_ring_data *rd;
	struct nge_txdesc *txd;
	bus_addr_t addr;
	int i;

	sc->nge_cdata.nge_tx_prod = 0;
	sc->nge_cdata.nge_tx_cons = 0;
	sc->nge_cdata.nge_tx_cnt = 0;

	rd = &sc->nge_rdata;
	bzero(rd->nge_tx_ring, sizeof(struct nge_desc) * NGE_TX_RING_CNT);
	for (i = 0; i < NGE_TX_RING_CNT; i++) {
		if (i == NGE_TX_RING_CNT - 1)
			addr = NGE_TX_RING_ADDR(sc, 0);
		else
			addr = NGE_TX_RING_ADDR(sc, i + 1);
		rd->nge_tx_ring[i].nge_next = htole32(NGE_ADDR_LO(addr));
		txd = &sc->nge_cdata.nge_txdesc[i];
		txd->tx_m = NULL;
	}

	bus_dmamap_sync(sc->nge_cdata.nge_tx_ring_tag,
	    sc->nge_cdata.nge_tx_ring_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	return (0);
}

/*
 * Initialize the RX descriptors and allocate mbufs for them. Note that
 * we arrange the descriptors in a closed ring, so that the last descriptor
 * points back to the first.
 */
static int
nge_list_rx_init(struct nge_softc *sc)
{
	struct nge_ring_data *rd;
	bus_addr_t addr;
	int i;

	sc->nge_cdata.nge_rx_cons = 0;
	sc->nge_head = sc->nge_tail = NULL;

	rd = &sc->nge_rdata;
	bzero(rd->nge_rx_ring, sizeof(struct nge_desc) * NGE_RX_RING_CNT);
	for (i = 0; i < NGE_RX_RING_CNT; i++) {
		if (nge_newbuf(sc, i) != 0)
			return (ENOBUFS);
		if (i == NGE_RX_RING_CNT - 1)
			addr = NGE_RX_RING_ADDR(sc, 0);
		else
			addr = NGE_RX_RING_ADDR(sc, i + 1);
		rd->nge_rx_ring[i].nge_next = htole32(NGE_ADDR_LO(addr));
	}

	bus_dmamap_sync(sc->nge_cdata.nge_rx_ring_tag,
	    sc->nge_cdata.nge_rx_ring_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	return (0);
}

static __inline void
nge_discard_rxbuf(struct nge_softc *sc, int idx)
{
	struct nge_desc *desc;

	desc = &sc->nge_rdata.nge_rx_ring[idx];
	desc->nge_cmdsts = htole32(MCLBYTES - sizeof(uint64_t));
	desc->nge_extsts = 0;
}

/*
 * Initialize an RX descriptor and attach an MBUF cluster.
 */
static int
nge_newbuf(struct nge_softc *sc, int idx)
{
	struct nge_desc *desc;
	struct nge_rxdesc *rxd;
	struct mbuf *m;
	bus_dma_segment_t segs[1];
	bus_dmamap_t map;
	int nsegs;

	m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
	if (m == NULL)
		return (ENOBUFS);
	m->m_len = m->m_pkthdr.len = MCLBYTES;
	m_adj(m, sizeof(uint64_t));

	if (bus_dmamap_load_mbuf_sg(sc->nge_cdata.nge_rx_tag,
	    sc->nge_cdata.nge_rx_sparemap, m, segs, &nsegs, 0) != 0) {
		m_freem(m);
		return (ENOBUFS);
	}
	KASSERT(nsegs == 1, ("%s: %d segments returned!", __func__, nsegs));

	rxd = &sc->nge_cdata.nge_rxdesc[idx];
	if (rxd->rx_m != NULL) {
		bus_dmamap_sync(sc->nge_cdata.nge_rx_tag, rxd->rx_dmamap,
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->nge_cdata.nge_rx_tag, rxd->rx_dmamap);
	}
	map = rxd->rx_dmamap;
	rxd->rx_dmamap = sc->nge_cdata.nge_rx_sparemap;
	sc->nge_cdata.nge_rx_sparemap = map;
	bus_dmamap_sync(sc->nge_cdata.nge_rx_tag, rxd->rx_dmamap,
	    BUS_DMASYNC_PREREAD);
	rxd->rx_m = m;
	desc = &sc->nge_rdata.nge_rx_ring[idx];
	desc->nge_ptr = htole32(NGE_ADDR_LO(segs[0].ds_addr));
	desc->nge_cmdsts = htole32(segs[0].ds_len);
	desc->nge_extsts = 0;

	return (0);
}

#ifndef __NO_STRICT_ALIGNMENT
static __inline void
nge_fixup_rx(struct mbuf *m)
{
	int			i;
	uint16_t		*src, *dst;

	src = mtod(m, uint16_t *);
	dst = src - 1;

	for (i = 0; i < (m->m_len / sizeof(uint16_t) + 1); i++)
		*dst++ = *src++;

	m->m_data -= ETHER_ALIGN;
}
#endif

/*
 * A frame has been uploaded: pass the resulting mbuf chain up to
 * the higher level protocols.
 */
static int
nge_rxeof(struct nge_softc *sc)
{
	struct mbuf *m;
	struct ifnet *ifp;
	struct nge_desc *cur_rx;
	struct nge_rxdesc *rxd;
	int cons, prog, rx_npkts, total_len;
	uint32_t cmdsts, extsts;

	NGE_LOCK_ASSERT(sc);

	ifp = sc->nge_ifp;
	cons = sc->nge_cdata.nge_rx_cons;
	rx_npkts = 0;

	bus_dmamap_sync(sc->nge_cdata.nge_rx_ring_tag,
	    sc->nge_cdata.nge_rx_ring_map,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	for (prog = 0; prog < NGE_RX_RING_CNT &&
	    (ifp->if_drv_flags & IFF_DRV_RUNNING) != 0;
	    NGE_INC(cons, NGE_RX_RING_CNT)) {
#ifdef DEVICE_POLLING
		if (ifp->if_capenable & IFCAP_POLLING) {
			if (sc->rxcycles <= 0)
				break;
			sc->rxcycles--;
		}
#endif
		cur_rx = &sc->nge_rdata.nge_rx_ring[cons];
		cmdsts = le32toh(cur_rx->nge_cmdsts);
		extsts = le32toh(cur_rx->nge_extsts);
		if ((cmdsts & NGE_CMDSTS_OWN) == 0)
			break;
		prog++;
		rxd = &sc->nge_cdata.nge_rxdesc[cons];
		m = rxd->rx_m;
		total_len = cmdsts & NGE_CMDSTS_BUFLEN;

		if ((cmdsts & NGE_CMDSTS_MORE) != 0) {
			if (nge_newbuf(sc, cons) != 0) {
				if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
				if (sc->nge_head != NULL) {
					m_freem(sc->nge_head);
					sc->nge_head = sc->nge_tail = NULL;
				}
				nge_discard_rxbuf(sc, cons);
				continue;
			}
			m->m_len = total_len;
			if (sc->nge_head == NULL) {
				m->m_pkthdr.len = total_len;
				sc->nge_head = sc->nge_tail = m;
			} else {
				m->m_flags &= ~M_PKTHDR;
				sc->nge_head->m_pkthdr.len += total_len;
				sc->nge_tail->m_next = m;
				sc->nge_tail = m;
			}
			continue;
		}

		/*
		 * If an error occurs, update stats, clear the
		 * status word and leave the mbuf cluster in place:
		 * it should simply get re-used next time this descriptor
	 	 * comes up in the ring.
		 */
		if ((cmdsts & NGE_CMDSTS_PKT_OK) == 0) {
			if ((cmdsts & NGE_RXSTAT_RUNT) &&
			    total_len >= (ETHER_MIN_LEN - ETHER_CRC_LEN - 4)) {
				/*
				 * Work-around hardware bug, accept runt frames
				 * if its length is larger than or equal to 56.
				 */
			} else {
				/*
				 * Input error counters are updated by hardware.
				 */
				if (sc->nge_head != NULL) {
					m_freem(sc->nge_head);
					sc->nge_head = sc->nge_tail = NULL;
				}
				nge_discard_rxbuf(sc, cons);
				continue;
			}
		}

		/* Try conjure up a replacement mbuf. */

		if (nge_newbuf(sc, cons) != 0) {
			if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
			if (sc->nge_head != NULL) {
				m_freem(sc->nge_head);
				sc->nge_head = sc->nge_tail = NULL;
			}
			nge_discard_rxbuf(sc, cons);
			continue;
		}

		/* Chain received mbufs. */
		if (sc->nge_head != NULL) {
			m->m_len = total_len;
			m->m_flags &= ~M_PKTHDR;
			sc->nge_tail->m_next = m;
			m = sc->nge_head;
			m->m_pkthdr.len += total_len;
			sc->nge_head = sc->nge_tail = NULL;
		} else
			m->m_pkthdr.len = m->m_len = total_len;

		/*
		 * Ok. NatSemi really screwed up here. This is the
		 * only gigE chip I know of with alignment constraints
		 * on receive buffers. RX buffers must be 64-bit aligned.
		 */
		/*
		 * By popular demand, ignore the alignment problems
		 * on the non-strict alignment platform. The performance hit
		 * incurred due to unaligned accesses is much smaller
		 * than the hit produced by forcing buffer copies all
		 * the time, especially with jumbo frames. We still
		 * need to fix up the alignment everywhere else though.
		 */
#ifndef __NO_STRICT_ALIGNMENT
		nge_fixup_rx(m);
#endif
		m->m_pkthdr.rcvif = ifp;
		if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);

		if ((ifp->if_capenable & IFCAP_RXCSUM) != 0) {
			/* Do IP checksum checking. */
			if ((extsts & NGE_RXEXTSTS_IPPKT) != 0)
				m->m_pkthdr.csum_flags |= CSUM_IP_CHECKED;
			if ((extsts & NGE_RXEXTSTS_IPCSUMERR) == 0)
				m->m_pkthdr.csum_flags |= CSUM_IP_VALID;
			if ((extsts & NGE_RXEXTSTS_TCPPKT &&
			    !(extsts & NGE_RXEXTSTS_TCPCSUMERR)) ||
			    (extsts & NGE_RXEXTSTS_UDPPKT &&
			    !(extsts & NGE_RXEXTSTS_UDPCSUMERR))) {
				m->m_pkthdr.csum_flags |=
				    CSUM_DATA_VALID | CSUM_PSEUDO_HDR;
				m->m_pkthdr.csum_data = 0xffff;
			}
		}

		/*
		 * If we received a packet with a vlan tag, pass it
		 * to vlan_input() instead of ether_input().
		 */
		if ((extsts & NGE_RXEXTSTS_VLANPKT) != 0 &&
		    (ifp->if_capenable & IFCAP_VLAN_HWTAGGING) != 0) {
			m->m_pkthdr.ether_vtag =
			    bswap16(extsts & NGE_RXEXTSTS_VTCI);
			m->m_flags |= M_VLANTAG;
		}
		NGE_UNLOCK(sc);
		(*ifp->if_input)(ifp, m);
		NGE_LOCK(sc);
		rx_npkts++;
	}

	if (prog > 0) {
		sc->nge_cdata.nge_rx_cons = cons;
		bus_dmamap_sync(sc->nge_cdata.nge_rx_ring_tag,
		    sc->nge_cdata.nge_rx_ring_map,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	}
	return (rx_npkts);
}

/*
 * A frame was downloaded to the chip. It's safe for us to clean up
 * the list buffers.
 */
static void
nge_txeof(struct nge_softc *sc)
{
	struct nge_desc	*cur_tx;
	struct nge_txdesc *txd;
	struct ifnet *ifp;
	uint32_t cmdsts;
	int cons, prod;

	NGE_LOCK_ASSERT(sc);
	ifp = sc->nge_ifp;

	cons = sc->nge_cdata.nge_tx_cons;
	prod = sc->nge_cdata.nge_tx_prod;
	if (cons == prod)
		return;

	bus_dmamap_sync(sc->nge_cdata.nge_tx_ring_tag,
	    sc->nge_cdata.nge_tx_ring_map,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	/*
	 * Go through our tx list and free mbufs for those
	 * frames that have been transmitted.
	 */
	for (; cons != prod; NGE_INC(cons, NGE_TX_RING_CNT)) {
		cur_tx = &sc->nge_rdata.nge_tx_ring[cons];
		cmdsts = le32toh(cur_tx->nge_cmdsts);
		if ((cmdsts & NGE_CMDSTS_OWN) != 0)
			break;
		sc->nge_cdata.nge_tx_cnt--;
		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
		if ((cmdsts & NGE_CMDSTS_MORE) != 0)
			continue;

		txd = &sc->nge_cdata.nge_txdesc[cons];
		bus_dmamap_sync(sc->nge_cdata.nge_tx_tag, txd->tx_dmamap,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->nge_cdata.nge_tx_tag, txd->tx_dmamap);
		if ((cmdsts & NGE_CMDSTS_PKT_OK) == 0) {
			if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
			if ((cmdsts & NGE_TXSTAT_EXCESSCOLLS) != 0)
				if_inc_counter(ifp, IFCOUNTER_COLLISIONS, 1);
			if ((cmdsts & NGE_TXSTAT_OUTOFWINCOLL) != 0)
				if_inc_counter(ifp, IFCOUNTER_COLLISIONS, 1);
		} else
			if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);

		if_inc_counter(ifp, IFCOUNTER_COLLISIONS, (cmdsts & NGE_TXSTAT_COLLCNT) >> 16);
		KASSERT(txd->tx_m != NULL, ("%s: freeing NULL mbuf!\n",
		    __func__));
		m_freem(txd->tx_m);
		txd->tx_m = NULL;
	}

	sc->nge_cdata.nge_tx_cons = cons;
	if (sc->nge_cdata.nge_tx_cnt == 0)
		sc->nge_watchdog_timer = 0;
}

static void
nge_tick(void *xsc)
{
	struct nge_softc *sc;
	struct mii_data *mii;

	sc = xsc;
	NGE_LOCK_ASSERT(sc);
	mii = device_get_softc(sc->nge_miibus);
	mii_tick(mii);
	/*
	 * For PHYs that does not reset established link, it is
	 * necessary to check whether driver still have a valid
	 * link(e.g link state change callback is not called).
	 * Otherwise, driver think it lost link because driver
	 * initialization routine clears link state flag.
	 */
	if ((sc->nge_flags & NGE_FLAG_LINK) == 0)
		nge_miibus_statchg(sc->nge_dev);
	nge_stats_update(sc);
	nge_watchdog(sc);
	callout_reset(&sc->nge_stat_ch, hz, nge_tick, sc);
}

static void
nge_stats_update(struct nge_softc *sc)
{
	struct ifnet *ifp;
	struct nge_stats now, *stats, *nstats;

	NGE_LOCK_ASSERT(sc);

	ifp = sc->nge_ifp;
	stats = &now;
	stats->rx_pkts_errs =
	    CSR_READ_4(sc, NGE_MIB_RXERRPKT) & 0xFFFF;
	stats->rx_crc_errs =
	    CSR_READ_4(sc, NGE_MIB_RXERRFCS) & 0xFFFF;
	stats->rx_fifo_oflows =
	    CSR_READ_4(sc, NGE_MIB_RXERRMISSEDPKT) & 0xFFFF;
	stats->rx_align_errs =
	    CSR_READ_4(sc, NGE_MIB_RXERRALIGN) & 0xFFFF;
	stats->rx_sym_errs =
	    CSR_READ_4(sc, NGE_MIB_RXERRSYM) & 0xFFFF;
	stats->rx_pkts_jumbos =
	    CSR_READ_4(sc, NGE_MIB_RXERRGIANT) & 0xFFFF;
	stats->rx_len_errs =
	    CSR_READ_4(sc, NGE_MIB_RXERRRANGLEN) & 0xFFFF;
	stats->rx_unctl_frames =
	    CSR_READ_4(sc, NGE_MIB_RXBADOPCODE) & 0xFFFF;
	stats->rx_pause =
	    CSR_READ_4(sc, NGE_MIB_RXPAUSEPKTS) & 0xFFFF;
	stats->tx_pause =
	    CSR_READ_4(sc, NGE_MIB_TXPAUSEPKTS) & 0xFFFF;
	stats->tx_seq_errs =
	    CSR_READ_4(sc, NGE_MIB_TXERRSQE) & 0xFF;

	/*
	 * Since we've accept errored frames exclude Rx length errors.
	 */
	if_inc_counter(ifp, IFCOUNTER_IERRORS,
	    stats->rx_pkts_errs + stats->rx_crc_errs +
	    stats->rx_fifo_oflows + stats->rx_sym_errs);

	nstats = &sc->nge_stats;
	nstats->rx_pkts_errs += stats->rx_pkts_errs;
	nstats->rx_crc_errs += stats->rx_crc_errs;
	nstats->rx_fifo_oflows += stats->rx_fifo_oflows;
	nstats->rx_align_errs += stats->rx_align_errs;
	nstats->rx_sym_errs += stats->rx_sym_errs;
	nstats->rx_pkts_jumbos += stats->rx_pkts_jumbos;
	nstats->rx_len_errs += stats->rx_len_errs;
	nstats->rx_unctl_frames += stats->rx_unctl_frames;
	nstats->rx_pause += stats->rx_pause;
	nstats->tx_pause += stats->tx_pause;
	nstats->tx_seq_errs += stats->tx_seq_errs;
}

#ifdef DEVICE_POLLING
static poll_handler_t nge_poll;

static int
nge_poll(struct ifnet *ifp, enum poll_cmd cmd, int count)
{
	struct nge_softc *sc;
	int rx_npkts = 0;

	sc = ifp->if_softc;

	NGE_LOCK(sc);
	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0) {
		NGE_UNLOCK(sc);
		return (rx_npkts);
	}

	/*
	 * On the nge, reading the status register also clears it.
	 * So before returning to intr mode we must make sure that all
	 * possible pending sources of interrupts have been served.
	 * In practice this means run to completion the *eof routines,
	 * and then call the interrupt routine.
	 */
	sc->rxcycles = count;
	rx_npkts = nge_rxeof(sc);
	nge_txeof(sc);
	if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd))
		nge_start_locked(ifp);

	if (sc->rxcycles > 0 || cmd == POLL_AND_CHECK_STATUS) {
		uint32_t	status;

		/* Reading the ISR register clears all interrupts. */
		status = CSR_READ_4(sc, NGE_ISR);

		if ((status & (NGE_ISR_RX_ERR|NGE_ISR_RX_OFLOW)) != 0)
			rx_npkts += nge_rxeof(sc);

		if ((status & NGE_ISR_RX_IDLE) != 0)
			NGE_SETBIT(sc, NGE_CSR, NGE_CSR_RX_ENABLE);

		if ((status & NGE_ISR_SYSERR) != 0) {
			ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
			nge_init_locked(sc);
		}
	}
	NGE_UNLOCK(sc);
	return (rx_npkts);
}
#endif /* DEVICE_POLLING */

static void
nge_intr(void *arg)
{
	struct nge_softc *sc;
	struct ifnet *ifp;
	uint32_t status;

	sc = (struct nge_softc *)arg;
	ifp = sc->nge_ifp;

	NGE_LOCK(sc);

	if ((sc->nge_flags & NGE_FLAG_SUSPENDED) != 0)
		goto done_locked;

	/* Reading the ISR register clears all interrupts. */
	status = CSR_READ_4(sc, NGE_ISR);
	if (status == 0xffffffff || (status & NGE_INTRS) == 0)
		goto done_locked;
#ifdef DEVICE_POLLING
	if ((ifp->if_capenable & IFCAP_POLLING) != 0)
		goto done_locked;
#endif
	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0)
		goto done_locked;

	/* Disable interrupts. */
	CSR_WRITE_4(sc, NGE_IER, 0);

	/* Data LED on for TBI mode */
	if ((sc->nge_flags & NGE_FLAG_TBI) != 0)
		CSR_WRITE_4(sc, NGE_GPIO,
		    CSR_READ_4(sc, NGE_GPIO) | NGE_GPIO_GP3_OUT);

	for (; (status & NGE_INTRS) != 0;) {
		if ((status & (NGE_ISR_TX_DESC_OK | NGE_ISR_TX_ERR |
		    NGE_ISR_TX_OK | NGE_ISR_TX_IDLE)) != 0)
			nge_txeof(sc);

		if ((status & (NGE_ISR_RX_DESC_OK | NGE_ISR_RX_ERR |
		    NGE_ISR_RX_OFLOW | NGE_ISR_RX_FIFO_OFLOW |
		    NGE_ISR_RX_IDLE | NGE_ISR_RX_OK)) != 0)
			nge_rxeof(sc);

		if ((status & NGE_ISR_RX_IDLE) != 0)
			NGE_SETBIT(sc, NGE_CSR, NGE_CSR_RX_ENABLE);

		if ((status & NGE_ISR_SYSERR) != 0) {
			ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
			nge_init_locked(sc);
		}
		/* Reading the ISR register clears all interrupts. */
		status = CSR_READ_4(sc, NGE_ISR);
	}

	/* Re-enable interrupts. */
	CSR_WRITE_4(sc, NGE_IER, 1);

	if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd))
		nge_start_locked(ifp);

	/* Data LED off for TBI mode */
	if ((sc->nge_flags & NGE_FLAG_TBI) != 0)
		CSR_WRITE_4(sc, NGE_GPIO,
		    CSR_READ_4(sc, NGE_GPIO) & ~NGE_GPIO_GP3_OUT);

done_locked:
	NGE_UNLOCK(sc);
}

/*
 * Encapsulate an mbuf chain in a descriptor by coupling the mbuf data
 * pointers to the fragment pointers.
 */
static int
nge_encap(struct nge_softc *sc, struct mbuf **m_head)
{
	struct nge_txdesc *txd, *txd_last;
	struct nge_desc *desc;
	struct mbuf *m;
	bus_dmamap_t map;
	bus_dma_segment_t txsegs[NGE_MAXTXSEGS];
	int error, i, nsegs, prod, si;

	NGE_LOCK_ASSERT(sc);

	m = *m_head;
	prod = sc->nge_cdata.nge_tx_prod;
	txd = &sc->nge_cdata.nge_txdesc[prod];
	txd_last = txd;
	map = txd->tx_dmamap;
	error = bus_dmamap_load_mbuf_sg(sc->nge_cdata.nge_tx_tag, map,
	    *m_head, txsegs, &nsegs, BUS_DMA_NOWAIT);
	if (error == EFBIG) {
		m = m_collapse(*m_head, M_NOWAIT, NGE_MAXTXSEGS);
		if (m == NULL) {
			m_freem(*m_head);
			*m_head = NULL;
			return (ENOBUFS);
		}
		*m_head = m;
		error = bus_dmamap_load_mbuf_sg(sc->nge_cdata.nge_tx_tag,
		    map, *m_head, txsegs, &nsegs, BUS_DMA_NOWAIT);
		if (error != 0) {
			m_freem(*m_head);
			*m_head = NULL;
			return (error);
		}
	} else if (error != 0)
		return (error);
	if (nsegs == 0) {
		m_freem(*m_head);
		*m_head = NULL;
		return (EIO);
	}

	/* Check number of available descriptors. */
	if (sc->nge_cdata.nge_tx_cnt + nsegs >= (NGE_TX_RING_CNT - 1)) {
		bus_dmamap_unload(sc->nge_cdata.nge_tx_tag, map);
		return (ENOBUFS);
	}

	bus_dmamap_sync(sc->nge_cdata.nge_tx_tag, map, BUS_DMASYNC_PREWRITE);

	si = prod;
	for (i = 0; i < nsegs; i++) {
		desc = &sc->nge_rdata.nge_tx_ring[prod];
		desc->nge_ptr = htole32(NGE_ADDR_LO(txsegs[i].ds_addr));
		if (i == 0)
			desc->nge_cmdsts = htole32(txsegs[i].ds_len |
			    NGE_CMDSTS_MORE);
		else
			desc->nge_cmdsts = htole32(txsegs[i].ds_len |
			    NGE_CMDSTS_MORE | NGE_CMDSTS_OWN);
		desc->nge_extsts = 0;
		sc->nge_cdata.nge_tx_cnt++;
		NGE_INC(prod, NGE_TX_RING_CNT);
	}
	/* Update producer index. */
	sc->nge_cdata.nge_tx_prod = prod;

	prod = (prod + NGE_TX_RING_CNT - 1) % NGE_TX_RING_CNT;
	desc = &sc->nge_rdata.nge_tx_ring[prod];
	/* Check if we have a VLAN tag to insert. */
	if ((m->m_flags & M_VLANTAG) != 0)
		desc->nge_extsts |= htole32(NGE_TXEXTSTS_VLANPKT |
		    bswap16(m->m_pkthdr.ether_vtag));
	/* Set EOP on the last desciptor. */
	desc->nge_cmdsts &= htole32(~NGE_CMDSTS_MORE);

	/* Set checksum offload in the first descriptor. */
	desc = &sc->nge_rdata.nge_tx_ring[si];
	if ((m->m_pkthdr.csum_flags & NGE_CSUM_FEATURES) != 0) {
		if ((m->m_pkthdr.csum_flags & CSUM_IP) != 0)
			desc->nge_extsts |= htole32(NGE_TXEXTSTS_IPCSUM);
		if ((m->m_pkthdr.csum_flags & CSUM_TCP) != 0)
			desc->nge_extsts |= htole32(NGE_TXEXTSTS_TCPCSUM);
		if ((m->m_pkthdr.csum_flags & CSUM_UDP) != 0)
			desc->nge_extsts |= htole32(NGE_TXEXTSTS_UDPCSUM);
	}
	/* Lastly, turn the first descriptor ownership to hardware. */
	desc->nge_cmdsts |= htole32(NGE_CMDSTS_OWN);

	txd = &sc->nge_cdata.nge_txdesc[prod];
	map = txd_last->tx_dmamap;
	txd_last->tx_dmamap = txd->tx_dmamap;
	txd->tx_dmamap = map;
	txd->tx_m = m;

	return (0);
}

/*
 * Main transmit routine. To avoid having to do mbuf copies, we put pointers
 * to the mbuf data regions directly in the transmit lists. We also save a
 * copy of the pointers since the transmit list fragment pointers are
 * physical addresses.
 */

static void
nge_start(struct ifnet *ifp)
{
	struct nge_softc *sc;

	sc = ifp->if_softc;
	NGE_LOCK(sc);
	nge_start_locked(ifp);
	NGE_UNLOCK(sc);
}

static void
nge_start_locked(struct ifnet *ifp)
{
	struct nge_softc *sc;
	struct mbuf *m_head;
	int enq;

	sc = ifp->if_softc;

	NGE_LOCK_ASSERT(sc);

	if ((ifp->if_drv_flags & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) !=
	    IFF_DRV_RUNNING || (sc->nge_flags & NGE_FLAG_LINK) == 0)
		return;

	for (enq = 0; !IFQ_DRV_IS_EMPTY(&ifp->if_snd) &&
	    sc->nge_cdata.nge_tx_cnt < NGE_TX_RING_CNT - 2; ) {
		IFQ_DRV_DEQUEUE(&ifp->if_snd, m_head);
		if (m_head == NULL)
			break;
		/*
		 * Pack the data into the transmit ring. If we
		 * don't have room, set the OACTIVE flag and wait
		 * for the NIC to drain the ring.
		 */
		if (nge_encap(sc, &m_head)) {
			if (m_head == NULL)
				break;
			IFQ_DRV_PREPEND(&ifp->if_snd, m_head);
			ifp->if_drv_flags |= IFF_DRV_OACTIVE;
			break;
		}

		enq++;
		/*
		 * If there's a BPF listener, bounce a copy of this frame
		 * to him.
		 */
		ETHER_BPF_MTAP(ifp, m_head);
	}

	if (enq > 0) {
		bus_dmamap_sync(sc->nge_cdata.nge_tx_ring_tag,
		    sc->nge_cdata.nge_tx_ring_map,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
		/* Transmit */
		NGE_SETBIT(sc, NGE_CSR, NGE_CSR_TX_ENABLE);

		/* Set a timeout in case the chip goes out to lunch. */
		sc->nge_watchdog_timer = 5;
	}
}

static void
nge_init(void *xsc)
{
	struct nge_softc *sc = xsc;

	NGE_LOCK(sc);
	nge_init_locked(sc);
	NGE_UNLOCK(sc);
}

static void
nge_init_locked(struct nge_softc *sc)
{
	struct ifnet *ifp = sc->nge_ifp;
	struct mii_data *mii;
	uint8_t *eaddr;
	uint32_t reg;

	NGE_LOCK_ASSERT(sc);

	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
		return;

	/*
	 * Cancel pending I/O and free all RX/TX buffers.
	 */
	nge_stop(sc);

	/* Reset the adapter. */
	nge_reset(sc);

	/* Disable Rx filter prior to programming Rx filter. */
	CSR_WRITE_4(sc, NGE_RXFILT_CTL, 0);
	CSR_BARRIER_4(sc, NGE_RXFILT_CTL, BUS_SPACE_BARRIER_WRITE);

	mii = device_get_softc(sc->nge_miibus);

	/* Set MAC address. */
	eaddr = IF_LLADDR(sc->nge_ifp);
	CSR_WRITE_4(sc, NGE_RXFILT_CTL, NGE_FILTADDR_PAR0);
	CSR_WRITE_4(sc, NGE_RXFILT_DATA, (eaddr[1] << 8) | eaddr[0]);
	CSR_WRITE_4(sc, NGE_RXFILT_CTL, NGE_FILTADDR_PAR1);
	CSR_WRITE_4(sc, NGE_RXFILT_DATA, (eaddr[3] << 8) | eaddr[2]);
	CSR_WRITE_4(sc, NGE_RXFILT_CTL, NGE_FILTADDR_PAR2);
	CSR_WRITE_4(sc, NGE_RXFILT_DATA, (eaddr[5] << 8) | eaddr[4]);

	/* Init circular RX list. */
	if (nge_list_rx_init(sc) == ENOBUFS) {
		device_printf(sc->nge_dev, "initialization failed: no "
			"memory for rx buffers\n");
		nge_stop(sc);
		return;
	}

	/*
	 * Init tx descriptors.
	 */
	nge_list_tx_init(sc);

	/* Set Rx filter. */
	nge_rxfilter(sc);

	/* Disable PRIQ ctl. */
	CSR_WRITE_4(sc, NGE_PRIOQCTL, 0);

	/*
	 * Set pause frames parameters.
	 *  Rx stat FIFO hi-threshold : 2 or more packets
	 *  Rx stat FIFO lo-threshold : less than 2 packets
	 *  Rx data FIFO hi-threshold : 2K or more bytes
	 *  Rx data FIFO lo-threshold : less than 2K bytes
	 *  pause time : (512ns * 0xffff) -> 33.55ms
	 */
	CSR_WRITE_4(sc, NGE_PAUSECSR,
	    NGE_PAUSECSR_PAUSE_ON_MCAST |
	    NGE_PAUSECSR_PAUSE_ON_DA |
	    ((1 << 24) & NGE_PAUSECSR_RX_STATFIFO_THR_HI) |
	    ((1 << 22) & NGE_PAUSECSR_RX_STATFIFO_THR_LO) |
	    ((1 << 20) & NGE_PAUSECSR_RX_DATAFIFO_THR_HI) |
	    ((1 << 18) & NGE_PAUSECSR_RX_DATAFIFO_THR_LO) |
	    NGE_PAUSECSR_CNT);

	/*
	 * Load the address of the RX and TX lists.
	 */
	CSR_WRITE_4(sc, NGE_RX_LISTPTR_HI,
	    NGE_ADDR_HI(sc->nge_rdata.nge_rx_ring_paddr));
	CSR_WRITE_4(sc, NGE_RX_LISTPTR_LO,
	    NGE_ADDR_LO(sc->nge_rdata.nge_rx_ring_paddr));
	CSR_WRITE_4(sc, NGE_TX_LISTPTR_HI,
	    NGE_ADDR_HI(sc->nge_rdata.nge_tx_ring_paddr));
	CSR_WRITE_4(sc, NGE_TX_LISTPTR_LO,
	    NGE_ADDR_LO(sc->nge_rdata.nge_tx_ring_paddr));

	/* Set RX configuration. */
	CSR_WRITE_4(sc, NGE_RX_CFG, NGE_RXCFG);

	CSR_WRITE_4(sc, NGE_VLAN_IP_RXCTL, 0);
	/*
	 * Enable hardware checksum validation for all IPv4
	 * packets, do not reject packets with bad checksums.
	 */
	if ((ifp->if_capenable & IFCAP_RXCSUM) != 0)
		NGE_SETBIT(sc, NGE_VLAN_IP_RXCTL, NGE_VIPRXCTL_IPCSUM_ENB);

	/*
	 * Tell the chip to detect and strip VLAN tag info from
	 * received frames. The tag will be provided in the extsts
	 * field in the RX descriptors.
	 */
	NGE_SETBIT(sc, NGE_VLAN_IP_RXCTL, NGE_VIPRXCTL_TAG_DETECT_ENB);
	if ((ifp->if_capenable & IFCAP_VLAN_HWTAGGING) != 0)
		NGE_SETBIT(sc, NGE_VLAN_IP_RXCTL, NGE_VIPRXCTL_TAG_STRIP_ENB);

	/* Set TX configuration. */
	CSR_WRITE_4(sc, NGE_TX_CFG, NGE_TXCFG);

	/*
	 * Enable TX IPv4 checksumming on a per-packet basis.
	 */
	CSR_WRITE_4(sc, NGE_VLAN_IP_TXCTL, NGE_VIPTXCTL_CSUM_PER_PKT);

	/*
	 * Tell the chip to insert VLAN tags on a per-packet basis as
	 * dictated by the code in the frame encapsulation routine.
	 */
	NGE_SETBIT(sc, NGE_VLAN_IP_TXCTL, NGE_VIPTXCTL_TAG_PER_PKT);

	/*
	 * Enable the delivery of PHY interrupts based on
	 * link/speed/duplex status changes. Also enable the
	 * extsts field in the DMA descriptors (needed for
	 * TCP/IP checksum offload on transmit).
	 */
	NGE_SETBIT(sc, NGE_CFG, NGE_CFG_PHYINTR_SPD |
	    NGE_CFG_PHYINTR_LNK | NGE_CFG_PHYINTR_DUP | NGE_CFG_EXTSTS_ENB);

	/*
	 * Configure interrupt holdoff (moderation). We can
	 * have the chip delay interrupt delivery for a certain
	 * period. Units are in 100us, and the max setting
	 * is 25500us (0xFF x 100us). Default is a 100us holdoff.
	 */
	CSR_WRITE_4(sc, NGE_IHR, sc->nge_int_holdoff);

	/*
	 * Enable MAC statistics counters and clear.
	 */
	reg = CSR_READ_4(sc, NGE_MIBCTL);
	reg &= ~NGE_MIBCTL_FREEZE_CNT;
	reg |= NGE_MIBCTL_CLEAR_CNT;
	CSR_WRITE_4(sc, NGE_MIBCTL, reg);

	/*
	 * Enable interrupts.
	 */
	CSR_WRITE_4(sc, NGE_IMR, NGE_INTRS);
#ifdef DEVICE_POLLING
	/*
	 * ... only enable interrupts if we are not polling, make sure
	 * they are off otherwise.
	 */
	if ((ifp->if_capenable & IFCAP_POLLING) != 0)
		CSR_WRITE_4(sc, NGE_IER, 0);
	else
#endif
	CSR_WRITE_4(sc, NGE_IER, 1);

	sc->nge_flags &= ~NGE_FLAG_LINK;
	mii_mediachg(mii);

	sc->nge_watchdog_timer = 0;
	callout_reset(&sc->nge_stat_ch, hz, nge_tick, sc);

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
}

/*
 * Set media options.
 */
static int
nge_mediachange(struct ifnet *ifp)
{
	struct nge_softc *sc;
	struct mii_data	*mii;
	struct mii_softc *miisc;
	int error;

	sc = ifp->if_softc;
	NGE_LOCK(sc);
	mii = device_get_softc(sc->nge_miibus);
	LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
		PHY_RESET(miisc);
	error = mii_mediachg(mii);
	NGE_UNLOCK(sc);

	return (error);
}

/*
 * Report current media status.
 */
static void
nge_mediastatus(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct nge_softc *sc;
	struct mii_data *mii;

	sc = ifp->if_softc;
	NGE_LOCK(sc);
	mii = device_get_softc(sc->nge_miibus);
	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
	NGE_UNLOCK(sc);
}

static int
nge_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct nge_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *) data;
	struct mii_data *mii;
	int error = 0, mask;

	switch (command) {
	case SIOCSIFMTU:
		if (ifr->ifr_mtu < ETHERMIN || ifr->ifr_mtu > NGE_JUMBO_MTU)
			error = EINVAL;
		else {
			NGE_LOCK(sc);
			ifp->if_mtu = ifr->ifr_mtu;
			/*
			 * Workaround: if the MTU is larger than
			 * 8152 (TX FIFO size minus 64 minus 18), turn off
			 * TX checksum offloading.
			 */
			if (ifr->ifr_mtu >= 8152) {
				ifp->if_capenable &= ~IFCAP_TXCSUM;
				ifp->if_hwassist &= ~NGE_CSUM_FEATURES;
			} else {
				ifp->if_capenable |= IFCAP_TXCSUM;
				ifp->if_hwassist |= NGE_CSUM_FEATURES;
			}
			NGE_UNLOCK(sc);
			VLAN_CAPABILITIES(ifp);
		}
		break;
	case SIOCSIFFLAGS:
		NGE_LOCK(sc);
		if ((ifp->if_flags & IFF_UP) != 0) {
			if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0) {
				if ((ifp->if_flags ^ sc->nge_if_flags) &
				    (IFF_PROMISC | IFF_ALLMULTI))
					nge_rxfilter(sc);
			} else {
				if ((sc->nge_flags & NGE_FLAG_DETACH) == 0)
					nge_init_locked(sc);
			}
		} else {
			if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
				nge_stop(sc);
		}
		sc->nge_if_flags = ifp->if_flags;
		NGE_UNLOCK(sc);
		error = 0;
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		NGE_LOCK(sc);
		if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
			nge_rxfilter(sc);
		NGE_UNLOCK(sc);
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		mii = device_get_softc(sc->nge_miibus);
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, command);
		break;
	case SIOCSIFCAP:
		NGE_LOCK(sc);
		mask = ifr->ifr_reqcap ^ ifp->if_capenable;
#ifdef DEVICE_POLLING
		if ((mask & IFCAP_POLLING) != 0 &&
		    (IFCAP_POLLING & ifp->if_capabilities) != 0) {
			ifp->if_capenable ^= IFCAP_POLLING;
			if ((IFCAP_POLLING & ifp->if_capenable) != 0) {
				error = ether_poll_register(nge_poll, ifp);
				if (error != 0) {
					NGE_UNLOCK(sc);
					break;
				}
				/* Disable interrupts. */
				CSR_WRITE_4(sc, NGE_IER, 0);
			} else {
				error = ether_poll_deregister(ifp);
				/* Enable interrupts. */
				CSR_WRITE_4(sc, NGE_IER, 1);
			}
		}
#endif /* DEVICE_POLLING */
		if ((mask & IFCAP_TXCSUM) != 0 &&
		    (IFCAP_TXCSUM & ifp->if_capabilities) != 0) {
			ifp->if_capenable ^= IFCAP_TXCSUM;
			if ((IFCAP_TXCSUM & ifp->if_capenable) != 0)
				ifp->if_hwassist |= NGE_CSUM_FEATURES;
			else
				ifp->if_hwassist &= ~NGE_CSUM_FEATURES;
		}
		if ((mask & IFCAP_RXCSUM) != 0 &&
		    (IFCAP_RXCSUM & ifp->if_capabilities) != 0)
			ifp->if_capenable ^= IFCAP_RXCSUM;

		if ((mask & IFCAP_WOL) != 0 &&
		    (ifp->if_capabilities & IFCAP_WOL) != 0) {
			if ((mask & IFCAP_WOL_UCAST) != 0)
				ifp->if_capenable ^= IFCAP_WOL_UCAST;
			if ((mask & IFCAP_WOL_MCAST) != 0)
				ifp->if_capenable ^= IFCAP_WOL_MCAST;
			if ((mask & IFCAP_WOL_MAGIC) != 0)
				ifp->if_capenable ^= IFCAP_WOL_MAGIC;
		}

		if ((mask & IFCAP_VLAN_HWCSUM) != 0 &&
		    (ifp->if_capabilities & IFCAP_VLAN_HWCSUM) != 0)
			ifp->if_capenable ^= IFCAP_VLAN_HWCSUM;
		if ((mask & IFCAP_VLAN_HWTAGGING) != 0 &&
		    (ifp->if_capabilities & IFCAP_VLAN_HWTAGGING) != 0) {
			ifp->if_capenable ^= IFCAP_VLAN_HWTAGGING;
			if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0) {
				if ((ifp->if_capenable &
				    IFCAP_VLAN_HWTAGGING) != 0)
					NGE_SETBIT(sc,
					    NGE_VLAN_IP_RXCTL,
					    NGE_VIPRXCTL_TAG_STRIP_ENB);
				else
					NGE_CLRBIT(sc,
					    NGE_VLAN_IP_RXCTL,
					    NGE_VIPRXCTL_TAG_STRIP_ENB);
			}
		}
		/*
		 * Both VLAN hardware tagging and checksum offload is
		 * required to do checksum offload on VLAN interface.
		 */
		if ((ifp->if_capenable & IFCAP_TXCSUM) == 0)
			ifp->if_capenable &= ~IFCAP_VLAN_HWCSUM;
		if ((ifp->if_capenable & IFCAP_VLAN_HWTAGGING) == 0)
			ifp->if_capenable &= ~IFCAP_VLAN_HWCSUM;
		NGE_UNLOCK(sc);
		VLAN_CAPABILITIES(ifp);
		break;
	default:
		error = ether_ioctl(ifp, command, data);
		break;
	}

	return (error);
}

static void
nge_watchdog(struct nge_softc *sc)
{
	struct ifnet *ifp;

	NGE_LOCK_ASSERT(sc);

	if (sc->nge_watchdog_timer == 0 || --sc->nge_watchdog_timer)
		return;

	ifp = sc->nge_ifp;
	if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
	if_printf(ifp, "watchdog timeout\n");

	ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
	nge_init_locked(sc);

	if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd))
		nge_start_locked(ifp);
}

static int
nge_stop_mac(struct nge_softc *sc)
{
	uint32_t reg;
	int i;

	NGE_LOCK_ASSERT(sc);

	reg = CSR_READ_4(sc, NGE_CSR);
	if ((reg & (NGE_CSR_TX_ENABLE | NGE_CSR_RX_ENABLE)) != 0) {
		reg &= ~(NGE_CSR_TX_ENABLE | NGE_CSR_RX_ENABLE);
		reg |= NGE_CSR_TX_DISABLE | NGE_CSR_RX_DISABLE;
		CSR_WRITE_4(sc, NGE_CSR, reg);
		for (i = 0; i < NGE_TIMEOUT; i++) {
			DELAY(1);
			if ((CSR_READ_4(sc, NGE_CSR) &
			    (NGE_CSR_RX_ENABLE | NGE_CSR_TX_ENABLE)) == 0)
				break;
		}
		if (i == NGE_TIMEOUT)
			return (ETIMEDOUT);
	}

	return (0);
}

/*
 * Stop the adapter and free any mbufs allocated to the
 * RX and TX lists.
 */
static void
nge_stop(struct nge_softc *sc)
{
	struct nge_txdesc *txd;
	struct nge_rxdesc *rxd;
	int i;
	struct ifnet *ifp;

	NGE_LOCK_ASSERT(sc);
	ifp = sc->nge_ifp;

	ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);
	sc->nge_flags &= ~NGE_FLAG_LINK;
	callout_stop(&sc->nge_stat_ch);
	sc->nge_watchdog_timer = 0;

	CSR_WRITE_4(sc, NGE_IER, 0);
	CSR_WRITE_4(sc, NGE_IMR, 0);
	if (nge_stop_mac(sc) == ETIMEDOUT)
		device_printf(sc->nge_dev,
		   "%s: unable to stop Tx/Rx MAC\n", __func__);
	CSR_WRITE_4(sc, NGE_TX_LISTPTR_HI, 0);
	CSR_WRITE_4(sc, NGE_TX_LISTPTR_LO, 0);
	CSR_WRITE_4(sc, NGE_RX_LISTPTR_HI, 0);
	CSR_WRITE_4(sc, NGE_RX_LISTPTR_LO, 0);
	nge_stats_update(sc);
	if (sc->nge_head != NULL) {
		m_freem(sc->nge_head);
		sc->nge_head = sc->nge_tail = NULL;
	}

	/*
	 * Free RX and TX mbufs still in the queues.
	 */
	for (i = 0; i < NGE_RX_RING_CNT; i++) {
		rxd = &sc->nge_cdata.nge_rxdesc[i];
		if (rxd->rx_m != NULL) {
			bus_dmamap_sync(sc->nge_cdata.nge_rx_tag,
			    rxd->rx_dmamap, BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(sc->nge_cdata.nge_rx_tag,
			    rxd->rx_dmamap);
			m_freem(rxd->rx_m);
			rxd->rx_m = NULL;
		}
	}
	for (i = 0; i < NGE_TX_RING_CNT; i++) {
		txd = &sc->nge_cdata.nge_txdesc[i];
		if (txd->tx_m != NULL) {
			bus_dmamap_sync(sc->nge_cdata.nge_tx_tag,
			    txd->tx_dmamap, BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->nge_cdata.nge_tx_tag,
			    txd->tx_dmamap);
			m_freem(txd->tx_m);
			txd->tx_m = NULL;
		}
	}
}

/*
 * Before setting WOL bits, caller should have stopped Receiver.
 */
static void
nge_wol(struct nge_softc *sc)
{
	struct ifnet *ifp;
	uint32_t reg;
	uint16_t pmstat;
	int pmc;

	NGE_LOCK_ASSERT(sc);

	if (pci_find_cap(sc->nge_dev, PCIY_PMG, &pmc) != 0)
		return;

	ifp = sc->nge_ifp;
	if ((ifp->if_capenable & IFCAP_WOL) == 0) {
		/* Disable WOL & disconnect CLKRUN to save power. */
		CSR_WRITE_4(sc, NGE_WOLCSR, 0);
		CSR_WRITE_4(sc, NGE_CLKRUN, 0);
	} else {
		if (nge_stop_mac(sc) == ETIMEDOUT)
			device_printf(sc->nge_dev,
			    "%s: unable to stop Tx/Rx MAC\n", __func__);
		/*
		 * Make sure wake frames will be buffered in the Rx FIFO.
		 * (i.e. Silent Rx mode.)
		 */
		CSR_WRITE_4(sc, NGE_RX_LISTPTR_HI, 0);
		CSR_BARRIER_4(sc, NGE_RX_LISTPTR_HI, BUS_SPACE_BARRIER_WRITE);
		CSR_WRITE_4(sc, NGE_RX_LISTPTR_LO, 0);
		CSR_BARRIER_4(sc, NGE_RX_LISTPTR_LO, BUS_SPACE_BARRIER_WRITE);
		/* Enable Rx again. */
		NGE_SETBIT(sc, NGE_CSR, NGE_CSR_RX_ENABLE);
		CSR_BARRIER_4(sc, NGE_CSR, BUS_SPACE_BARRIER_WRITE);

		/* Configure WOL events. */
		reg = 0;
		if ((ifp->if_capenable & IFCAP_WOL_UCAST) != 0)
			reg |= NGE_WOLCSR_WAKE_ON_UNICAST;
		if ((ifp->if_capenable & IFCAP_WOL_MCAST) != 0)
			reg |= NGE_WOLCSR_WAKE_ON_MULTICAST;
		if ((ifp->if_capenable & IFCAP_WOL_MAGIC) != 0)
			reg |= NGE_WOLCSR_WAKE_ON_MAGICPKT;
		CSR_WRITE_4(sc, NGE_WOLCSR, reg);

		/* Activate CLKRUN. */
		reg = CSR_READ_4(sc, NGE_CLKRUN);
		reg |= NGE_CLKRUN_PMEENB | NGE_CLNRUN_CLKRUN_ENB;
		CSR_WRITE_4(sc, NGE_CLKRUN, reg);
	}

	/* Request PME. */
	pmstat = pci_read_config(sc->nge_dev, pmc + PCIR_POWER_STATUS, 2);
	pmstat &= ~(PCIM_PSTAT_PME | PCIM_PSTAT_PMEENABLE);
	if ((ifp->if_capenable & IFCAP_WOL) != 0)
		pmstat |= PCIM_PSTAT_PME | PCIM_PSTAT_PMEENABLE;
	pci_write_config(sc->nge_dev, pmc + PCIR_POWER_STATUS, pmstat, 2);
}

/*
 * Stop all chip I/O so that the kernel's probe routines don't
 * get confused by errant DMAs when rebooting.
 */
static int
nge_shutdown(device_t dev)
{

	return (nge_suspend(dev));
}

static int
nge_suspend(device_t dev)
{
	struct nge_softc *sc;

	sc = device_get_softc(dev);

	NGE_LOCK(sc);
	nge_stop(sc);
	nge_wol(sc);
	sc->nge_flags |= NGE_FLAG_SUSPENDED;
	NGE_UNLOCK(sc);

	return (0);
}

static int
nge_resume(device_t dev)
{
	struct nge_softc *sc;
	struct ifnet *ifp;
	uint16_t pmstat;
	int pmc;

	sc = device_get_softc(dev);

	NGE_LOCK(sc);
	ifp = sc->nge_ifp;
	if (pci_find_cap(sc->nge_dev, PCIY_PMG, &pmc) == 0) {
		/* Disable PME and clear PME status. */
		pmstat = pci_read_config(sc->nge_dev,
		    pmc + PCIR_POWER_STATUS, 2);
		if ((pmstat & PCIM_PSTAT_PMEENABLE) != 0) {
			pmstat &= ~PCIM_PSTAT_PMEENABLE;
			pci_write_config(sc->nge_dev,
			    pmc + PCIR_POWER_STATUS, pmstat, 2);
		}
	}
	if (ifp->if_flags & IFF_UP) {
		ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
		nge_init_locked(sc);
	}

	sc->nge_flags &= ~NGE_FLAG_SUSPENDED;
	NGE_UNLOCK(sc);

	return (0);
}

#define	NGE_SYSCTL_STAT_ADD32(c, h, n, p, d)	\
	    SYSCTL_ADD_UINT(c, h, OID_AUTO, n, CTLFLAG_RD, p, 0, d)

static void
nge_sysctl_node(struct nge_softc *sc)
{
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid_list *child, *parent;
	struct sysctl_oid *tree;
	struct nge_stats *stats;
	int error;

	ctx = device_get_sysctl_ctx(sc->nge_dev);
	child = SYSCTL_CHILDREN(device_get_sysctl_tree(sc->nge_dev));
	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "int_holdoff",
	    CTLTYPE_INT | CTLFLAG_RW, &sc->nge_int_holdoff, 0,
	    sysctl_hw_nge_int_holdoff, "I", "NGE interrupt moderation");
	/* Pull in device tunables. */
	sc->nge_int_holdoff = NGE_INT_HOLDOFF_DEFAULT;
	error = resource_int_value(device_get_name(sc->nge_dev),
	    device_get_unit(sc->nge_dev), "int_holdoff", &sc->nge_int_holdoff);
	if (error == 0) {
		if (sc->nge_int_holdoff < NGE_INT_HOLDOFF_MIN ||
		    sc->nge_int_holdoff > NGE_INT_HOLDOFF_MAX ) {
			device_printf(sc->nge_dev,
			    "int_holdoff value out of range; "
			    "using default: %d(%d us)\n",
			    NGE_INT_HOLDOFF_DEFAULT,
			    NGE_INT_HOLDOFF_DEFAULT * 100);
			sc->nge_int_holdoff = NGE_INT_HOLDOFF_DEFAULT;
		}
	}

	stats = &sc->nge_stats;
	tree = SYSCTL_ADD_NODE(ctx, child, OID_AUTO, "stats", CTLFLAG_RD,
	    NULL, "NGE statistics");
	parent = SYSCTL_CHILDREN(tree);

	/* Rx statistics. */
	tree = SYSCTL_ADD_NODE(ctx, parent, OID_AUTO, "rx", CTLFLAG_RD,
	    NULL, "Rx MAC statistics");
	child = SYSCTL_CHILDREN(tree);
	NGE_SYSCTL_STAT_ADD32(ctx, child, "pkts_errs",
	    &stats->rx_pkts_errs,
	    "Packet errors including both wire errors and FIFO overruns");
	NGE_SYSCTL_STAT_ADD32(ctx, child, "crc_errs",
	    &stats->rx_crc_errs, "CRC errors");
	NGE_SYSCTL_STAT_ADD32(ctx, child, "fifo_oflows",
	    &stats->rx_fifo_oflows, "FIFO overflows");
	NGE_SYSCTL_STAT_ADD32(ctx, child, "align_errs",
	    &stats->rx_align_errs, "Frame alignment errors");
	NGE_SYSCTL_STAT_ADD32(ctx, child, "sym_errs",
	    &stats->rx_sym_errs, "One or more symbol errors");
	NGE_SYSCTL_STAT_ADD32(ctx, child, "pkts_jumbos",
	    &stats->rx_pkts_jumbos,
	    "Packets received with length greater than 1518 bytes");
	NGE_SYSCTL_STAT_ADD32(ctx, child, "len_errs",
	    &stats->rx_len_errs, "In Range Length errors");
	NGE_SYSCTL_STAT_ADD32(ctx, child, "unctl_frames",
	    &stats->rx_unctl_frames, "Control frames with unsupported opcode");
	NGE_SYSCTL_STAT_ADD32(ctx, child, "pause",
	    &stats->rx_pause, "Pause frames");

	/* Tx statistics. */
	tree = SYSCTL_ADD_NODE(ctx, parent, OID_AUTO, "tx", CTLFLAG_RD,
	    NULL, "Tx MAC statistics");
	child = SYSCTL_CHILDREN(tree);
	NGE_SYSCTL_STAT_ADD32(ctx, child, "pause",
	    &stats->tx_pause, "Pause frames");
	NGE_SYSCTL_STAT_ADD32(ctx, child, "seq_errs",
	    &stats->tx_seq_errs,
	    "Loss of collision heartbeat during transmission");
}

#undef NGE_SYSCTL_STAT_ADD32

static int
sysctl_int_range(SYSCTL_HANDLER_ARGS, int low, int high)
{
	int error, value;

	if (arg1 == NULL)
		return (EINVAL);
	value = *(int *)arg1;
	error = sysctl_handle_int(oidp, &value, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	if (value < low || value > high)
		return (EINVAL);
	*(int *)arg1 = value;

	return (0);
}

static int
sysctl_hw_nge_int_holdoff(SYSCTL_HANDLER_ARGS)
{

	return (sysctl_int_range(oidp, arg1, arg2, req, NGE_INT_HOLDOFF_MIN,
	    NGE_INT_HOLDOFF_MAX));
}
