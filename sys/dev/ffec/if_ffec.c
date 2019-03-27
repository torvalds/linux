/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Ian Lepore <ian@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Driver for Freescale Fast Ethernet Controller, found on imx-series SoCs among
 * others.  Also works for the ENET Gigibit controller found on imx6 and imx28,
 * but the driver doesn't currently use any of the ENET advanced features other
 * than enabling gigabit.
 *
 * The interface name 'fec' is already taken by netgraph's Fast Etherchannel
 * (netgraph/ng_fec.c), so we use 'ffec'.
 *
 * Requires an FDT entry with at least these properties:
 *   fec: ethernet@02188000 {
 *      compatible = "fsl,imxNN-fec";
 *      reg = <0x02188000 0x4000>;
 *      interrupts = <150 151>;
 *      phy-mode = "rgmii";
 *      phy-disable-preamble; // optional
 *   };
 * The second interrupt number is for IEEE-1588, and is not currently used; it
 * need not be present.  phy-mode must be one of: "mii", "rmii", "rgmii".
 * There is also an optional property, phy-disable-preamble, which if present
 * will disable the preamble bits, cutting the size of each mdio transaction
 * (and thus the busy-wait time) in half.
 */

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

#include <machine/bus.h>

#include <net/bpf.h>
#include <net/if.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/if_var.h>
#include <net/if_vlan_var.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ffec/if_ffecreg.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>
#include <dev/mii/mii_fdt.h>
#include "miibus_if.h"

/*
 * There are small differences in the hardware on various SoCs.  Not every SoC
 * we support has its own FECTYPE; most work as GENERIC and only the ones that
 * need different handling get their own entry.  In addition to the types in
 * this list, there are some flags below that can be ORed into the upper bits.
 */
enum {
	FECTYPE_NONE,
	FECTYPE_GENERIC,
	FECTYPE_IMX53,
	FECTYPE_IMX6,	/* imx6 and imx7 */
	FECTYPE_MVF,
};

/*
 * Flags that describe general differences between the FEC hardware in various
 * SoCs.  These are ORed into the FECTYPE enum values in the ofw_compat_data, so
 * the low 8 bits are reserved for the type enum.  In the softc, the type and
 * flags are put into separate members, so that you don't need to mask the flags
 * out of the type to compare it.
 */
#define	FECTYPE_MASK		0x000000ff
#define	FECFLAG_GBE		(1 <<  8)
#define	FECFLAG_AVB		(1 <<  9)
#define	FECFLAG_RACC		(1 << 10)

/*
 * Table of supported FDT compat strings and their associated FECTYPE values.
 */
static struct ofw_compat_data compat_data[] = {
	{"fsl,imx51-fec",	FECTYPE_GENERIC},
	{"fsl,imx53-fec",	FECTYPE_IMX53},
	{"fsl,imx6q-fec",	FECTYPE_IMX6 | FECFLAG_RACC | FECFLAG_GBE },
	{"fsl,imx6ul-fec",	FECTYPE_IMX6 | FECFLAG_RACC },
	{"fsl,imx7d-fec",	FECTYPE_IMX6 | FECFLAG_RACC | FECFLAG_GBE |
				FECFLAG_AVB },
	{"fsl,mvf600-fec",	FECTYPE_MVF  | FECFLAG_RACC },
	{"fsl,mvf-fec",		FECTYPE_MVF},
	{NULL,		 	FECTYPE_NONE},
};

/*
 * Driver data and defines.
 */
#define	RX_DESC_COUNT	64
#define	RX_DESC_SIZE	(sizeof(struct ffec_hwdesc) * RX_DESC_COUNT)
#define	TX_DESC_COUNT	64
#define	TX_DESC_SIZE	(sizeof(struct ffec_hwdesc) * TX_DESC_COUNT)

#define	WATCHDOG_TIMEOUT_SECS	5

#define	MAX_IRQ_COUNT 3

struct ffec_bufmap {
	struct mbuf	*mbuf;
	bus_dmamap_t	map;
};

struct ffec_softc {
	device_t		dev;
	device_t		miibus;
	struct mii_data *	mii_softc;
	struct ifnet		*ifp;
	int			if_flags;
	struct mtx		mtx;
	struct resource		*irq_res[MAX_IRQ_COUNT];
	struct resource		*mem_res;
	void *			intr_cookie[MAX_IRQ_COUNT];
	struct callout		ffec_callout;
	mii_contype_t		phy_conn_type;
	uint32_t		fecflags;
	uint8_t			fectype;
	boolean_t		link_is_up;
	boolean_t		is_attached;
	boolean_t		is_detaching;
	int			tx_watchdog_count;
	int			rxbuf_align;
	int			txbuf_align;

	bus_dma_tag_t		rxdesc_tag;
	bus_dmamap_t		rxdesc_map;
	struct ffec_hwdesc	*rxdesc_ring;
	bus_addr_t		rxdesc_ring_paddr;
	bus_dma_tag_t		rxbuf_tag;
	struct ffec_bufmap	rxbuf_map[RX_DESC_COUNT];
	uint32_t		rx_idx;

	bus_dma_tag_t		txdesc_tag;
	bus_dmamap_t		txdesc_map;
	struct ffec_hwdesc	*txdesc_ring;
	bus_addr_t		txdesc_ring_paddr;
	bus_dma_tag_t		txbuf_tag;
	struct ffec_bufmap	txbuf_map[TX_DESC_COUNT];
	uint32_t		tx_idx_head;
	uint32_t		tx_idx_tail;
	int			txcount;
};

static struct resource_spec irq_res_spec[MAX_IRQ_COUNT + 1] = {
	{ SYS_RES_IRQ,		0,	RF_ACTIVE },
	{ SYS_RES_IRQ,		1,	RF_ACTIVE | RF_OPTIONAL },
	{ SYS_RES_IRQ,		2,	RF_ACTIVE | RF_OPTIONAL },
	RESOURCE_SPEC_END
};

#define	FFEC_LOCK(sc)			mtx_lock(&(sc)->mtx)
#define	FFEC_UNLOCK(sc)			mtx_unlock(&(sc)->mtx)
#define	FFEC_LOCK_INIT(sc)		mtx_init(&(sc)->mtx, \
	    device_get_nameunit((sc)->dev), MTX_NETWORK_LOCK, MTX_DEF)
#define	FFEC_LOCK_DESTROY(sc)		mtx_destroy(&(sc)->mtx);
#define	FFEC_ASSERT_LOCKED(sc)		mtx_assert(&(sc)->mtx, MA_OWNED);
#define	FFEC_ASSERT_UNLOCKED(sc)	mtx_assert(&(sc)->mtx, MA_NOTOWNED);

static void ffec_init_locked(struct ffec_softc *sc);
static void ffec_stop_locked(struct ffec_softc *sc);
static void ffec_txstart_locked(struct ffec_softc *sc);
static void ffec_txfinish_locked(struct ffec_softc *sc);

static inline uint16_t
RD2(struct ffec_softc *sc, bus_size_t off)
{

	return (bus_read_2(sc->mem_res, off));
}

static inline void
WR2(struct ffec_softc *sc, bus_size_t off, uint16_t val)
{

	bus_write_2(sc->mem_res, off, val);
}

static inline uint32_t
RD4(struct ffec_softc *sc, bus_size_t off)
{

	return (bus_read_4(sc->mem_res, off));
}

static inline void
WR4(struct ffec_softc *sc, bus_size_t off, uint32_t val)
{

	bus_write_4(sc->mem_res, off, val);
}

static inline uint32_t
next_rxidx(struct ffec_softc *sc, uint32_t curidx)
{

	return ((curidx == RX_DESC_COUNT - 1) ? 0 : curidx + 1);
}

static inline uint32_t
next_txidx(struct ffec_softc *sc, uint32_t curidx)
{

	return ((curidx == TX_DESC_COUNT - 1) ? 0 : curidx + 1);
}

static void
ffec_get1paddr(void *arg, bus_dma_segment_t *segs, int nsegs, int error)
{

	if (error != 0)
		return;
	*(bus_addr_t *)arg = segs[0].ds_addr;
}

static void
ffec_miigasket_setup(struct ffec_softc *sc)
{
	uint32_t ifmode;

	/*
	 * We only need the gasket for MII and RMII connections on certain SoCs.
	 */

	switch (sc->fectype)
	{
	case FECTYPE_IMX53:
		break;
	default:
		return;
	}

	switch (sc->phy_conn_type)
	{
	case MII_CONTYPE_MII:
		ifmode = 0;
		break;
	case MII_CONTYPE_RMII:
		ifmode = FEC_MIIGSK_CFGR_IF_MODE_RMII;
		break;
	default:
		return;
	}

	/*
	 * Disable the gasket, configure for either MII or RMII, then enable.
	 */

	WR2(sc, FEC_MIIGSK_ENR, 0);
	while (RD2(sc, FEC_MIIGSK_ENR) & FEC_MIIGSK_ENR_READY)
		continue;

	WR2(sc, FEC_MIIGSK_CFGR, ifmode);

	WR2(sc, FEC_MIIGSK_ENR, FEC_MIIGSK_ENR_EN);
	while (!(RD2(sc, FEC_MIIGSK_ENR) & FEC_MIIGSK_ENR_READY))
		continue;
}

static boolean_t
ffec_miibus_iowait(struct ffec_softc *sc)
{
	uint32_t timeout;

	for (timeout = 10000; timeout != 0; --timeout)
		if (RD4(sc, FEC_IER_REG) & FEC_IER_MII)
			return (true);

	return (false);
}

static int
ffec_miibus_readreg(device_t dev, int phy, int reg)
{
	struct ffec_softc *sc;
	int val;

	sc = device_get_softc(dev);

	WR4(sc, FEC_IER_REG, FEC_IER_MII);

	WR4(sc, FEC_MMFR_REG, FEC_MMFR_OP_READ |
	    FEC_MMFR_ST_VALUE | FEC_MMFR_TA_VALUE |
	    ((phy << FEC_MMFR_PA_SHIFT) & FEC_MMFR_PA_MASK) |
	    ((reg << FEC_MMFR_RA_SHIFT) & FEC_MMFR_RA_MASK));

	if (!ffec_miibus_iowait(sc)) {
		device_printf(dev, "timeout waiting for mii read\n");
		return (-1); /* All-ones is a symptom of bad mdio. */
	}

	val = RD4(sc, FEC_MMFR_REG) & FEC_MMFR_DATA_MASK;

	return (val);
}

static int
ffec_miibus_writereg(device_t dev, int phy, int reg, int val)
{
	struct ffec_softc *sc;

	sc = device_get_softc(dev);

	WR4(sc, FEC_IER_REG, FEC_IER_MII);

	WR4(sc, FEC_MMFR_REG, FEC_MMFR_OP_WRITE |
	    FEC_MMFR_ST_VALUE | FEC_MMFR_TA_VALUE |
	    ((phy << FEC_MMFR_PA_SHIFT) & FEC_MMFR_PA_MASK) |
	    ((reg << FEC_MMFR_RA_SHIFT) & FEC_MMFR_RA_MASK) |
	    (val & FEC_MMFR_DATA_MASK));

	if (!ffec_miibus_iowait(sc)) {
		device_printf(dev, "timeout waiting for mii write\n");
		return (-1);
	}

	return (0);
}

static void
ffec_miibus_statchg(device_t dev)
{
	struct ffec_softc *sc;
	struct mii_data *mii;
	uint32_t ecr, rcr, tcr;

	/*
	 * Called by the MII bus driver when the PHY establishes link to set the
	 * MAC interface registers.
	 */

	sc = device_get_softc(dev);

	FFEC_ASSERT_LOCKED(sc);

	mii = sc->mii_softc;

	if (mii->mii_media_status & IFM_ACTIVE)
		sc->link_is_up = true;
	else
		sc->link_is_up = false;

	ecr = RD4(sc, FEC_ECR_REG) & ~FEC_ECR_SPEED;
	rcr = RD4(sc, FEC_RCR_REG) & ~(FEC_RCR_RMII_10T | FEC_RCR_RMII_MODE |
	    FEC_RCR_RGMII_EN | FEC_RCR_DRT | FEC_RCR_FCE);
	tcr = RD4(sc, FEC_TCR_REG) & ~FEC_TCR_FDEN;

	rcr |= FEC_RCR_MII_MODE; /* Must always be on even for R[G]MII. */
	switch (sc->phy_conn_type) {
	case MII_CONTYPE_RMII:
		rcr |= FEC_RCR_RMII_MODE;
		break;
	case MII_CONTYPE_RGMII:
	case MII_CONTYPE_RGMII_ID:
	case MII_CONTYPE_RGMII_RXID:
	case MII_CONTYPE_RGMII_TXID:
		rcr |= FEC_RCR_RGMII_EN;
		break;
	default:
		break;
	}

	switch (IFM_SUBTYPE(mii->mii_media_active)) {
	case IFM_1000_T:
	case IFM_1000_SX:
		ecr |= FEC_ECR_SPEED;
		break;
	case IFM_100_TX:
		/* Not-FEC_ECR_SPEED + not-FEC_RCR_RMII_10T means 100TX */
		break;
	case IFM_10_T:
		rcr |= FEC_RCR_RMII_10T;
		break;
	case IFM_NONE:
		sc->link_is_up = false;
		return;
	default:
		sc->link_is_up = false;
		device_printf(dev, "Unsupported media %u\n",
		    IFM_SUBTYPE(mii->mii_media_active));
		return;
	}

	if ((IFM_OPTIONS(mii->mii_media_active) & IFM_FDX) != 0)
		tcr |= FEC_TCR_FDEN;
	else
		rcr |= FEC_RCR_DRT;

	if ((IFM_OPTIONS(mii->mii_media_active) & IFM_FLOW) != 0)
		rcr |= FEC_RCR_FCE;

	WR4(sc, FEC_RCR_REG, rcr);
	WR4(sc, FEC_TCR_REG, tcr);
	WR4(sc, FEC_ECR_REG, ecr);
}

static void
ffec_media_status(struct ifnet * ifp, struct ifmediareq *ifmr)
{
	struct ffec_softc *sc;
	struct mii_data *mii;


	sc = ifp->if_softc;
	mii = sc->mii_softc;
	FFEC_LOCK(sc);
	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
	FFEC_UNLOCK(sc);
}

static int
ffec_media_change_locked(struct ffec_softc *sc)
{

	return (mii_mediachg(sc->mii_softc));
}

static int
ffec_media_change(struct ifnet * ifp)
{
	struct ffec_softc *sc;
	int error;

	sc = ifp->if_softc;

	FFEC_LOCK(sc);
	error = ffec_media_change_locked(sc);
	FFEC_UNLOCK(sc);
	return (error);
}

static void ffec_clear_stats(struct ffec_softc *sc)
{
	uint32_t mibc;

	mibc = RD4(sc, FEC_MIBC_REG);

	/*
	 * On newer hardware the statistic regs are cleared by toggling a bit in
	 * the mib control register.  On older hardware the clear procedure is
	 * to disable statistics collection, zero the regs, then re-enable.
	 */
	if (sc->fectype == FECTYPE_IMX6 || sc->fectype == FECTYPE_MVF) {
		WR4(sc, FEC_MIBC_REG, mibc | FEC_MIBC_CLEAR);
		WR4(sc, FEC_MIBC_REG, mibc & ~FEC_MIBC_CLEAR);
	} else {
		WR4(sc, FEC_MIBC_REG, mibc | FEC_MIBC_DIS);
	
		WR4(sc, FEC_IEEE_R_DROP, 0);
		WR4(sc, FEC_IEEE_R_MACERR, 0);
		WR4(sc, FEC_RMON_R_CRC_ALIGN, 0);
		WR4(sc, FEC_RMON_R_FRAG, 0);
		WR4(sc, FEC_RMON_R_JAB, 0);
		WR4(sc, FEC_RMON_R_MC_PKT, 0);
		WR4(sc, FEC_RMON_R_OVERSIZE, 0);
		WR4(sc, FEC_RMON_R_PACKETS, 0);
		WR4(sc, FEC_RMON_R_UNDERSIZE, 0);
		WR4(sc, FEC_RMON_T_COL, 0);
		WR4(sc, FEC_RMON_T_CRC_ALIGN, 0);
		WR4(sc, FEC_RMON_T_FRAG, 0);
		WR4(sc, FEC_RMON_T_JAB, 0);
		WR4(sc, FEC_RMON_T_MC_PKT, 0);
		WR4(sc, FEC_RMON_T_OVERSIZE , 0);
		WR4(sc, FEC_RMON_T_PACKETS, 0);
		WR4(sc, FEC_RMON_T_UNDERSIZE, 0);

		WR4(sc, FEC_MIBC_REG, mibc);
	}
}

static void
ffec_harvest_stats(struct ffec_softc *sc)
{
	struct ifnet *ifp;

	ifp = sc->ifp;

	/*
	 * - FEC_IEEE_R_DROP is "dropped due to invalid start frame delimiter"
	 *   so it's really just another type of input error.
	 * - FEC_IEEE_R_MACERR is "no receive fifo space"; count as input drops.
	 */
	if_inc_counter(ifp, IFCOUNTER_IPACKETS, RD4(sc, FEC_RMON_R_PACKETS));
	if_inc_counter(ifp, IFCOUNTER_IMCASTS, RD4(sc, FEC_RMON_R_MC_PKT));
	if_inc_counter(ifp, IFCOUNTER_IERRORS,
	    RD4(sc, FEC_RMON_R_CRC_ALIGN) + RD4(sc, FEC_RMON_R_UNDERSIZE) +
	    RD4(sc, FEC_RMON_R_OVERSIZE) + RD4(sc, FEC_RMON_R_FRAG) +
	    RD4(sc, FEC_RMON_R_JAB) + RD4(sc, FEC_IEEE_R_DROP));

	if_inc_counter(ifp, IFCOUNTER_IQDROPS, RD4(sc, FEC_IEEE_R_MACERR));

	if_inc_counter(ifp, IFCOUNTER_OPACKETS, RD4(sc, FEC_RMON_T_PACKETS));
	if_inc_counter(ifp, IFCOUNTER_OMCASTS, RD4(sc, FEC_RMON_T_MC_PKT));
	if_inc_counter(ifp, IFCOUNTER_OERRORS,
	    RD4(sc, FEC_RMON_T_CRC_ALIGN) + RD4(sc, FEC_RMON_T_UNDERSIZE) +
	    RD4(sc, FEC_RMON_T_OVERSIZE) + RD4(sc, FEC_RMON_T_FRAG) +
	    RD4(sc, FEC_RMON_T_JAB));

	if_inc_counter(ifp, IFCOUNTER_COLLISIONS, RD4(sc, FEC_RMON_T_COL));

	ffec_clear_stats(sc);
}

static void
ffec_tick(void *arg)
{
	struct ffec_softc *sc;
	struct ifnet *ifp;
	int link_was_up;

	sc = arg;

	FFEC_ASSERT_LOCKED(sc);

	ifp = sc->ifp;

	if (!(ifp->if_drv_flags & IFF_DRV_RUNNING))
	    return;

	/*
	 * Typical tx watchdog.  If this fires it indicates that we enqueued
	 * packets for output and never got a txdone interrupt for them.  Maybe
	 * it's a missed interrupt somehow, just pretend we got one.
	 */
	if (sc->tx_watchdog_count > 0) {
		if (--sc->tx_watchdog_count == 0) {
			ffec_txfinish_locked(sc);
		}
	}

	/* Gather stats from hardware counters. */
	ffec_harvest_stats(sc);

	/* Check the media status. */
	link_was_up = sc->link_is_up;
	mii_tick(sc->mii_softc);
	if (sc->link_is_up && !link_was_up)
		ffec_txstart_locked(sc);

	/* Schedule another check one second from now. */
	callout_reset(&sc->ffec_callout, hz, ffec_tick, sc);
}

inline static uint32_t
ffec_setup_txdesc(struct ffec_softc *sc, int idx, bus_addr_t paddr, 
    uint32_t len)
{
	uint32_t nidx;
	uint32_t flags;

	nidx = next_txidx(sc, idx);

	/* Addr/len 0 means we're clearing the descriptor after xmit done. */
	if (paddr == 0 || len == 0) {
		flags = 0;
		--sc->txcount;
	} else {
		flags = FEC_TXDESC_READY | FEC_TXDESC_L | FEC_TXDESC_TC;
		++sc->txcount;
	}
	if (nidx == 0)
		flags |= FEC_TXDESC_WRAP;

	/*
	 * The hardware requires 32-bit physical addresses.  We set up the dma
	 * tag to indicate that, so the cast to uint32_t should never lose
	 * significant bits.
	 */
	sc->txdesc_ring[idx].buf_paddr = (uint32_t)paddr;
	sc->txdesc_ring[idx].flags_len = flags | len; /* Must be set last! */

	return (nidx);
}

static int
ffec_setup_txbuf(struct ffec_softc *sc, int idx, struct mbuf **mp)
{
	struct mbuf * m;
	int error, nsegs;
	struct bus_dma_segment seg;

	if ((m = m_defrag(*mp, M_NOWAIT)) == NULL)
		return (ENOMEM);
	*mp = m;

	error = bus_dmamap_load_mbuf_sg(sc->txbuf_tag, sc->txbuf_map[idx].map,
	    m, &seg, &nsegs, 0);
	if (error != 0) {
		return (ENOMEM);
	}
	bus_dmamap_sync(sc->txbuf_tag, sc->txbuf_map[idx].map, 
	    BUS_DMASYNC_PREWRITE);

	sc->txbuf_map[idx].mbuf = m;
	ffec_setup_txdesc(sc, idx, seg.ds_addr, seg.ds_len);

	return (0);

}

static void
ffec_txstart_locked(struct ffec_softc *sc)
{
	struct ifnet *ifp;
	struct mbuf *m;
	int enqueued;

	FFEC_ASSERT_LOCKED(sc);

	if (!sc->link_is_up)
		return;

	ifp = sc->ifp;

	if (ifp->if_drv_flags & IFF_DRV_OACTIVE)
		return;

	enqueued = 0;

	for (;;) {
		if (sc->txcount == (TX_DESC_COUNT-1)) {
			ifp->if_drv_flags |= IFF_DRV_OACTIVE;
			break;
		}
		IFQ_DRV_DEQUEUE(&ifp->if_snd, m);
		if (m == NULL)
			break;
		if (ffec_setup_txbuf(sc, sc->tx_idx_head, &m) != 0) {
			IFQ_DRV_PREPEND(&ifp->if_snd, m);
			break;
		}
		BPF_MTAP(ifp, m);
		sc->tx_idx_head = next_txidx(sc, sc->tx_idx_head);
		++enqueued;
	}

	if (enqueued != 0) {
		bus_dmamap_sync(sc->txdesc_tag, sc->txdesc_map, BUS_DMASYNC_PREWRITE);
		WR4(sc, FEC_TDAR_REG, FEC_TDAR_TDAR);
		bus_dmamap_sync(sc->txdesc_tag, sc->txdesc_map, BUS_DMASYNC_POSTWRITE);
		sc->tx_watchdog_count = WATCHDOG_TIMEOUT_SECS;
	}
}

static void
ffec_txstart(struct ifnet *ifp)
{
	struct ffec_softc *sc = ifp->if_softc;

	FFEC_LOCK(sc);
	ffec_txstart_locked(sc);
	FFEC_UNLOCK(sc);
}

static void
ffec_txfinish_locked(struct ffec_softc *sc)
{
	struct ifnet *ifp;
	struct ffec_hwdesc *desc;
	struct ffec_bufmap *bmap;
	boolean_t retired_buffer;

	FFEC_ASSERT_LOCKED(sc);

	/* XXX Can't set PRE|POST right now, but we need both. */
	bus_dmamap_sync(sc->txdesc_tag, sc->txdesc_map, BUS_DMASYNC_PREREAD);
	bus_dmamap_sync(sc->txdesc_tag, sc->txdesc_map, BUS_DMASYNC_POSTREAD);
	ifp = sc->ifp;
	retired_buffer = false;
	while (sc->tx_idx_tail != sc->tx_idx_head) {
		desc = &sc->txdesc_ring[sc->tx_idx_tail];
		if (desc->flags_len & FEC_TXDESC_READY)
			break;
		retired_buffer = true;
		bmap = &sc->txbuf_map[sc->tx_idx_tail];
		bus_dmamap_sync(sc->txbuf_tag, bmap->map, 
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->txbuf_tag, bmap->map);
		m_freem(bmap->mbuf);
		bmap->mbuf = NULL;
		ffec_setup_txdesc(sc, sc->tx_idx_tail, 0, 0);
		sc->tx_idx_tail = next_txidx(sc, sc->tx_idx_tail);
	}

	/*
	 * If we retired any buffers, there will be open tx slots available in
	 * the descriptor ring, go try to start some new output.
	 */
	if (retired_buffer) {
		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
		ffec_txstart_locked(sc);
	}

	/* If there are no buffers outstanding, muzzle the watchdog. */
	if (sc->tx_idx_tail == sc->tx_idx_head) {
		sc->tx_watchdog_count = 0;
	}
}

inline static uint32_t
ffec_setup_rxdesc(struct ffec_softc *sc, int idx, bus_addr_t paddr)
{
	uint32_t nidx;

	/*
	 * The hardware requires 32-bit physical addresses.  We set up the dma
	 * tag to indicate that, so the cast to uint32_t should never lose
	 * significant bits.
	 */
	nidx = next_rxidx(sc, idx);
	sc->rxdesc_ring[idx].buf_paddr = (uint32_t)paddr;
	sc->rxdesc_ring[idx].flags_len = FEC_RXDESC_EMPTY | 
		((nidx == 0) ? FEC_RXDESC_WRAP : 0);

	return (nidx);
}

static int
ffec_setup_rxbuf(struct ffec_softc *sc, int idx, struct mbuf * m)
{
	int error, nsegs;
	struct bus_dma_segment seg;

	if (!(sc->fecflags & FECFLAG_RACC)) {
		/*
		 * The RACC[SHIFT16] feature is not available.  So, we need to
		 * leave at least ETHER_ALIGN bytes free at the beginning of the
		 * buffer to allow the data to be re-aligned after receiving it
		 * (by copying it backwards ETHER_ALIGN bytes in the same
		 * buffer).  We also have to ensure that the beginning of the
		 * buffer is aligned to the hardware's requirements.
		 */
		m_adj(m, roundup(ETHER_ALIGN, sc->rxbuf_align));
	}

	error = bus_dmamap_load_mbuf_sg(sc->rxbuf_tag, sc->rxbuf_map[idx].map,
	    m, &seg, &nsegs, 0);
	if (error != 0) {
		return (error);
	}

	bus_dmamap_sync(sc->rxbuf_tag, sc->rxbuf_map[idx].map, 
	    BUS_DMASYNC_PREREAD);

	sc->rxbuf_map[idx].mbuf = m;
	ffec_setup_rxdesc(sc, idx, seg.ds_addr);
	
	return (0);
}

static struct mbuf *
ffec_alloc_mbufcl(struct ffec_softc *sc)
{
	struct mbuf *m;

	m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
	if (m != NULL)
		m->m_pkthdr.len = m->m_len = m->m_ext.ext_size;

	return (m);
}

static void
ffec_rxfinish_onebuf(struct ffec_softc *sc, int len)
{
	struct mbuf *m, *newmbuf;
	struct ffec_bufmap *bmap;
	uint8_t *dst, *src;
	int error;

	/*
	 *  First try to get a new mbuf to plug into this slot in the rx ring.
	 *  If that fails, drop the current packet and recycle the current
	 *  mbuf, which is still mapped and loaded.
	 */
	if ((newmbuf = ffec_alloc_mbufcl(sc)) == NULL) {
		if_inc_counter(sc->ifp, IFCOUNTER_IQDROPS, 1);
		ffec_setup_rxdesc(sc, sc->rx_idx, 
		    sc->rxdesc_ring[sc->rx_idx].buf_paddr);
		return;
	}

	FFEC_UNLOCK(sc);

	bmap = &sc->rxbuf_map[sc->rx_idx];
	len -= ETHER_CRC_LEN;
	bus_dmamap_sync(sc->rxbuf_tag, bmap->map, BUS_DMASYNC_POSTREAD);
	bus_dmamap_unload(sc->rxbuf_tag, bmap->map);
	m = bmap->mbuf;
	bmap->mbuf = NULL;
	m->m_len = len;
	m->m_pkthdr.len = len;
	m->m_pkthdr.rcvif = sc->ifp;

	/*
	 * Align the protocol headers in the receive buffer on a 32-bit
	 * boundary.  Newer hardware does the alignment for us.  On hardware
	 * that doesn't support this feature, we have to copy-align the data.
	 *
	 *  XXX for older hardware, could we speed this up by copying just the
	 *  protocol headers into their own small mbuf then chaining the cluster
	 *  to it? That way we'd only need to copy like 64 bytes or whatever the
	 *  biggest header is, instead of the whole 1530ish-byte frame.
	 */
	if (sc->fecflags & FECFLAG_RACC) {
		m->m_data = mtod(m, uint8_t *) + 2;
	} else {
		src = mtod(m, uint8_t*);
		dst = src - ETHER_ALIGN;
		bcopy(src, dst, len);
		m->m_data = dst;
	}
	sc->ifp->if_input(sc->ifp, m);

	FFEC_LOCK(sc);

	if ((error = ffec_setup_rxbuf(sc, sc->rx_idx, newmbuf)) != 0) {
		device_printf(sc->dev, "ffec_setup_rxbuf error %d\n", error);
		/* XXX Now what?  We've got a hole in the rx ring. */
	}

}

static void
ffec_rxfinish_locked(struct ffec_softc *sc)
{
	struct ffec_hwdesc *desc;
	int len;
	boolean_t produced_empty_buffer;

	FFEC_ASSERT_LOCKED(sc);

	/* XXX Can't set PRE|POST right now, but we need both. */
	bus_dmamap_sync(sc->rxdesc_tag, sc->rxdesc_map, BUS_DMASYNC_PREREAD);
	bus_dmamap_sync(sc->rxdesc_tag, sc->rxdesc_map, BUS_DMASYNC_POSTREAD);
	produced_empty_buffer = false;
	for (;;) {
		desc = &sc->rxdesc_ring[sc->rx_idx];
		if (desc->flags_len & FEC_RXDESC_EMPTY)
			break;
		produced_empty_buffer = true;
		len = (desc->flags_len & FEC_RXDESC_LEN_MASK);
		if (len < 64) {
			/*
			 * Just recycle the descriptor and continue.           .
			 */
			ffec_setup_rxdesc(sc, sc->rx_idx,
			    sc->rxdesc_ring[sc->rx_idx].buf_paddr);
		} else if ((desc->flags_len & FEC_RXDESC_L) == 0) {
			/*
			 * The entire frame is not in this buffer.  Impossible.
			 * Recycle the descriptor and continue.
			 *
			 * XXX what's the right way to handle this? Probably we
			 * should stop/init the hardware because this should
			 * just really never happen when we have buffers bigger
			 * than the maximum frame size.
			 */
			device_printf(sc->dev, 
			    "fec_rxfinish: received frame without LAST bit set");
			ffec_setup_rxdesc(sc, sc->rx_idx, 
			    sc->rxdesc_ring[sc->rx_idx].buf_paddr);
		} else if (desc->flags_len & FEC_RXDESC_ERROR_BITS) {
			/*
			 *  Something went wrong with receiving the frame, we
			 *  don't care what (the hardware has counted the error
			 *  in the stats registers already), we just reuse the
			 *  same mbuf, which is still dma-mapped, by resetting
			 *  the rx descriptor.
			 */
			ffec_setup_rxdesc(sc, sc->rx_idx, 
			    sc->rxdesc_ring[sc->rx_idx].buf_paddr);
		} else {
			/*
			 *  Normal case: a good frame all in one buffer.
			 */
			ffec_rxfinish_onebuf(sc, len);
		}
		sc->rx_idx = next_rxidx(sc, sc->rx_idx);
	}

	if (produced_empty_buffer) {
		bus_dmamap_sync(sc->rxdesc_tag, sc->rxdesc_map, BUS_DMASYNC_PREWRITE);
		WR4(sc, FEC_RDAR_REG, FEC_RDAR_RDAR);
		bus_dmamap_sync(sc->rxdesc_tag, sc->rxdesc_map, BUS_DMASYNC_POSTWRITE);
	}
}

static void
ffec_get_hwaddr(struct ffec_softc *sc, uint8_t *hwaddr)
{
	uint32_t palr, paur, rnd;

	/*
	 * Try to recover a MAC address from the running hardware. If there's
	 * something non-zero there, assume the bootloader did the right thing
	 * and just use it.
	 *
	 * Otherwise, set the address to a convenient locally assigned address,
	 * 'bsd' + random 24 low-order bits.  'b' is 0x62, which has the locally
	 * assigned bit set, and the broadcast/multicast bit clear.
	 */
	palr = RD4(sc, FEC_PALR_REG);
	paur = RD4(sc, FEC_PAUR_REG) & FEC_PAUR_PADDR2_MASK;
	if ((palr | paur) != 0) {
		hwaddr[0] = palr >> 24;
		hwaddr[1] = palr >> 16;
		hwaddr[2] = palr >>  8;
		hwaddr[3] = palr >>  0;
		hwaddr[4] = paur >> 24;
		hwaddr[5] = paur >> 16;
	} else {
		rnd = arc4random() & 0x00ffffff;
		hwaddr[0] = 'b';
		hwaddr[1] = 's';
		hwaddr[2] = 'd';
		hwaddr[3] = rnd >> 16;
		hwaddr[4] = rnd >>  8;
		hwaddr[5] = rnd >>  0;
	}

	if (bootverbose) {
		device_printf(sc->dev,
		    "MAC address %02x:%02x:%02x:%02x:%02x:%02x:\n",
		    hwaddr[0], hwaddr[1], hwaddr[2], 
		    hwaddr[3], hwaddr[4], hwaddr[5]);
	}
}

static void
ffec_setup_rxfilter(struct ffec_softc *sc)
{
	struct ifnet *ifp;
	struct ifmultiaddr *ifma;
	uint8_t *eaddr;
	uint32_t crc;
	uint64_t ghash, ihash;

	FFEC_ASSERT_LOCKED(sc);

	ifp = sc->ifp;

	/*
	 * Set the multicast (group) filter hash.
	 */
	if ((ifp->if_flags & IFF_ALLMULTI))
		ghash = 0xffffffffffffffffLLU;
	else {
		ghash = 0;
		if_maddr_rlock(ifp);
		CK_STAILQ_FOREACH(ifma, &sc->ifp->if_multiaddrs, ifma_link) {
			if (ifma->ifma_addr->sa_family != AF_LINK)
				continue;
			/* 6 bits from MSB in LE CRC32 are used for hash. */
			crc = ether_crc32_le(LLADDR((struct sockaddr_dl *)
			    ifma->ifma_addr), ETHER_ADDR_LEN);
			ghash |= 1LLU << (((uint8_t *)&crc)[3] >> 2);
		}
		if_maddr_runlock(ifp);
	}
	WR4(sc, FEC_GAUR_REG, (uint32_t)(ghash >> 32));
	WR4(sc, FEC_GALR_REG, (uint32_t)ghash);

	/*
	 * Set the individual address filter hash.
	 *
	 * XXX Is 0 the right value when promiscuous is off?  This hw feature
	 * seems to support the concept of MAC address aliases, does such a
	 * thing even exist?
	 */
	if ((ifp->if_flags & IFF_PROMISC))
		ihash = 0xffffffffffffffffLLU;
	else {
		ihash = 0;
	}
	WR4(sc, FEC_IAUR_REG, (uint32_t)(ihash >> 32));
	WR4(sc, FEC_IALR_REG, (uint32_t)ihash);

	/*
	 * Set the primary address.
	 */
	eaddr = IF_LLADDR(ifp);
	WR4(sc, FEC_PALR_REG, (eaddr[0] << 24) | (eaddr[1] << 16) |
	    (eaddr[2] <<  8) | eaddr[3]);
	WR4(sc, FEC_PAUR_REG, (eaddr[4] << 24) | (eaddr[5] << 16));
}

static void
ffec_stop_locked(struct ffec_softc *sc)
{
	struct ifnet *ifp;
	struct ffec_hwdesc *desc;
	struct ffec_bufmap *bmap;
	int idx;

	FFEC_ASSERT_LOCKED(sc);

	ifp = sc->ifp;
	ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);
	sc->tx_watchdog_count = 0;

	/* 
	 * Stop the hardware, mask all interrupts, and clear all current
	 * interrupt status bits.
	 */
	WR4(sc, FEC_ECR_REG, RD4(sc, FEC_ECR_REG) & ~FEC_ECR_ETHEREN);
	WR4(sc, FEC_IEM_REG, 0x00000000);
	WR4(sc, FEC_IER_REG, 0xffffffff);

	/*
	 * Stop the media-check callout.  Do not use callout_drain() because
	 * we're holding a mutex the callout acquires, and if it's currently
	 * waiting to acquire it, we'd deadlock.  If it is waiting now, the
	 * ffec_tick() routine will return without doing anything when it sees
	 * that IFF_DRV_RUNNING is not set, so avoiding callout_drain() is safe.
	 */
	callout_stop(&sc->ffec_callout);

	/*
	 * Discard all untransmitted buffers.  Each buffer is simply freed;
	 * it's as if the bits were transmitted and then lost on the wire.
	 *
	 * XXX Is this right?  Or should we use IFQ_DRV_PREPEND() to put them
	 * back on the queue for when we get restarted later?
	 */
	idx = sc->tx_idx_tail;
	while (idx != sc->tx_idx_head) {
		desc = &sc->txdesc_ring[idx];
		bmap = &sc->txbuf_map[idx];
		if (desc->buf_paddr != 0) {
			bus_dmamap_unload(sc->txbuf_tag, bmap->map);
			m_freem(bmap->mbuf);
			bmap->mbuf = NULL;
			ffec_setup_txdesc(sc, idx, 0, 0);
		}
		idx = next_txidx(sc, idx);
	}

	/*
	 * Discard all unprocessed receive buffers.  This amounts to just
	 * pretending that nothing ever got received into them.  We reuse the
	 * mbuf already mapped for each desc, simply turning the EMPTY flags
	 * back on so they'll get reused when we start up again.
	 */
	for (idx = 0; idx < RX_DESC_COUNT; ++idx) {
		desc = &sc->rxdesc_ring[idx];
		ffec_setup_rxdesc(sc, idx, desc->buf_paddr);
	}
}

static void
ffec_init_locked(struct ffec_softc *sc)
{
	struct ifnet *ifp = sc->ifp;
	uint32_t maxbuf, maxfl, regval;

	FFEC_ASSERT_LOCKED(sc);

	/*
	 * The hardware has a limit of 0x7ff as the max frame length (see
	 * comments for MRBR below), and we use mbuf clusters as receive
	 * buffers, and we currently are designed to receive an entire frame
	 * into a single buffer.
	 *
	 * We start with a MCLBYTES-sized cluster, but we have to offset into
	 * the buffer by ETHER_ALIGN to make room for post-receive re-alignment,
	 * and then that value has to be rounded up to the hardware's DMA
	 * alignment requirements, so all in all our buffer is that much smaller
	 * than MCLBYTES.
	 *
	 * The resulting value is used as the frame truncation length and the
	 * max buffer receive buffer size for now.  It'll become more complex
	 * when we support jumbo frames and receiving fragments of them into
	 * separate buffers.
	 */
	maxbuf = MCLBYTES - roundup(ETHER_ALIGN, sc->rxbuf_align);
	maxfl = min(maxbuf, 0x7ff);

	if (ifp->if_drv_flags & IFF_DRV_RUNNING)
		return;

	/* Mask all interrupts and clear all current interrupt status bits. */
	WR4(sc, FEC_IEM_REG, 0x00000000);
	WR4(sc, FEC_IER_REG, 0xffffffff);

	/*
	 * Go set up palr/puar, galr/gaur, ialr/iaur.
	 */
	ffec_setup_rxfilter(sc);

	/*
	 * TFWR - Transmit FIFO watermark register.
	 *
	 * Set the transmit fifo watermark register to "store and forward" mode
	 * and also set a threshold of 128 bytes in the fifo before transmission
	 * of a frame begins (to avoid dma underruns).  Recent FEC hardware
	 * supports STRFWD and when that bit is set, the watermark level in the
	 * low bits is ignored.  Older hardware doesn't have STRFWD, but writing
	 * to that bit is innocuous, and the TWFR bits get used instead.
	 */
	WR4(sc, FEC_TFWR_REG, FEC_TFWR_STRFWD | FEC_TFWR_TWFR_128BYTE);

	/* RCR - Receive control register.
	 *
	 * Set max frame length + clean out anything left from u-boot.
	 */
	WR4(sc, FEC_RCR_REG, (maxfl << FEC_RCR_MAX_FL_SHIFT));

	/*
	 * TCR - Transmit control register.
	 *
	 * Clean out anything left from u-boot.  Any necessary values are set in
	 * ffec_miibus_statchg() based on the media type.
	 */
	WR4(sc, FEC_TCR_REG, 0);
        
	/*
	 * OPD - Opcode/pause duration.
	 *
	 * XXX These magic numbers come from u-boot.
	 */
	WR4(sc, FEC_OPD_REG, 0x00010020);

	/*
	 * FRSR - Fifo receive start register.
	 *
	 * This register does not exist on imx6, it is present on earlier
	 * hardware. The u-boot code sets this to a non-default value that's 32
	 * bytes larger than the default, with no clue as to why.  The default
	 * value should work fine, so there's no code to init it here.
	 */

	/*
	 *  MRBR - Max RX buffer size.
	 *
	 *  Note: For hardware prior to imx6 this value cannot exceed 0x07ff,
	 *  but the datasheet says no such thing for imx6.  On the imx6, setting
	 *  this to 2K without setting EN1588 resulted in a crazy runaway
	 *  receive loop in the hardware, where every rx descriptor in the ring
	 *  had its EMPTY flag cleared, no completion or error flags set, and a
	 *  length of zero.  I think maybe you can only exceed it when EN1588 is
	 *  set, like maybe that's what enables jumbo frames, because in general
	 *  the EN1588 flag seems to be the "enable new stuff" vs. "be legacy-
	 *  compatible" flag.
	 */
	WR4(sc, FEC_MRBR_REG, maxfl << FEC_MRBR_R_BUF_SIZE_SHIFT);

	/*
	 * FTRL - Frame truncation length.
	 *
	 * Must be greater than or equal to the value set in FEC_RCR_MAXFL.
	 */
	WR4(sc, FEC_FTRL_REG, maxfl);

	/*
	 * RDSR / TDSR descriptor ring pointers.
	 *
	 * When we turn on ECR_ETHEREN at the end, the hardware zeroes its
	 * internal current descriptor index values for both rings, so we zero
	 * our index values as well.
	 */
	sc->rx_idx = 0;
	sc->tx_idx_head = sc->tx_idx_tail = 0;
	sc->txcount = 0;
	WR4(sc, FEC_RDSR_REG, sc->rxdesc_ring_paddr);
	WR4(sc, FEC_TDSR_REG, sc->txdesc_ring_paddr);

	/*
	 * EIM - interrupt mask register.
	 *
	 * We always enable the same set of interrupts while running; unlike
	 * some drivers there's no need to change the mask on the fly depending
	 * on what operations are in progress.
	 */
	WR4(sc, FEC_IEM_REG, FEC_IER_TXF | FEC_IER_RXF | FEC_IER_EBERR);

	/*
	 * MIBC - MIB control (hardware stats); clear all statistics regs, then
	 * enable collection of statistics.
	 */
	regval = RD4(sc, FEC_MIBC_REG);
	WR4(sc, FEC_MIBC_REG, regval | FEC_MIBC_DIS);
	ffec_clear_stats(sc);
	WR4(sc, FEC_MIBC_REG, regval & ~FEC_MIBC_DIS);

	if (sc->fecflags & FECFLAG_RACC) {
		/*
		 * RACC - Receive Accelerator Function Configuration.
		 */
		regval = RD4(sc, FEC_RACC_REG);
		WR4(sc, FEC_RACC_REG, regval | FEC_RACC_SHIFT16);
	}

	/*
	 * ECR - Ethernet control register.
	 *
	 * This must happen after all the other config registers are set.  If
	 * we're running on little-endian hardware, also set the flag for byte-
	 * swapping descriptor ring entries.  This flag doesn't exist on older
	 * hardware, but it can be safely set -- the bit position it occupies
	 * was unused.
	 */
	regval = RD4(sc, FEC_ECR_REG);
#if _BYTE_ORDER == _LITTLE_ENDIAN
	regval |= FEC_ECR_DBSWP;
#endif
	regval |= FEC_ECR_ETHEREN;
	WR4(sc, FEC_ECR_REG, regval);

	ifp->if_drv_flags |= IFF_DRV_RUNNING;

       /*
	* Call mii_mediachg() which will call back into ffec_miibus_statchg() to
	* set up the remaining config registers based on the current media.
	*/
	mii_mediachg(sc->mii_softc);
	callout_reset(&sc->ffec_callout, hz, ffec_tick, sc);

	/*
	 * Tell the hardware that receive buffers are available.  They were made
	 * available in ffec_attach() or ffec_stop().
	 */
	WR4(sc, FEC_RDAR_REG, FEC_RDAR_RDAR);
}

static void
ffec_init(void *if_softc)
{
	struct ffec_softc *sc = if_softc;

	FFEC_LOCK(sc);
	ffec_init_locked(sc);
	FFEC_UNLOCK(sc);
}

static void
ffec_intr(void *arg)
{
	struct ffec_softc *sc;
	uint32_t ier;

	sc = arg;

	FFEC_LOCK(sc);

	ier = RD4(sc, FEC_IER_REG);

	if (ier & FEC_IER_TXF) {
		WR4(sc, FEC_IER_REG, FEC_IER_TXF);
		ffec_txfinish_locked(sc);
	}

	if (ier & FEC_IER_RXF) {
		WR4(sc, FEC_IER_REG, FEC_IER_RXF);
		ffec_rxfinish_locked(sc);
	}

	/*
	 * We actually don't care about most errors, because the hardware copes
	 * with them just fine, discarding the incoming bad frame, or forcing a
	 * bad CRC onto an outgoing bad frame, and counting the errors in the
	 * stats registers.  The one that really matters is EBERR (DMA bus
	 * error) because the hardware automatically clears ECR[ETHEREN] and we
	 * have to restart it here.  It should never happen.
	 */
	if (ier & FEC_IER_EBERR) {
		WR4(sc, FEC_IER_REG, FEC_IER_EBERR);
		device_printf(sc->dev, 
		    "Ethernet DMA error, restarting controller.\n");
		ffec_stop_locked(sc);
		ffec_init_locked(sc);
	}

	FFEC_UNLOCK(sc);

}

static int
ffec_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct ffec_softc *sc;
	struct mii_data *mii;
	struct ifreq *ifr;
	int mask, error;

	sc = ifp->if_softc;
	ifr = (struct ifreq *)data;

	error = 0;
	switch (cmd) {
	case SIOCSIFFLAGS:
		FFEC_LOCK(sc);
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
				if ((ifp->if_flags ^ sc->if_flags) &
				    (IFF_PROMISC | IFF_ALLMULTI))
					ffec_setup_rxfilter(sc);
			} else {
				if (!sc->is_detaching)
					ffec_init_locked(sc);
			}
		} else {
			if (ifp->if_drv_flags & IFF_DRV_RUNNING)
				ffec_stop_locked(sc);
		}
		sc->if_flags = ifp->if_flags;
		FFEC_UNLOCK(sc);
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
			FFEC_LOCK(sc);
			ffec_setup_rxfilter(sc);
			FFEC_UNLOCK(sc);
		}
		break;

	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		mii = sc->mii_softc;
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, cmd);
		break;

	case SIOCSIFCAP:
		mask = ifp->if_capenable ^ ifr->ifr_reqcap;
		if (mask & IFCAP_VLAN_MTU) {
			/* No work to do except acknowledge the change took. */
			ifp->if_capenable ^= IFCAP_VLAN_MTU;
		}
		break;

	default:
		error = ether_ioctl(ifp, cmd, data);
		break;
	}       

	return (error);
}

static int
ffec_detach(device_t dev)
{
	struct ffec_softc *sc;
	bus_dmamap_t map;
	int idx, irq;

	/*
	 * NB: This function can be called internally to unwind a failure to
	 * attach. Make sure a resource got allocated/created before destroying.
	 */

	sc = device_get_softc(dev);

	if (sc->is_attached) {
		FFEC_LOCK(sc);
		sc->is_detaching = true;
		ffec_stop_locked(sc);
		FFEC_UNLOCK(sc);
		callout_drain(&sc->ffec_callout);
		ether_ifdetach(sc->ifp);
	}

	/* XXX no miibus detach? */

	/* Clean up RX DMA resources and free mbufs. */
	for (idx = 0; idx < RX_DESC_COUNT; ++idx) {
		if ((map = sc->rxbuf_map[idx].map) != NULL) {
			bus_dmamap_unload(sc->rxbuf_tag, map);
			bus_dmamap_destroy(sc->rxbuf_tag, map);
			m_freem(sc->rxbuf_map[idx].mbuf);
		}
	}
	if (sc->rxbuf_tag != NULL)
		bus_dma_tag_destroy(sc->rxbuf_tag);
	if (sc->rxdesc_map != NULL) {
		bus_dmamap_unload(sc->rxdesc_tag, sc->rxdesc_map);
		bus_dmamap_destroy(sc->rxdesc_tag, sc->rxdesc_map);
	}
	if (sc->rxdesc_tag != NULL)
	bus_dma_tag_destroy(sc->rxdesc_tag);

	/* Clean up TX DMA resources. */
	for (idx = 0; idx < TX_DESC_COUNT; ++idx) {
		if ((map = sc->txbuf_map[idx].map) != NULL) {
			/* TX maps are already unloaded. */
			bus_dmamap_destroy(sc->txbuf_tag, map);
		}
	}
	if (sc->txbuf_tag != NULL)
		bus_dma_tag_destroy(sc->txbuf_tag);
	if (sc->txdesc_map != NULL) {
		bus_dmamap_unload(sc->txdesc_tag, sc->txdesc_map);
		bus_dmamap_destroy(sc->txdesc_tag, sc->txdesc_map);
	}
	if (sc->txdesc_tag != NULL)
		bus_dma_tag_destroy(sc->txdesc_tag);

	/* Release bus resources. */
	for (irq = 0; irq < MAX_IRQ_COUNT; ++irq) {
		if (sc->intr_cookie[irq] != NULL) {
			bus_teardown_intr(dev, sc->irq_res[irq],
			    sc->intr_cookie[irq]);
		}
	}
	bus_release_resources(dev, irq_res_spec, sc->irq_res);

	if (sc->mem_res != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->mem_res);

	FFEC_LOCK_DESTROY(sc);
	return (0);
}

static int
ffec_attach(device_t dev)
{
	struct ffec_softc *sc;
	struct ifnet *ifp = NULL;
	struct mbuf *m;
	void *dummy;
	uintptr_t typeflags;
	phandle_t ofw_node;
	uint32_t idx, mscr;
	int error, phynum, rid, irq;
	uint8_t eaddr[ETHER_ADDR_LEN];

	sc = device_get_softc(dev);
	sc->dev = dev;

	FFEC_LOCK_INIT(sc);

	/*
	 * There are differences in the implementation and features of the FEC
	 * hardware on different SoCs, so figure out what type we are.
	 */
	typeflags = ofw_bus_search_compatible(dev, compat_data)->ocd_data;
	sc->fectype = (uint8_t)(typeflags & FECTYPE_MASK);
	sc->fecflags = (uint32_t)(typeflags & ~FECTYPE_MASK);

	if (sc->fecflags & FECFLAG_AVB) {
		sc->rxbuf_align = 64;
		sc->txbuf_align = 1;
	} else {
		sc->rxbuf_align = 16;
		sc->txbuf_align = 16;
	}

	/*
	 * We have to be told what kind of electrical connection exists between
	 * the MAC and PHY or we can't operate correctly.
	 */
	if ((ofw_node = ofw_bus_get_node(dev)) == -1) {
		device_printf(dev, "Impossible: Can't find ofw bus node\n");
		error = ENXIO;
		goto out;
	}
	sc->phy_conn_type = mii_fdt_get_contype(ofw_node);
	if (sc->phy_conn_type == MII_CONTYPE_UNKNOWN) {
		device_printf(sc->dev, "No valid 'phy-mode' "
		    "property found in FDT data for device.\n");
		error = ENOATTR;
		goto out;
	}

	callout_init_mtx(&sc->ffec_callout, &sc->mtx, 0);

	/* Allocate bus resources for accessing the hardware. */
	rid = 0;
	sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, 
	    RF_ACTIVE);
	if (sc->mem_res == NULL) {
		device_printf(dev, "could not allocate memory resources.\n");
		error = ENOMEM;
		goto out;
	}

	error = bus_alloc_resources(dev, irq_res_spec, sc->irq_res);
	if (error != 0) {
		device_printf(dev, "could not allocate interrupt resources\n");
		goto out;
	}

	/*
	 * Set up TX descriptor ring, descriptors, and dma maps.
	 */
	error = bus_dma_tag_create(
	    bus_get_dma_tag(dev),	/* Parent tag. */
	    FEC_DESC_RING_ALIGN, 0,	/* alignment, boundary */
	    BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    TX_DESC_SIZE, 1, 		/* maxsize, nsegments */
	    TX_DESC_SIZE,		/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->txdesc_tag);
	if (error != 0) {
		device_printf(sc->dev,
		    "could not create TX ring DMA tag.\n");
		goto out;
	}

	error = bus_dmamem_alloc(sc->txdesc_tag, (void**)&sc->txdesc_ring,
	    BUS_DMA_COHERENT | BUS_DMA_WAITOK | BUS_DMA_ZERO, &sc->txdesc_map);
	if (error != 0) {
		device_printf(sc->dev,
		    "could not allocate TX descriptor ring.\n");
		goto out;
	}

	error = bus_dmamap_load(sc->txdesc_tag, sc->txdesc_map, sc->txdesc_ring,
	    TX_DESC_SIZE, ffec_get1paddr, &sc->txdesc_ring_paddr, 0);
	if (error != 0) {
		device_printf(sc->dev,
		    "could not load TX descriptor ring map.\n");
		goto out;
	}

	error = bus_dma_tag_create(
	    bus_get_dma_tag(dev),	/* Parent tag. */
	    sc->txbuf_align, 0,		/* alignment, boundary */
	    BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    MCLBYTES, 1, 		/* maxsize, nsegments */
	    MCLBYTES,			/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->txbuf_tag);
	if (error != 0) {
		device_printf(sc->dev,
		    "could not create TX ring DMA tag.\n");
		goto out;
	}

	for (idx = 0; idx < TX_DESC_COUNT; ++idx) {
		error = bus_dmamap_create(sc->txbuf_tag, 0, 
		    &sc->txbuf_map[idx].map);
		if (error != 0) {
			device_printf(sc->dev,
			    "could not create TX buffer DMA map.\n");
			goto out;
		}
		ffec_setup_txdesc(sc, idx, 0, 0);
	}

	/*
	 * Set up RX descriptor ring, descriptors, dma maps, and mbufs.
	 */
	error = bus_dma_tag_create(
	    bus_get_dma_tag(dev),	/* Parent tag. */
	    FEC_DESC_RING_ALIGN, 0,	/* alignment, boundary */
	    BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    RX_DESC_SIZE, 1, 		/* maxsize, nsegments */
	    RX_DESC_SIZE,		/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->rxdesc_tag);
	if (error != 0) {
		device_printf(sc->dev,
		    "could not create RX ring DMA tag.\n");
		goto out;
	}

	error = bus_dmamem_alloc(sc->rxdesc_tag, (void **)&sc->rxdesc_ring, 
	    BUS_DMA_COHERENT | BUS_DMA_WAITOK | BUS_DMA_ZERO, &sc->rxdesc_map);
	if (error != 0) {
		device_printf(sc->dev,
		    "could not allocate RX descriptor ring.\n");
		goto out;
	}

	error = bus_dmamap_load(sc->rxdesc_tag, sc->rxdesc_map, sc->rxdesc_ring,
	    RX_DESC_SIZE, ffec_get1paddr, &sc->rxdesc_ring_paddr, 0);
	if (error != 0) {
		device_printf(sc->dev,
		    "could not load RX descriptor ring map.\n");
		goto out;
	}

	error = bus_dma_tag_create(
	    bus_get_dma_tag(dev),	/* Parent tag. */
	    1, 0,			/* alignment, boundary */
	    BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    MCLBYTES, 1, 		/* maxsize, nsegments */
	    MCLBYTES,			/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->rxbuf_tag);
	if (error != 0) {
		device_printf(sc->dev,
		    "could not create RX buf DMA tag.\n");
		goto out;
	}

	for (idx = 0; idx < RX_DESC_COUNT; ++idx) {
		error = bus_dmamap_create(sc->rxbuf_tag, 0, 
		    &sc->rxbuf_map[idx].map);
		if (error != 0) {
			device_printf(sc->dev,
			    "could not create RX buffer DMA map.\n");
			goto out;
		}
		if ((m = ffec_alloc_mbufcl(sc)) == NULL) {
			device_printf(dev, "Could not alloc mbuf\n");
			error = ENOMEM;
			goto out;
		}
		if ((error = ffec_setup_rxbuf(sc, idx, m)) != 0) {
			device_printf(sc->dev,
			    "could not create new RX buffer.\n");
			goto out;
		}
	}

	/* Try to get the MAC address from the hardware before resetting it. */
	ffec_get_hwaddr(sc, eaddr);

	/*
	 * Reset the hardware.  Disables all interrupts.
	 *
	 * When the FEC is connected to the AXI bus (indicated by AVB flag), a
	 * MAC reset while a bus transaction is pending can hang the bus.
	 * Instead of resetting, turn off the ENABLE bit, which allows the
	 * hardware to complete any in-progress transfers (appending a bad CRC
	 * to any partial packet) and release the AXI bus.  This could probably
	 * be done unconditionally for all hardware variants, but that hasn't
	 * been tested.
	 */
	if (sc->fecflags & FECFLAG_AVB)
		WR4(sc, FEC_ECR_REG, 0);
	else
		WR4(sc, FEC_ECR_REG, FEC_ECR_RESET);

	/* Setup interrupt handler. */
	for (irq = 0; irq < MAX_IRQ_COUNT; ++irq) {
		if (sc->irq_res[irq] != NULL) {
			error = bus_setup_intr(dev, sc->irq_res[irq],
			    INTR_TYPE_NET | INTR_MPSAFE, NULL, ffec_intr, sc,
			    &sc->intr_cookie[irq]);
			if (error != 0) {
				device_printf(dev,
				    "could not setup interrupt handler.\n");
				goto out;
			}
		}
	}

	/*
	 * Set up the PHY control register.
	 *
	 * Speed formula for ENET is md_clock = mac_clock / ((N + 1) * 2).
	 * Speed formula for FEC is  md_clock = mac_clock / (N * 2)
	 *
	 * XXX - Revisit this...
	 *
	 * For a Wandboard imx6 (ENET) I was originally using 4, but the uboot
	 * code uses 10.  Both values seem to work, but I suspect many modern
	 * PHY parts can do mdio at speeds far above the standard 2.5 MHz.
	 *
	 * Different imx manuals use confusingly different terminology (things
	 * like "system clock" and "internal module clock") with examples that
	 * use frequencies that have nothing to do with ethernet, giving the
	 * vague impression that maybe the clock in question is the periphclock
	 * or something.  In fact, on an imx53 development board (FEC),
	 * measuring the mdio clock at the pin on the PHY and playing with
	 * various divisors showed that the root speed was 66 MHz (clk_ipg_root
	 * aka periphclock) and 13 was the right divisor.
	 *
	 * All in all, it seems likely that 13 is a safe divisor for now,
	 * because if we really do need to base it on the peripheral clock
	 * speed, then we need a platform-independant get-clock-freq API.
	 */
	mscr = 13 << FEC_MSCR_MII_SPEED_SHIFT;
	if (OF_hasprop(ofw_node, "phy-disable-preamble")) {
		mscr |= FEC_MSCR_DIS_PRE;
		if (bootverbose)
			device_printf(dev, "PHY preamble disabled\n");
	}
	WR4(sc, FEC_MSCR_REG, mscr);

	/* Set up the ethernet interface. */
	sc->ifp = ifp = if_alloc(IFT_ETHER);

	ifp->if_softc = sc;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_capabilities = IFCAP_VLAN_MTU;
	ifp->if_capenable = ifp->if_capabilities;
	ifp->if_start = ffec_txstart;
	ifp->if_ioctl = ffec_ioctl;
	ifp->if_init = ffec_init;
	IFQ_SET_MAXLEN(&ifp->if_snd, TX_DESC_COUNT - 1);
	ifp->if_snd.ifq_drv_maxlen = TX_DESC_COUNT - 1;
	IFQ_SET_READY(&ifp->if_snd);
	ifp->if_hdrlen = sizeof(struct ether_vlan_header);

#if 0 /* XXX The hardware keeps stats we could use for these. */
	ifp->if_linkmib = &sc->mibdata;
	ifp->if_linkmiblen = sizeof(sc->mibdata);
#endif

	/* Set up the miigasket hardware (if any). */
	ffec_miigasket_setup(sc);

	/* Attach the mii driver. */
	if (fdt_get_phyaddr(ofw_node, dev, &phynum, &dummy) != 0) {
		phynum = MII_PHY_ANY;
	}
	error = mii_attach(dev, &sc->miibus, ifp, ffec_media_change,
	    ffec_media_status, BMSR_DEFCAPMASK, phynum, MII_OFFSET_ANY,
	    (sc->fecflags & FECTYPE_MVF) ? MIIF_FORCEANEG : 0);
	if (error != 0) {
		device_printf(dev, "PHY attach failed\n");
		goto out;
	}
	sc->mii_softc = device_get_softc(sc->miibus);

	/* All ready to run, attach the ethernet interface. */
	ether_ifattach(ifp, eaddr);
	sc->is_attached = true;

	error = 0;
out:

	if (error != 0)
		ffec_detach(dev);

	return (error);
}

static int
ffec_probe(device_t dev)
{
	uintptr_t fectype;

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	fectype = ofw_bus_search_compatible(dev, compat_data)->ocd_data;
	if (fectype == FECTYPE_NONE)
		return (ENXIO);

	device_set_desc(dev, (fectype & FECFLAG_GBE) ?
	    "Freescale Gigabit Ethernet Controller" :
	    "Freescale Fast Ethernet Controller");

	return (BUS_PROBE_DEFAULT);
}


static device_method_t ffec_methods[] = {
	/* Device interface. */
	DEVMETHOD(device_probe,		ffec_probe),
	DEVMETHOD(device_attach,	ffec_attach),
	DEVMETHOD(device_detach,	ffec_detach),

/*
	DEVMETHOD(device_shutdown,	ffec_shutdown),
	DEVMETHOD(device_suspend,	ffec_suspend),
	DEVMETHOD(device_resume,	ffec_resume),
*/

	/* MII interface. */
	DEVMETHOD(miibus_readreg,	ffec_miibus_readreg),
	DEVMETHOD(miibus_writereg,	ffec_miibus_writereg),
	DEVMETHOD(miibus_statchg,	ffec_miibus_statchg),

	DEVMETHOD_END
};

static driver_t ffec_driver = {
	"ffec",
	ffec_methods,
	sizeof(struct ffec_softc)
};

static devclass_t ffec_devclass;

DRIVER_MODULE(ffec, simplebus, ffec_driver, ffec_devclass, 0, 0);
DRIVER_MODULE(miibus, ffec, miibus_driver, miibus_devclass, 0, 0);

MODULE_DEPEND(ffec, ether, 1, 1, 1);
MODULE_DEPEND(ffec, miibus, 1, 1, 1);
