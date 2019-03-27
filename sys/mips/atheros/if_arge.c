/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009, Oleksandr Tymoshenko
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * AR71XX gigabit ethernet driver
 */
#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_device_polling.h"
#endif

#include "opt_arge.h"

#include <sys/param.h>
#include <sys/endian.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/lock.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/socket.h>
#include <sys/taskqueue.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_media.h>
#include <net/ethernet.h>
#include <net/if_types.h>

#include <net/bpf.h>

#include <machine/bus.h>
#include <machine/cache.h>
#include <machine/resource.h>
#include <vm/vm_param.h>
#include <vm/vm.h>
#include <vm/pmap.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include "opt_arge.h"

#if defined(ARGE_MDIO)
#include <dev/mdio/mdio.h>
#include <dev/etherswitch/miiproxy.h>
#include "mdio_if.h"
#endif


MODULE_DEPEND(arge, ether, 1, 1, 1);
MODULE_DEPEND(arge, miibus, 1, 1, 1);
MODULE_VERSION(arge, 1);

#include "miibus_if.h"

#include <net/ethernet.h>

#include <mips/atheros/ar71xxreg.h>
#include <mips/atheros/ar934xreg.h>	/* XXX tsk! */
#include <mips/atheros/qca953xreg.h>	/* XXX tsk! */
#include <mips/atheros/qca955xreg.h>	/* XXX tsk! */
#include <mips/atheros/if_argevar.h>
#include <mips/atheros/ar71xx_setup.h>
#include <mips/atheros/ar71xx_cpudef.h>
#include <mips/atheros/ar71xx_macaddr.h>

typedef enum {
	ARGE_DBG_MII 	=	0x00000001,
	ARGE_DBG_INTR	=	0x00000002,
	ARGE_DBG_TX	=	0x00000004,
	ARGE_DBG_RX	=	0x00000008,
	ARGE_DBG_ERR	=	0x00000010,
	ARGE_DBG_RESET	=	0x00000020,
	ARGE_DBG_PLL	=	0x00000040,
	ARGE_DBG_ANY	=	0xffffffff,
} arge_debug_flags;

static const char * arge_miicfg_str[] = {
	"NONE",
	"GMII",
	"MII",
	"RGMII",
	"RMII",
	"SGMII"
};

#ifdef ARGE_DEBUG
#define	ARGEDEBUG(_sc, _m, ...) 					\
	do {								\
		if (((_m) & (_sc)->arge_debug) || ((_m) == ARGE_DBG_ANY)) \
			device_printf((_sc)->arge_dev, __VA_ARGS__);	\
	} while (0)
#else
#define	ARGEDEBUG(_sc, _m, ...)
#endif

static int arge_attach(device_t);
static int arge_detach(device_t);
static void arge_flush_ddr(struct arge_softc *);
static int arge_ifmedia_upd(struct ifnet *);
static void arge_ifmedia_sts(struct ifnet *, struct ifmediareq *);
static int arge_ioctl(struct ifnet *, u_long, caddr_t);
static void arge_init(void *);
static void arge_init_locked(struct arge_softc *);
static void arge_link_task(void *, int);
static void arge_update_link_locked(struct arge_softc *sc);
static void arge_set_pll(struct arge_softc *, int, int);
static int arge_miibus_readreg(device_t, int, int);
static void arge_miibus_statchg(device_t);
static int arge_miibus_writereg(device_t, int, int, int);
static int arge_probe(device_t);
static void arge_reset_dma(struct arge_softc *);
static int arge_resume(device_t);
static int arge_rx_ring_init(struct arge_softc *);
static void arge_rx_ring_free(struct arge_softc *sc);
static int arge_tx_ring_init(struct arge_softc *);
static void arge_tx_ring_free(struct arge_softc *);
#ifdef DEVICE_POLLING
static int arge_poll(struct ifnet *, enum poll_cmd, int);
#endif
static int arge_shutdown(device_t);
static void arge_start(struct ifnet *);
static void arge_start_locked(struct ifnet *);
static void arge_stop(struct arge_softc *);
static int arge_suspend(device_t);

static int arge_rx_locked(struct arge_softc *);
static void arge_tx_locked(struct arge_softc *);
static void arge_intr(void *);
static int arge_intr_filter(void *);
static void arge_tick(void *);

static void arge_hinted_child(device_t bus, const char *dname, int dunit);

/*
 * ifmedia callbacks for multiPHY MAC
 */
void arge_multiphy_mediastatus(struct ifnet *, struct ifmediareq *);
int arge_multiphy_mediachange(struct ifnet *);

static void arge_dmamap_cb(void *, bus_dma_segment_t *, int, int);
static int arge_dma_alloc(struct arge_softc *);
static void arge_dma_free(struct arge_softc *);
static int arge_newbuf(struct arge_softc *, int);
static __inline void arge_fixup_rx(struct mbuf *);

static device_method_t arge_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		arge_probe),
	DEVMETHOD(device_attach,	arge_attach),
	DEVMETHOD(device_detach,	arge_detach),
	DEVMETHOD(device_suspend,	arge_suspend),
	DEVMETHOD(device_resume,	arge_resume),
	DEVMETHOD(device_shutdown,	arge_shutdown),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	arge_miibus_readreg),
	DEVMETHOD(miibus_writereg,	arge_miibus_writereg),
	DEVMETHOD(miibus_statchg,	arge_miibus_statchg),

	/* bus interface */
	DEVMETHOD(bus_add_child,	device_add_child_ordered),
	DEVMETHOD(bus_hinted_child,	arge_hinted_child),

	DEVMETHOD_END
};

static driver_t arge_driver = {
	"arge",
	arge_methods,
	sizeof(struct arge_softc)
};

static devclass_t arge_devclass;

DRIVER_MODULE(arge, nexus, arge_driver, arge_devclass, 0, 0);
DRIVER_MODULE(miibus, arge, miibus_driver, miibus_devclass, 0, 0);

#if defined(ARGE_MDIO)
static int argemdio_probe(device_t);
static int argemdio_attach(device_t);
static int argemdio_detach(device_t);

/*
 * Declare an additional, separate driver for accessing the MDIO bus.
 */
static device_method_t argemdio_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		argemdio_probe),
	DEVMETHOD(device_attach,	argemdio_attach),
	DEVMETHOD(device_detach,	argemdio_detach),

	/* bus interface */
	DEVMETHOD(bus_add_child,	device_add_child_ordered),
	
	/* MDIO access */
	DEVMETHOD(mdio_readreg,		arge_miibus_readreg),
	DEVMETHOD(mdio_writereg,	arge_miibus_writereg),
};

DEFINE_CLASS_0(argemdio, argemdio_driver, argemdio_methods,
    sizeof(struct arge_softc));
static devclass_t argemdio_devclass;

DRIVER_MODULE(miiproxy, arge, miiproxy_driver, miiproxy_devclass, 0, 0);
DRIVER_MODULE(argemdio, nexus, argemdio_driver, argemdio_devclass, 0, 0);
DRIVER_MODULE(mdio, argemdio, mdio_driver, mdio_devclass, 0, 0);
#endif

static struct mtx miibus_mtx;

MTX_SYSINIT(miibus_mtx, &miibus_mtx, "arge mii lock", MTX_DEF);

/*
 * Flushes all
 *
 * XXX this needs to be done at interrupt time! Grr!
 */
static void
arge_flush_ddr(struct arge_softc *sc)
{
	switch (sc->arge_mac_unit) {
	case 0:
		ar71xx_device_flush_ddr(AR71XX_CPU_DDR_FLUSH_GE0);
		break;
	case 1:
		ar71xx_device_flush_ddr(AR71XX_CPU_DDR_FLUSH_GE1);
		break;
	default:
		device_printf(sc->arge_dev, "%s: unknown unit (%d)\n",
		    __func__,
		    sc->arge_mac_unit);
		break;
	}
}

static int
arge_probe(device_t dev)
{

	device_set_desc(dev, "Atheros AR71xx built-in ethernet interface");
	return (BUS_PROBE_NOWILDCARD);
}

#ifdef	ARGE_DEBUG
static void
arge_attach_intr_sysctl(device_t dev, struct sysctl_oid_list *parent)
{
	struct arge_softc *sc = device_get_softc(dev);
	struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(dev);
	struct sysctl_oid *tree = device_get_sysctl_tree(dev);
	struct sysctl_oid_list *child = SYSCTL_CHILDREN(tree);
	char sn[8];
	int i;

	tree = SYSCTL_ADD_NODE(ctx, parent, OID_AUTO, "intr",
	    CTLFLAG_RD, NULL, "Interrupt statistics");
	child = SYSCTL_CHILDREN(tree);
	for (i = 0; i < 32; i++) {
		snprintf(sn, sizeof(sn), "%d", i);
		SYSCTL_ADD_UINT(ctx, child, OID_AUTO, sn, CTLFLAG_RD,
		    &sc->intr_stats.count[i], 0, "");
	}
}
#endif

static void
arge_attach_sysctl(device_t dev)
{
	struct arge_softc *sc = device_get_softc(dev);
	struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(dev);
	struct sysctl_oid *tree = device_get_sysctl_tree(dev);

#ifdef	ARGE_DEBUG
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"debug", CTLFLAG_RW, &sc->arge_debug, 0,
		"arge interface debugging flags");
	arge_attach_intr_sysctl(dev, SYSCTL_CHILDREN(tree));
#endif

	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"tx_pkts_aligned", CTLFLAG_RW, &sc->stats.tx_pkts_aligned, 0,
		"number of TX aligned packets");

	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"tx_pkts_unaligned", CTLFLAG_RW, &sc->stats.tx_pkts_unaligned,
		0, "number of TX unaligned packets");

	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"tx_pkts_unaligned_start", CTLFLAG_RW, &sc->stats.tx_pkts_unaligned_start,
		0, "number of TX unaligned packets (start)");

	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"tx_pkts_unaligned_len", CTLFLAG_RW, &sc->stats.tx_pkts_unaligned_len,
		0, "number of TX unaligned packets (len)");

	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"tx_pkts_nosegs", CTLFLAG_RW, &sc->stats.tx_pkts_nosegs,
		0, "number of TX packets fail with no ring slots avail");

	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"intr_stray_filter", CTLFLAG_RW, &sc->stats.intr_stray,
		0, "number of stray interrupts (filter)");

	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"intr_stray_intr", CTLFLAG_RW, &sc->stats.intr_stray2,
		0, "number of stray interrupts (intr)");

	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"intr_ok", CTLFLAG_RW, &sc->stats.intr_ok,
		0, "number of OK interrupts");
#ifdef	ARGE_DEBUG
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, "tx_prod",
	    CTLFLAG_RW, &sc->arge_cdata.arge_tx_prod, 0, "");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, "tx_cons",
	    CTLFLAG_RW, &sc->arge_cdata.arge_tx_cons, 0, "");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, "tx_cnt",
	    CTLFLAG_RW, &sc->arge_cdata.arge_tx_cnt, 0, "");
#endif
}

static void
arge_reset_mac(struct arge_softc *sc)
{
	uint32_t reg;
	uint32_t reset_reg;

	ARGEDEBUG(sc, ARGE_DBG_RESET, "%s called\n", __func__);

	/* Step 1. Soft-reset MAC */
	ARGE_SET_BITS(sc, AR71XX_MAC_CFG1, MAC_CFG1_SOFT_RESET);
	DELAY(20);

	/* Step 2. Punt the MAC core from the central reset register */
	/*
	 * XXX TODO: migrate this (and other) chip specific stuff into
	 * a chipdef method.
	 */
	if (sc->arge_mac_unit == 0) {
		reset_reg = RST_RESET_GE0_MAC;
	} else {
		reset_reg = RST_RESET_GE1_MAC;
	}

	/*
	 * AR934x (and later) also needs the MDIO block reset.
	 * XXX should methodize this!
	 */
	if (ar71xx_soc == AR71XX_SOC_AR9341 ||
	   ar71xx_soc == AR71XX_SOC_AR9342 ||
	   ar71xx_soc == AR71XX_SOC_AR9344) {
		if (sc->arge_mac_unit == 0) {
			reset_reg |= AR934X_RESET_GE0_MDIO;
		} else {
			reset_reg |= AR934X_RESET_GE1_MDIO;
		}
	}

	if (ar71xx_soc == AR71XX_SOC_QCA9556 ||
	   ar71xx_soc == AR71XX_SOC_QCA9558) {
		if (sc->arge_mac_unit == 0) {
			reset_reg |= QCA955X_RESET_GE0_MDIO;
		} else {
			reset_reg |= QCA955X_RESET_GE1_MDIO;
		}
	}

	if (ar71xx_soc == AR71XX_SOC_QCA9533 ||
	   ar71xx_soc == AR71XX_SOC_QCA9533_V2) {
		if (sc->arge_mac_unit == 0) {
			reset_reg |= QCA953X_RESET_GE0_MDIO;
		} else {
			reset_reg |= QCA953X_RESET_GE1_MDIO;
		}
	}

	ar71xx_device_stop(reset_reg);
	DELAY(100);
	ar71xx_device_start(reset_reg);

	/* Step 3. Reconfigure MAC block */
	ARGE_WRITE(sc, AR71XX_MAC_CFG1,
		MAC_CFG1_SYNC_RX | MAC_CFG1_RX_ENABLE |
		MAC_CFG1_SYNC_TX | MAC_CFG1_TX_ENABLE);

	reg = ARGE_READ(sc, AR71XX_MAC_CFG2);
	reg |= MAC_CFG2_ENABLE_PADCRC | MAC_CFG2_LENGTH_FIELD ;
	ARGE_WRITE(sc, AR71XX_MAC_CFG2, reg);

	ARGE_WRITE(sc, AR71XX_MAC_MAX_FRAME_LEN, 1536);
}

/*
 * These values map to the divisor values programmed into
 * AR71XX_MAC_MII_CFG.
 *
 * The index of each value corresponds to the divisor section
 * value in AR71XX_MAC_MII_CFG (ie, table[0] means '0' in
 * AR71XX_MAC_MII_CFG, table[1] means '1', etc.)
 */
static const uint32_t ar71xx_mdio_div_table[] = {
	4, 4, 6, 8, 10, 14, 20, 28,
};

static const uint32_t ar7240_mdio_div_table[] = {
	2, 2, 4, 6, 8, 12, 18, 26, 32, 40, 48, 56, 62, 70, 78, 96,
};

static const uint32_t ar933x_mdio_div_table[] = {
	4, 4, 6, 8, 10, 14, 20, 28, 34, 42, 50, 58, 66, 74, 82, 98,
};

/*
 * Lookup the divisor to use based on the given frequency.
 *
 * Returns the divisor to use, or -ve on error.
 */
static int
arge_mdio_get_divider(struct arge_softc *sc, unsigned long mdio_clock)
{
	unsigned long ref_clock, t;
	const uint32_t *table;
	int ndivs;
	int i;

	/*
	 * This is the base MDIO frequency on the SoC.
	 * The dividers .. well, divide. Duh.
	 */
	ref_clock = ar71xx_mdio_freq();

	/*
	 * If either clock is undefined, just tell the
	 * caller to fall through to the defaults.
	 */
	if (ref_clock == 0 || mdio_clock == 0)
		return (-EINVAL);

	/*
	 * Pick the correct table!
	 */
	switch (ar71xx_soc) {
	case AR71XX_SOC_AR9330:
	case AR71XX_SOC_AR9331:
	case AR71XX_SOC_AR9341:
	case AR71XX_SOC_AR9342:
	case AR71XX_SOC_AR9344:
	case AR71XX_SOC_QCA9533:
	case AR71XX_SOC_QCA9533_V2:
	case AR71XX_SOC_QCA9556:
	case AR71XX_SOC_QCA9558:
		table = ar933x_mdio_div_table;
		ndivs = nitems(ar933x_mdio_div_table);
		break;

	case AR71XX_SOC_AR7240:
	case AR71XX_SOC_AR7241:
	case AR71XX_SOC_AR7242:
		table = ar7240_mdio_div_table;
		ndivs = nitems(ar7240_mdio_div_table);
		break;

	default:
		table = ar71xx_mdio_div_table;
		ndivs = nitems(ar71xx_mdio_div_table);
	}

	/*
	 * Now, walk through the list and find the first divisor
	 * that falls under the target MDIO frequency.
	 *
	 * The divisors go up, but the corresponding frequencies
	 * are actually decreasing.
	 */
	for (i = 0; i < ndivs; i++) {
		t = ref_clock / table[i];
		if (t <= mdio_clock) {
			return (i);
		}
	}

	ARGEDEBUG(sc, ARGE_DBG_RESET,
	    "No divider found; MDIO=%lu Hz; target=%lu Hz\n",
		ref_clock, mdio_clock);
	return (-ENOENT);
}

/*
 * Fetch the MDIO bus clock rate.
 *
 * For now, the default is DIV_28 for everything
 * bar AR934x, which will be DIV_58.
 *
 * It will definitely need updating to take into account
 * the MDIO bus core clock rate and the target clock
 * rate for the chip.
 */
static uint32_t
arge_fetch_mdiobus_clock_rate(struct arge_softc *sc)
{
	int mdio_freq, div;

	/*
	 * Is the MDIO frequency defined? If so, find a divisor that
	 * makes reasonable sense.  Don't overshoot the frequency.
	 */
	if (resource_int_value(device_get_name(sc->arge_dev),
	    device_get_unit(sc->arge_dev),
	    "mdio_freq",
	    &mdio_freq) == 0) {
		sc->arge_mdiofreq = mdio_freq;
		div = arge_mdio_get_divider(sc, sc->arge_mdiofreq);
		if (bootverbose)
			device_printf(sc->arge_dev,
			    "%s: mdio ref freq=%llu Hz, target freq=%llu Hz,"
			    " divisor index=%d\n",
			    __func__,
			    (unsigned long long) ar71xx_mdio_freq(),
			    (unsigned long long) mdio_freq,
			    div);
		if (div >= 0)
			return (div);
	}

	/*
	 * Default value(s).
	 *
	 * XXX obviously these need .. fixing.
	 *
	 * From Linux/OpenWRT:
	 *
	 * + 7240? DIV_6
	 * + Builtin-switch port and not 934x? DIV_10
	 * + Not built-in switch port and 934x? DIV_58
	 * + .. else DIV_28.
	 */
	switch (ar71xx_soc) {
	case AR71XX_SOC_AR9341:
	case AR71XX_SOC_AR9342:
	case AR71XX_SOC_AR9344:
	case AR71XX_SOC_QCA9533:
	case AR71XX_SOC_QCA9533_V2:
	case AR71XX_SOC_QCA9556:
	case AR71XX_SOC_QCA9558:
		return (MAC_MII_CFG_CLOCK_DIV_58);
		break;
	default:
		return (MAC_MII_CFG_CLOCK_DIV_28);
	}
}

static void
arge_reset_miibus(struct arge_softc *sc)
{
	uint32_t mdio_div;

	mdio_div = arge_fetch_mdiobus_clock_rate(sc);

	/*
	 * XXX AR934x and later; should we be also resetting the
	 * MDIO block(s) using the reset register block?
	 */

	/* Reset MII bus; program in the default divisor */
	ARGE_WRITE(sc, AR71XX_MAC_MII_CFG, MAC_MII_CFG_RESET | mdio_div);
	DELAY(100);
	ARGE_WRITE(sc, AR71XX_MAC_MII_CFG, mdio_div);
	DELAY(100);
}

static void
arge_fetch_pll_config(struct arge_softc *sc)
{
	long int val;

	if (resource_long_value(device_get_name(sc->arge_dev),
	    device_get_unit(sc->arge_dev),
	    "pll_10", &val) == 0) {
		sc->arge_pllcfg.pll_10 = val;
		device_printf(sc->arge_dev, "%s: pll_10 = 0x%x\n",
		    __func__, (int) val);
	}
	if (resource_long_value(device_get_name(sc->arge_dev),
	    device_get_unit(sc->arge_dev),
	    "pll_100", &val) == 0) {
		sc->arge_pllcfg.pll_100 = val;
		device_printf(sc->arge_dev, "%s: pll_100 = 0x%x\n",
		    __func__, (int) val);
	}
	if (resource_long_value(device_get_name(sc->arge_dev),
	    device_get_unit(sc->arge_dev),
	    "pll_1000", &val) == 0) {
		sc->arge_pllcfg.pll_1000 = val;
		device_printf(sc->arge_dev, "%s: pll_1000 = 0x%x\n",
		    __func__, (int) val);
	}
}

static int
arge_attach(device_t dev)
{
	struct ifnet		*ifp;
	struct arge_softc	*sc;
	int			error = 0, rid, i;
	uint32_t		hint;
	long			eeprom_mac_addr = 0;
	int			miicfg = 0;
	int			readascii = 0;
	int			local_mac = 0;
	uint8_t			local_macaddr[ETHER_ADDR_LEN];
	char *			local_macstr;
	char			devid_str[32];
	int			count;

	sc = device_get_softc(dev);
	sc->arge_dev = dev;
	sc->arge_mac_unit = device_get_unit(dev);

	/*
	 * See if there's a "board" MAC address hint available for
	 * this particular device.
	 *
	 * This is in the environment - it'd be nice to use the resource_*()
	 * routines, but at the moment the system is booting, the resource hints
	 * are set to the 'static' map so they're not pulling from kenv.
	 */
	snprintf(devid_str, 32, "hint.%s.%d.macaddr",
	    device_get_name(dev),
	    device_get_unit(dev));
	if ((local_macstr = kern_getenv(devid_str)) != NULL) {
		uint32_t tmpmac[ETHER_ADDR_LEN];

		/* Have a MAC address; should use it */
		device_printf(dev, "Overriding MAC address from environment: '%s'\n",
		    local_macstr);

		/* Extract out the MAC address */
		/* XXX this should all be a generic method */
		count = sscanf(local_macstr, "%x%*c%x%*c%x%*c%x%*c%x%*c%x",
		    &tmpmac[0], &tmpmac[1],
		    &tmpmac[2], &tmpmac[3],
		    &tmpmac[4], &tmpmac[5]);
		if (count == 6) {
			/* Valid! */
			local_mac = 1;
			for (i = 0; i < ETHER_ADDR_LEN; i++)
				local_macaddr[i] = tmpmac[i];
		}
		/* Done! */
		freeenv(local_macstr);
		local_macstr = NULL;
	}

	/*
	 * Hardware workarounds.
	 */
	switch (ar71xx_soc) {
	case AR71XX_SOC_AR9330:
	case AR71XX_SOC_AR9331:
	case AR71XX_SOC_AR9341:
	case AR71XX_SOC_AR9342:
	case AR71XX_SOC_AR9344:
	case AR71XX_SOC_QCA9533:
	case AR71XX_SOC_QCA9533_V2:
	case AR71XX_SOC_QCA9556:
	case AR71XX_SOC_QCA9558:
		/* Arbitrary alignment */
		sc->arge_hw_flags |= ARGE_HW_FLG_TX_DESC_ALIGN_1BYTE;
		sc->arge_hw_flags |= ARGE_HW_FLG_RX_DESC_ALIGN_1BYTE;
		break;
	default:
		sc->arge_hw_flags |= ARGE_HW_FLG_TX_DESC_ALIGN_4BYTE;
		sc->arge_hw_flags |= ARGE_HW_FLG_RX_DESC_ALIGN_4BYTE;
		break;
	}

	/*
	 * Some units (eg the TP-Link WR-1043ND) do not have a convenient
	 * EEPROM location to read the ethernet MAC address from.
	 * OpenWRT simply snaffles it from a fixed location.
	 *
	 * Since multiple units seem to use this feature, include
	 * a method of setting the MAC address based on an flash location
	 * in CPU address space.
	 *
	 * Some vendors have decided to store the mac address as a literal
	 * string of 18 characters in xx:xx:xx:xx:xx:xx format instead of
	 * an array of numbers.  Expose a hint to turn on this conversion
	 * feature via strtol()
	 */
	 if (local_mac == 0 && resource_long_value(device_get_name(dev),
	     device_get_unit(dev), "eeprommac", &eeprom_mac_addr) == 0) {
		local_mac = 1;
		int i;
		const char *mac =
		    (const char *) MIPS_PHYS_TO_KSEG1(eeprom_mac_addr);
		device_printf(dev, "Overriding MAC from EEPROM\n");
		if (resource_int_value(device_get_name(dev), device_get_unit(dev),
			"readascii", &readascii) == 0) {
			device_printf(dev, "Vendor stores MAC in ASCII format\n");
			for (i = 0; i < 6; i++) {
				local_macaddr[i] = strtol(&(mac[i*3]), NULL, 16);
			}
		} else {
			for (i = 0; i < 6; i++) {
				local_macaddr[i] = mac[i];
			}
		}
	}

	KASSERT(((sc->arge_mac_unit == 0) || (sc->arge_mac_unit == 1)),
	    ("if_arge: Only MAC0 and MAC1 supported"));

	/*
	 * Fetch the PLL configuration.
	 */
	arge_fetch_pll_config(sc);

	/*
	 * Get the MII configuration, if applicable.
	 */
	if (resource_int_value(device_get_name(dev), device_get_unit(dev),
	    "miimode", &miicfg) == 0) {
		/* XXX bounds check? */
		device_printf(dev, "%s: overriding MII mode to '%s'\n",
		    __func__, arge_miicfg_str[miicfg]);
		sc->arge_miicfg = miicfg;
	}

	/*
	 *  Get which PHY of 5 available we should use for this unit
	 */
	if (resource_int_value(device_get_name(dev), device_get_unit(dev), 
	    "phymask", &sc->arge_phymask) != 0) {
		/*
		 * Use port 4 (WAN) for GE0. For any other port use
		 * its PHY the same as its unit number
		 */
		if (sc->arge_mac_unit == 0)
			sc->arge_phymask = (1 << 4);
		else
			/* Use all phys up to 4 */
			sc->arge_phymask = (1 << 4) - 1;

		device_printf(dev, "No PHY specified, using mask %d\n", sc->arge_phymask);
	}

	/*
	 * Get default/hard-coded media & duplex mode.
	 */
	if (resource_int_value(device_get_name(dev), device_get_unit(dev),
	    "media", &hint) != 0)
		hint = 0;

	if (hint == 1000)
		sc->arge_media_type = IFM_1000_T;
	else if (hint == 100)
		sc->arge_media_type = IFM_100_TX;
	else if (hint == 10)
		sc->arge_media_type = IFM_10_T;
	else
		sc->arge_media_type = 0;

	if (resource_int_value(device_get_name(dev), device_get_unit(dev),
	    "fduplex", &hint) != 0)
		hint = 1;

	if (hint)
		sc->arge_duplex_mode = IFM_FDX;
	else
		sc->arge_duplex_mode = 0;

	mtx_init(&sc->arge_mtx, device_get_nameunit(dev), MTX_NETWORK_LOCK,
	    MTX_DEF);
	callout_init_mtx(&sc->arge_stat_callout, &sc->arge_mtx, 0);
	TASK_INIT(&sc->arge_link_task, 0, arge_link_task, sc);

	/* Map control/status registers. */
	sc->arge_rid = 0;
	sc->arge_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, 
	    &sc->arge_rid, RF_ACTIVE | RF_SHAREABLE);

	if (sc->arge_res == NULL) {
		device_printf(dev, "couldn't map memory\n");
		error = ENXIO;
		goto fail;
	}

	/* Allocate interrupts */
	rid = 0;
	sc->arge_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_SHAREABLE | RF_ACTIVE);

	if (sc->arge_irq == NULL) {
		device_printf(dev, "couldn't map interrupt\n");
		error = ENXIO;
		goto fail;
	}

	/* Allocate ifnet structure. */
	ifp = sc->arge_ifp = if_alloc(IFT_ETHER);

	if (ifp == NULL) {
		device_printf(dev, "couldn't allocate ifnet structure\n");
		error = ENOSPC;
		goto fail;
	}

	ifp->if_softc = sc;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = arge_ioctl;
	ifp->if_start = arge_start;
	ifp->if_init = arge_init;
	sc->arge_if_flags = ifp->if_flags;

	/* XXX: add real size */
	IFQ_SET_MAXLEN(&ifp->if_snd, ifqmaxlen);
	ifp->if_snd.ifq_maxlen = ifqmaxlen;
	IFQ_SET_READY(&ifp->if_snd);

	/* Tell the upper layer(s) we support long frames. */
	ifp->if_capabilities |= IFCAP_VLAN_MTU;

	ifp->if_capenable = ifp->if_capabilities;
#ifdef DEVICE_POLLING
	ifp->if_capabilities |= IFCAP_POLLING;
#endif

	/* If there's a local mac defined, copy that in */
	if (local_mac == 1) {
		(void) ar71xx_mac_addr_init(sc->arge_eaddr,
		    local_macaddr, 0, 0);
	} else {
		/*
		 * No MAC address configured. Generate the random one.
		 */
		if  (bootverbose)
			device_printf(dev,
			    "Generating random ethernet address.\n");
		(void) ar71xx_mac_addr_random_init(sc->arge_eaddr);
	}

	if (arge_dma_alloc(sc) != 0) {
		error = ENXIO;
		goto fail;
	}

	/*
	 * Don't do this for the MDIO bus case - it's already done
	 * as part of the MDIO bus attachment.
	 *
	 * XXX TODO: if we don't do this, we don't ever release the MAC
	 * from reset and we can't use the port.  Now, if we define ARGE_MDIO
	 * but we /don't/ define two MDIO busses, then we can't actually
	 * use both MACs.
	 */
#if !defined(ARGE_MDIO)
	/* Initialize the MAC block */
	arge_reset_mac(sc);
	arge_reset_miibus(sc);
#endif

	/* Configure MII mode, just for convienence */
	if (sc->arge_miicfg != 0)
		ar71xx_device_set_mii_if(sc->arge_mac_unit, sc->arge_miicfg);

	/*
	 * Set all Ethernet address registers to the same initial values
	 * set all four addresses to 66-88-aa-cc-dd-ee
	 */
	ARGE_WRITE(sc, AR71XX_MAC_STA_ADDR1, (sc->arge_eaddr[2] << 24)
	    | (sc->arge_eaddr[3] << 16) | (sc->arge_eaddr[4] << 8)
	    | sc->arge_eaddr[5]);
	ARGE_WRITE(sc, AR71XX_MAC_STA_ADDR2, (sc->arge_eaddr[0] << 8)
	    | sc->arge_eaddr[1]);

	ARGE_WRITE(sc, AR71XX_MAC_FIFO_CFG0,
	    FIFO_CFG0_ALL << FIFO_CFG0_ENABLE_SHIFT);

	/*
	 * SoC specific bits.
	 */
	switch (ar71xx_soc) {
		case AR71XX_SOC_AR7240:
		case AR71XX_SOC_AR7241:
		case AR71XX_SOC_AR7242:
		case AR71XX_SOC_AR9330:
		case AR71XX_SOC_AR9331:
		case AR71XX_SOC_AR9341:
		case AR71XX_SOC_AR9342:
		case AR71XX_SOC_AR9344:
		case AR71XX_SOC_QCA9533:
		case AR71XX_SOC_QCA9533_V2:
		case AR71XX_SOC_QCA9556:
		case AR71XX_SOC_QCA9558:
			ARGE_WRITE(sc, AR71XX_MAC_FIFO_CFG1, 0x0010ffff);
			ARGE_WRITE(sc, AR71XX_MAC_FIFO_CFG2, 0x015500aa);
			break;
		/* AR71xx, AR913x */
		default:
			ARGE_WRITE(sc, AR71XX_MAC_FIFO_CFG1, 0x0fff0000);
			ARGE_WRITE(sc, AR71XX_MAC_FIFO_CFG2, 0x00001fff);
	}

	ARGE_WRITE(sc, AR71XX_MAC_FIFO_RX_FILTMATCH,
	    FIFO_RX_FILTMATCH_DEFAULT);

	ARGE_WRITE(sc, AR71XX_MAC_FIFO_RX_FILTMASK,
	    FIFO_RX_FILTMASK_DEFAULT);

#if defined(ARGE_MDIO)
	sc->arge_miiproxy = mii_attach_proxy(sc->arge_dev);
#endif

	device_printf(sc->arge_dev, "finishing attachment, phymask %04x"
	    ", proxy %s \n", sc->arge_phymask, sc->arge_miiproxy == NULL ?
	    "null" : "set");
	for (i = 0; i < ARGE_NPHY; i++) {
		if (((1 << i) & sc->arge_phymask) != 0) {
			error = mii_attach(sc->arge_miiproxy != NULL ?
			    sc->arge_miiproxy : sc->arge_dev,
			    &sc->arge_miibus, sc->arge_ifp,
			    arge_ifmedia_upd, arge_ifmedia_sts,
			    BMSR_DEFCAPMASK, i, MII_OFFSET_ANY, 0);
			if (error != 0) {
				device_printf(sc->arge_dev, "unable to attach"
				    " PHY %d: %d\n", i, error);
				goto fail;
			}
		}
	}

	if (sc->arge_miibus == NULL) {
		/* no PHY, so use hard-coded values */
		ifmedia_init(&sc->arge_ifmedia, 0,
		    arge_multiphy_mediachange,
		    arge_multiphy_mediastatus);
		ifmedia_add(&sc->arge_ifmedia,
		    IFM_ETHER | sc->arge_media_type  | sc->arge_duplex_mode,
		    0, NULL);
		ifmedia_set(&sc->arge_ifmedia,
		    IFM_ETHER | sc->arge_media_type  | sc->arge_duplex_mode);
		arge_set_pll(sc, sc->arge_media_type, sc->arge_duplex_mode);
	}

	/* Call MI attach routine. */
	ether_ifattach(sc->arge_ifp, sc->arge_eaddr);

	/* Hook interrupt last to avoid having to lock softc */
	error = bus_setup_intr(sc->arge_dev, sc->arge_irq, INTR_TYPE_NET | INTR_MPSAFE,
	    arge_intr_filter, arge_intr, sc, &sc->arge_intrhand);

	if (error) {
		device_printf(sc->arge_dev, "couldn't set up irq\n");
		ether_ifdetach(sc->arge_ifp);
		goto fail;
	}

	/* setup sysctl variables */
	arge_attach_sysctl(sc->arge_dev);

fail:
	if (error) 
		arge_detach(dev);

	return (error);
}

static int
arge_detach(device_t dev)
{
	struct arge_softc	*sc = device_get_softc(dev);
	struct ifnet		*ifp = sc->arge_ifp;

	KASSERT(mtx_initialized(&sc->arge_mtx),
	    ("arge mutex not initialized"));

	/* These should only be active if attach succeeded */
	if (device_is_attached(dev)) {
		ARGE_LOCK(sc);
		sc->arge_detach = 1;
#ifdef DEVICE_POLLING
		if (ifp->if_capenable & IFCAP_POLLING)
			ether_poll_deregister(ifp);
#endif

		arge_stop(sc);
		ARGE_UNLOCK(sc);
		taskqueue_drain(taskqueue_swi, &sc->arge_link_task);
		ether_ifdetach(ifp);
	}

	if (sc->arge_miibus)
		device_delete_child(dev, sc->arge_miibus);

	if (sc->arge_miiproxy)
		device_delete_child(dev, sc->arge_miiproxy);

	bus_generic_detach(dev);

	if (sc->arge_intrhand)
		bus_teardown_intr(dev, sc->arge_irq, sc->arge_intrhand);

	if (sc->arge_res)
		bus_release_resource(dev, SYS_RES_MEMORY, sc->arge_rid,
		    sc->arge_res);

	if (ifp)
		if_free(ifp);

	arge_dma_free(sc);

	mtx_destroy(&sc->arge_mtx);

	return (0);

}

static int
arge_suspend(device_t dev)
{

	panic("%s", __func__);
	return 0;
}

static int
arge_resume(device_t dev)
{

	panic("%s", __func__);
	return 0;
}

static int
arge_shutdown(device_t dev)
{
	struct arge_softc	*sc;

	sc = device_get_softc(dev);

	ARGE_LOCK(sc);
	arge_stop(sc);
	ARGE_UNLOCK(sc);

	return (0);
}

static void
arge_hinted_child(device_t bus, const char *dname, int dunit)
{
	BUS_ADD_CHILD(bus, 0, dname, dunit);
	device_printf(bus, "hinted child %s%d\n", dname, dunit);
}

static int
arge_mdio_busy(struct arge_softc *sc)
{
	int i,result;

	for (i = 0; i < ARGE_MII_TIMEOUT; i++) {
		DELAY(5);
		ARGE_MDIO_BARRIER_READ(sc);
		result = ARGE_MDIO_READ(sc, AR71XX_MAC_MII_INDICATOR);
		if (! result)
			return (0);
		DELAY(5);
	}
	return (-1);
}

static int
arge_miibus_readreg(device_t dev, int phy, int reg)
{
	struct arge_softc * sc = device_get_softc(dev);
	int result;
	uint32_t addr = (phy << MAC_MII_PHY_ADDR_SHIFT)
	    | (reg & MAC_MII_REG_MASK);

	mtx_lock(&miibus_mtx);
	ARGE_MDIO_BARRIER_RW(sc);
	ARGE_MDIO_WRITE(sc, AR71XX_MAC_MII_CMD, MAC_MII_CMD_WRITE);
	ARGE_MDIO_BARRIER_WRITE(sc);
	ARGE_MDIO_WRITE(sc, AR71XX_MAC_MII_ADDR, addr);
	ARGE_MDIO_BARRIER_WRITE(sc);
	ARGE_MDIO_WRITE(sc, AR71XX_MAC_MII_CMD, MAC_MII_CMD_READ);

	if (arge_mdio_busy(sc) != 0) {
		mtx_unlock(&miibus_mtx);
		ARGEDEBUG(sc, ARGE_DBG_ANY, "%s timedout\n", __func__);
		/* XXX: return ERRNO istead? */
		return (-1);
	}

	ARGE_MDIO_BARRIER_READ(sc);
	result = ARGE_MDIO_READ(sc, AR71XX_MAC_MII_STATUS) & MAC_MII_STATUS_MASK;
	ARGE_MDIO_BARRIER_RW(sc);
	ARGE_MDIO_WRITE(sc, AR71XX_MAC_MII_CMD, MAC_MII_CMD_WRITE);
	mtx_unlock(&miibus_mtx);

	ARGEDEBUG(sc, ARGE_DBG_MII,
	    "%s: phy=%d, reg=%02x, value[%08x]=%04x\n",
	    __func__, phy, reg, addr, result);

	return (result);
}

static int
arge_miibus_writereg(device_t dev, int phy, int reg, int data)
{
	struct arge_softc * sc = device_get_softc(dev);
	uint32_t addr =
	    (phy << MAC_MII_PHY_ADDR_SHIFT) | (reg & MAC_MII_REG_MASK);

	ARGEDEBUG(sc, ARGE_DBG_MII, "%s: phy=%d, reg=%02x, value=%04x\n", __func__, 
	    phy, reg, data);

	mtx_lock(&miibus_mtx);
	ARGE_MDIO_BARRIER_RW(sc);
	ARGE_MDIO_WRITE(sc, AR71XX_MAC_MII_ADDR, addr);
	ARGE_MDIO_BARRIER_WRITE(sc);
	ARGE_MDIO_WRITE(sc, AR71XX_MAC_MII_CONTROL, data);
	ARGE_MDIO_BARRIER_WRITE(sc);

	if (arge_mdio_busy(sc) != 0) {
		mtx_unlock(&miibus_mtx);
		ARGEDEBUG(sc, ARGE_DBG_ANY, "%s timedout\n", __func__);
		/* XXX: return ERRNO istead? */
		return (-1);
	}

	mtx_unlock(&miibus_mtx);
	return (0);
}

static void
arge_miibus_statchg(device_t dev)
{
	struct arge_softc	*sc;

	sc = device_get_softc(dev);
	taskqueue_enqueue(taskqueue_swi, &sc->arge_link_task);
}

static void
arge_link_task(void *arg, int pending)
{
	struct arge_softc	*sc;
	sc = (struct arge_softc *)arg;

	ARGE_LOCK(sc);
	arge_update_link_locked(sc);
	ARGE_UNLOCK(sc);
}

static void
arge_update_link_locked(struct arge_softc *sc)
{
	struct mii_data		*mii;
	struct ifnet		*ifp;
	uint32_t		media, duplex;

	mii = device_get_softc(sc->arge_miibus);
	ifp = sc->arge_ifp;
	if (mii == NULL || ifp == NULL ||
	    (ifp->if_drv_flags & IFF_DRV_RUNNING) == 0) {
		return;
	}

	/*
	 * If we have a static media type configured, then
	 * use that.  Some PHY configurations (eg QCA955x -> AR8327)
	 * use a static speed/duplex between the SoC and switch,
	 * even though the front-facing PHY speed changes.
	 */
	if (sc->arge_media_type != 0) {
		ARGEDEBUG(sc, ARGE_DBG_MII, "%s: fixed; media=%d, duplex=%d\n",
		    __func__,
		    sc->arge_media_type,
		    sc->arge_duplex_mode);
		if (mii->mii_media_status & IFM_ACTIVE) {
			sc->arge_link_status = 1;
		} else {
			sc->arge_link_status = 0;
		}
		arge_set_pll(sc, sc->arge_media_type, sc->arge_duplex_mode);
	}

	if (mii->mii_media_status & IFM_ACTIVE) {

		media = IFM_SUBTYPE(mii->mii_media_active);
		if (media != IFM_NONE) {
			sc->arge_link_status = 1;
			duplex = mii->mii_media_active & IFM_GMASK;
			ARGEDEBUG(sc, ARGE_DBG_MII, "%s: media=%d, duplex=%d\n",
			    __func__,
			    media,
			    duplex);
			arge_set_pll(sc, media, duplex);
		}
	} else {
		sc->arge_link_status = 0;
	}
}

static void
arge_set_pll(struct arge_softc *sc, int media, int duplex)
{
	uint32_t		cfg, ifcontrol, rx_filtmask;
	uint32_t		fifo_tx, pll;
	int if_speed;

	/*
	 * XXX Verify - is this valid for all chips?
	 * QCA955x (and likely some of the earlier chips!) define
	 * this as nibble mode and byte mode, and those have to do
	 * with the interface type (MII/SMII versus GMII/RGMII.)
	 */
	ARGEDEBUG(sc, ARGE_DBG_PLL, "set_pll(%04x, %s)\n", media,
	    duplex == IFM_FDX ? "full" : "half");
	cfg = ARGE_READ(sc, AR71XX_MAC_CFG2);
	cfg &= ~(MAC_CFG2_IFACE_MODE_1000
	    | MAC_CFG2_IFACE_MODE_10_100
	    | MAC_CFG2_FULL_DUPLEX);

	if (duplex == IFM_FDX)
		cfg |= MAC_CFG2_FULL_DUPLEX;

	ifcontrol = ARGE_READ(sc, AR71XX_MAC_IFCONTROL);
	ifcontrol &= ~MAC_IFCONTROL_SPEED;
	rx_filtmask =
	    ARGE_READ(sc, AR71XX_MAC_FIFO_RX_FILTMASK);
	rx_filtmask &= ~FIFO_RX_MASK_BYTE_MODE;

	switch(media) {
	case IFM_10_T:
		cfg |= MAC_CFG2_IFACE_MODE_10_100;
		if_speed = 10;
		break;
	case IFM_100_TX:
		cfg |= MAC_CFG2_IFACE_MODE_10_100;
		ifcontrol |= MAC_IFCONTROL_SPEED;
		if_speed = 100;
		break;
	case IFM_1000_T:
	case IFM_1000_SX:
		cfg |= MAC_CFG2_IFACE_MODE_1000;
		rx_filtmask |= FIFO_RX_MASK_BYTE_MODE;
		if_speed = 1000;
		break;
	default:
		if_speed = 100;
		device_printf(sc->arge_dev,
		    "Unknown media %d\n", media);
	}

	ARGEDEBUG(sc, ARGE_DBG_PLL, "%s: if_speed=%d\n", __func__, if_speed);

	switch (ar71xx_soc) {
		case AR71XX_SOC_AR7240:
		case AR71XX_SOC_AR7241:
		case AR71XX_SOC_AR7242:
		case AR71XX_SOC_AR9330:
		case AR71XX_SOC_AR9331:
		case AR71XX_SOC_AR9341:
		case AR71XX_SOC_AR9342:
		case AR71XX_SOC_AR9344:
		case AR71XX_SOC_QCA9533:
		case AR71XX_SOC_QCA9533_V2:
		case AR71XX_SOC_QCA9556:
		case AR71XX_SOC_QCA9558:
			fifo_tx = 0x01f00140;
			break;
		case AR71XX_SOC_AR9130:
		case AR71XX_SOC_AR9132:
			fifo_tx = 0x00780fff;
			break;
		/* AR71xx */
		default:
			fifo_tx = 0x008001ff;
	}

	ARGE_WRITE(sc, AR71XX_MAC_CFG2, cfg);
	ARGE_WRITE(sc, AR71XX_MAC_IFCONTROL, ifcontrol);
	ARGE_WRITE(sc, AR71XX_MAC_FIFO_RX_FILTMASK,
	    rx_filtmask);
	ARGE_WRITE(sc, AR71XX_MAC_FIFO_TX_THRESHOLD, fifo_tx);

	/* fetch PLL registers */
	pll = ar71xx_device_get_eth_pll(sc->arge_mac_unit, if_speed);
	ARGEDEBUG(sc, ARGE_DBG_PLL, "%s: pll=0x%x\n", __func__, pll);

	/* Override if required by platform data */
	if (if_speed == 10 && sc->arge_pllcfg.pll_10 != 0)
		pll = sc->arge_pllcfg.pll_10;
	else if (if_speed == 100 && sc->arge_pllcfg.pll_100 != 0)
		pll = sc->arge_pllcfg.pll_100;
	else if (if_speed == 1000 && sc->arge_pllcfg.pll_1000 != 0)
		pll = sc->arge_pllcfg.pll_1000;
	ARGEDEBUG(sc, ARGE_DBG_PLL, "%s: final pll=0x%x\n", __func__, pll);

	/* XXX ensure pll != 0 */
	ar71xx_device_set_pll_ge(sc->arge_mac_unit, if_speed, pll);

	/* set MII registers */
	/*
	 * This was introduced to match what the Linux ag71xx ethernet
	 * driver does.  For the AR71xx case, it does set the port
	 * MII speed.  However, if this is done, non-gigabit speeds
	 * are not at all reliable when speaking via RGMII through
	 * 'bridge' PHY port that's pretending to be a local PHY.
	 *
	 * Until that gets root caused, and until an AR71xx + normal
	 * PHY board is tested, leave this disabled.
	 */
#if 0
	ar71xx_device_set_mii_speed(sc->arge_mac_unit, if_speed);
#endif
}


static void
arge_reset_dma(struct arge_softc *sc)
{

	ARGEDEBUG(sc, ARGE_DBG_RESET, "%s: called\n", __func__);

	ARGE_WRITE(sc, AR71XX_DMA_RX_CONTROL, 0);
	ARGE_WRITE(sc, AR71XX_DMA_TX_CONTROL, 0);

	ARGE_WRITE(sc, AR71XX_DMA_RX_DESC, 0);
	ARGE_WRITE(sc, AR71XX_DMA_TX_DESC, 0);

	/* Clear all possible RX interrupts */
	while(ARGE_READ(sc, AR71XX_DMA_RX_STATUS) & DMA_RX_STATUS_PKT_RECVD)
		ARGE_WRITE(sc, AR71XX_DMA_RX_STATUS, DMA_RX_STATUS_PKT_RECVD);

	/*
	 * Clear all possible TX interrupts
	 */
	while(ARGE_READ(sc, AR71XX_DMA_TX_STATUS) & DMA_TX_STATUS_PKT_SENT)
		ARGE_WRITE(sc, AR71XX_DMA_TX_STATUS, DMA_TX_STATUS_PKT_SENT);

	/*
	 * Now Rx/Tx errors
	 */
	ARGE_WRITE(sc, AR71XX_DMA_RX_STATUS,
	    DMA_RX_STATUS_BUS_ERROR | DMA_RX_STATUS_OVERFLOW);
	ARGE_WRITE(sc, AR71XX_DMA_TX_STATUS,
	    DMA_TX_STATUS_BUS_ERROR | DMA_TX_STATUS_UNDERRUN);

	/*
	 * Force a DDR flush so any pending data is properly
	 * flushed to RAM before underlying buffers are freed.
	 */
	arge_flush_ddr(sc);
}

static void
arge_init(void *xsc)
{
	struct arge_softc	 *sc = xsc;

	ARGE_LOCK(sc);
	arge_init_locked(sc);
	ARGE_UNLOCK(sc);
}

static void
arge_init_locked(struct arge_softc *sc)
{
	struct ifnet		*ifp = sc->arge_ifp;
	struct mii_data		*mii;

	ARGE_LOCK_ASSERT(sc);

	if ((ifp->if_flags & IFF_UP) && (ifp->if_drv_flags & IFF_DRV_RUNNING))
		return;

	/* Init circular RX list. */
	if (arge_rx_ring_init(sc) != 0) {
		device_printf(sc->arge_dev,
		    "initialization failed: no memory for rx buffers\n");
		arge_stop(sc);
		return;
	}

	/* Init tx descriptors. */
	arge_tx_ring_init(sc);

	arge_reset_dma(sc);

	if (sc->arge_miibus) {
		mii = device_get_softc(sc->arge_miibus);
		mii_mediachg(mii);
	}
	else {
		/*
		 * Sun always shines over multiPHY interface
		 */
		sc->arge_link_status = 1;
	}

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

	if (sc->arge_miibus) {
		callout_reset(&sc->arge_stat_callout, hz, arge_tick, sc);
		arge_update_link_locked(sc);
	}

	ARGE_WRITE(sc, AR71XX_DMA_TX_DESC, ARGE_TX_RING_ADDR(sc, 0));
	ARGE_WRITE(sc, AR71XX_DMA_RX_DESC, ARGE_RX_RING_ADDR(sc, 0));

	/* Start listening */
	ARGE_WRITE(sc, AR71XX_DMA_RX_CONTROL, DMA_RX_CONTROL_EN);

	/* Enable interrupts */
	ARGE_WRITE(sc, AR71XX_DMA_INTR, DMA_INTR_ALL);
}

/*
 * Return whether the mbuf chain is correctly aligned
 * for the arge TX engine.
 *
 * All the MACs have a length requirement: any non-final
 * fragment (ie, descriptor with MORE bit set) needs to have
 * a length divisible by 4.
 *
 * The AR71xx, AR913x require the start address also be
 * DWORD aligned.  The later MACs don't.
 */
static int
arge_mbuf_chain_is_tx_aligned(struct arge_softc *sc, struct mbuf *m0)
{
	struct mbuf *m;

	for (m = m0; m != NULL; m = m->m_next) {
		/*
		 * Only do this for chips that require it.
		 */
		if ((sc->arge_hw_flags & ARGE_HW_FLG_TX_DESC_ALIGN_4BYTE) &&
		    (mtod(m, intptr_t) & 3) != 0) {
			sc->stats.tx_pkts_unaligned_start++;
			return 0;
		}

		/*
		 * All chips have this requirement for length.
		 */
		if ((m->m_next != NULL) && ((m->m_len & 0x03) != 0)) {
			sc->stats.tx_pkts_unaligned_len++;
			return 0;
		}
	}
	return 1;
}

/*
 * Encapsulate an mbuf chain in a descriptor by coupling the mbuf data
 * pointers to the fragment pointers.
 */
static int
arge_encap(struct arge_softc *sc, struct mbuf **m_head)
{
	struct arge_txdesc	*txd;
	struct arge_desc	*desc, *prev_desc;
	bus_dma_segment_t	txsegs[ARGE_MAXFRAGS];
	int			error, i, nsegs, prod, prev_prod;
	struct mbuf		*m;

	ARGE_LOCK_ASSERT(sc);

	/*
	 * Fix mbuf chain based on hardware alignment constraints.
	 */
	m = *m_head;
	if (! arge_mbuf_chain_is_tx_aligned(sc, m)) {
		sc->stats.tx_pkts_unaligned++;
		m = m_defrag(*m_head, M_NOWAIT);
		if (m == NULL) {
			m_freem(*m_head);
			*m_head = NULL;
			return (ENOBUFS);
		}
		*m_head = m;
	} else
		sc->stats.tx_pkts_aligned++;

	prod = sc->arge_cdata.arge_tx_prod;
	txd = &sc->arge_cdata.arge_txdesc[prod];
	error = bus_dmamap_load_mbuf_sg(sc->arge_cdata.arge_tx_tag,
	    txd->tx_dmamap, *m_head, txsegs, &nsegs, BUS_DMA_NOWAIT);

	if (error == EFBIG) {
		panic("EFBIG");
	} else if (error != 0)
		return (error);

	if (nsegs == 0) {
		m_freem(*m_head);
		*m_head = NULL;
		return (EIO);
	}

	/* Check number of available descriptors. */
	if (sc->arge_cdata.arge_tx_cnt + nsegs >= (ARGE_TX_RING_COUNT - 2)) {
		bus_dmamap_unload(sc->arge_cdata.arge_tx_tag, txd->tx_dmamap);
		sc->stats.tx_pkts_nosegs++;
		return (ENOBUFS);
	}

	txd->tx_m = *m_head;
	bus_dmamap_sync(sc->arge_cdata.arge_tx_tag, txd->tx_dmamap,
	    BUS_DMASYNC_PREWRITE);

	/*
	 * Make a list of descriptors for this packet. DMA controller will
	 * walk through it while arge_link is not zero.
	 *
	 * Since we're in a endless circular buffer, ensure that
	 * the first descriptor in a multi-descriptor ring is always
	 * set to EMPTY, then un-do it when we're done populating.
	 */
	prev_prod = prod;
	desc = prev_desc = NULL;
	for (i = 0; i < nsegs; i++) {
		uint32_t tmp;

		desc = &sc->arge_rdata.arge_tx_ring[prod];

		/*
		 * Set DESC_EMPTY so the hardware (hopefully) stops at this
		 * point.  We don't want it to start transmitting descriptors
		 * before we've finished fleshing this out.
		 */
		tmp = ARGE_DMASIZE(txsegs[i].ds_len);
		if (i == 0)
			tmp |= ARGE_DESC_EMPTY;
		desc->packet_ctrl = tmp;

		/* XXX Note: only relevant for older MACs; but check length! */
		if ((sc->arge_hw_flags & ARGE_HW_FLG_TX_DESC_ALIGN_4BYTE) &&
		    (txsegs[i].ds_addr & 3))
			panic("TX packet address unaligned\n");

		desc->packet_addr = txsegs[i].ds_addr;

		/* link with previous descriptor */
		if (prev_desc)
			prev_desc->packet_ctrl |= ARGE_DESC_MORE;

		sc->arge_cdata.arge_tx_cnt++;
		prev_desc = desc;
		ARGE_INC(prod, ARGE_TX_RING_COUNT);
	}

	/* Update producer index. */
	sc->arge_cdata.arge_tx_prod = prod;

	/*
	 * The descriptors are updated, so enable the first one.
	 */
	desc = &sc->arge_rdata.arge_tx_ring[prev_prod];
	desc->packet_ctrl &= ~ ARGE_DESC_EMPTY;

	/* Sync descriptors. */
	bus_dmamap_sync(sc->arge_cdata.arge_tx_ring_tag,
	    sc->arge_cdata.arge_tx_ring_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	/* Flush writes */
	ARGE_BARRIER_WRITE(sc);

	/* Start transmitting */
	ARGEDEBUG(sc, ARGE_DBG_TX, "%s: setting DMA_TX_CONTROL_EN\n",
	    __func__);
	ARGE_WRITE(sc, AR71XX_DMA_TX_CONTROL, DMA_TX_CONTROL_EN);
	return (0);
}

static void
arge_start(struct ifnet *ifp)
{
	struct arge_softc	 *sc;

	sc = ifp->if_softc;

	ARGE_LOCK(sc);
	arge_start_locked(ifp);
	ARGE_UNLOCK(sc);
}

static void
arge_start_locked(struct ifnet *ifp)
{
	struct arge_softc	*sc;
	struct mbuf		*m_head;
	int			enq = 0;

	sc = ifp->if_softc;

	ARGE_LOCK_ASSERT(sc);

	ARGEDEBUG(sc, ARGE_DBG_TX, "%s: beginning\n", __func__);

	if ((ifp->if_drv_flags & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) !=
	    IFF_DRV_RUNNING || sc->arge_link_status == 0 )
		return;

	/*
	 * Before we go any further, check whether we're already full.
	 * The below check errors out immediately if the ring is full
	 * and never gets a chance to set this flag. Although it's
	 * likely never needed, this at least avoids an unexpected
	 * situation.
	 */
	if (sc->arge_cdata.arge_tx_cnt >= ARGE_TX_RING_COUNT - 2) {
		ifp->if_drv_flags |= IFF_DRV_OACTIVE;
		ARGEDEBUG(sc, ARGE_DBG_ERR,
		    "%s: tx_cnt %d >= max %d; setting IFF_DRV_OACTIVE\n",
		    __func__, sc->arge_cdata.arge_tx_cnt,
		    ARGE_TX_RING_COUNT - 2);
		return;
	}

	arge_flush_ddr(sc);

	for (enq = 0; !IFQ_DRV_IS_EMPTY(&ifp->if_snd) &&
	    sc->arge_cdata.arge_tx_cnt < ARGE_TX_RING_COUNT - 2; ) {
		IFQ_DRV_DEQUEUE(&ifp->if_snd, m_head);
		if (m_head == NULL)
			break;


		/*
		 * Pack the data into the transmit ring.
		 */
		if (arge_encap(sc, &m_head)) {
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
	ARGEDEBUG(sc, ARGE_DBG_TX, "%s: finished; queued %d packets\n",
	    __func__, enq);
}

static void
arge_stop(struct arge_softc *sc)
{
	struct ifnet	    *ifp;

	ARGE_LOCK_ASSERT(sc);

	ifp = sc->arge_ifp;
	ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);
	if (sc->arge_miibus)
		callout_stop(&sc->arge_stat_callout);

	/* mask out interrupts */
	ARGE_WRITE(sc, AR71XX_DMA_INTR, 0);

	arge_reset_dma(sc);

	/* Flush FIFO and free any existing mbufs */
	arge_flush_ddr(sc);
	arge_rx_ring_free(sc);
	arge_tx_ring_free(sc);
}


static int
arge_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct arge_softc		*sc = ifp->if_softc;
	struct ifreq		*ifr = (struct ifreq *) data;
	struct mii_data		*mii;
	int			error;
#ifdef DEVICE_POLLING
	int			mask;
#endif

	switch (command) {
	case SIOCSIFFLAGS:
		ARGE_LOCK(sc);
		if ((ifp->if_flags & IFF_UP) != 0) {
			if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0) {
				if (((ifp->if_flags ^ sc->arge_if_flags)
				    & (IFF_PROMISC | IFF_ALLMULTI)) != 0) {
					/* XXX: handle promisc & multi flags */
				}

			} else {
				if (!sc->arge_detach)
					arge_init_locked(sc);
			}
		} else if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0) {
			ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
			arge_stop(sc);
		}
		sc->arge_if_flags = ifp->if_flags;
		ARGE_UNLOCK(sc);
		error = 0;
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		/* XXX: implement SIOCDELMULTI */
		error = 0;
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		if (sc->arge_miibus) {
			mii = device_get_softc(sc->arge_miibus);
			error = ifmedia_ioctl(ifp, ifr, &mii->mii_media,
			    command);
		}
		else
			error = ifmedia_ioctl(ifp, ifr, &sc->arge_ifmedia,
			    command);
		break;
	case SIOCSIFCAP:
		/* XXX: Check other capabilities */
#ifdef DEVICE_POLLING
		mask = ifp->if_capenable ^ ifr->ifr_reqcap;
		if (mask & IFCAP_POLLING) {
			if (ifr->ifr_reqcap & IFCAP_POLLING) {
				ARGE_WRITE(sc, AR71XX_DMA_INTR, 0);
				error = ether_poll_register(arge_poll, ifp);
				if (error)
					return error;
				ARGE_LOCK(sc);
				ifp->if_capenable |= IFCAP_POLLING;
				ARGE_UNLOCK(sc);
			} else {
				ARGE_WRITE(sc, AR71XX_DMA_INTR, DMA_INTR_ALL);
				error = ether_poll_deregister(ifp);
				ARGE_LOCK(sc);
				ifp->if_capenable &= ~IFCAP_POLLING;
				ARGE_UNLOCK(sc);
			}
		}
		error = 0;
		break;
#endif
	default:
		error = ether_ioctl(ifp, command, data);
		break;
	}

	return (error);
}

/*
 * Set media options.
 */
static int
arge_ifmedia_upd(struct ifnet *ifp)
{
	struct arge_softc		*sc;
	struct mii_data		*mii;
	struct mii_softc	*miisc;
	int			error;

	sc = ifp->if_softc;
	ARGE_LOCK(sc);
	mii = device_get_softc(sc->arge_miibus);
	LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
		PHY_RESET(miisc);
	error = mii_mediachg(mii);
	ARGE_UNLOCK(sc);

	return (error);
}

/*
 * Report current media status.
 */
static void
arge_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct arge_softc		*sc = ifp->if_softc;
	struct mii_data		*mii;

	mii = device_get_softc(sc->arge_miibus);
	ARGE_LOCK(sc);
	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
	ARGE_UNLOCK(sc);
}

struct arge_dmamap_arg {
	bus_addr_t	arge_busaddr;
};

static void
arge_dmamap_cb(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	struct arge_dmamap_arg	*ctx;

	if (error != 0)
		return;
	ctx = arg;
	ctx->arge_busaddr = segs[0].ds_addr;
}

static int
arge_dma_alloc(struct arge_softc *sc)
{
	struct arge_dmamap_arg	ctx;
	struct arge_txdesc	*txd;
	struct arge_rxdesc	*rxd;
	int			error, i;
	int			arge_tx_align, arge_rx_align;

	/* Assume 4 byte alignment by default */
	arge_tx_align = 4;
	arge_rx_align = 4;

	if (sc->arge_hw_flags & ARGE_HW_FLG_TX_DESC_ALIGN_1BYTE)
		arge_tx_align = 1;
	if (sc->arge_hw_flags & ARGE_HW_FLG_RX_DESC_ALIGN_1BYTE)
		arge_rx_align = 1;

	/* Create parent DMA tag. */
	error = bus_dma_tag_create(
	    bus_get_dma_tag(sc->arge_dev),	/* parent */
	    1, 0,			/* alignment, boundary */
	    BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    BUS_SPACE_MAXSIZE_32BIT,	/* maxsize */
	    0,				/* nsegments */
	    BUS_SPACE_MAXSIZE_32BIT,	/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->arge_cdata.arge_parent_tag);
	if (error != 0) {
		device_printf(sc->arge_dev,
		    "failed to create parent DMA tag\n");
		goto fail;
	}
	/* Create tag for Tx ring. */
	error = bus_dma_tag_create(
	    sc->arge_cdata.arge_parent_tag,	/* parent */
	    ARGE_RING_ALIGN, 0,		/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    ARGE_TX_DMA_SIZE,		/* maxsize */
	    1,				/* nsegments */
	    ARGE_TX_DMA_SIZE,		/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->arge_cdata.arge_tx_ring_tag);
	if (error != 0) {
		device_printf(sc->arge_dev,
		    "failed to create Tx ring DMA tag\n");
		goto fail;
	}

	/* Create tag for Rx ring. */
	error = bus_dma_tag_create(
	    sc->arge_cdata.arge_parent_tag,	/* parent */
	    ARGE_RING_ALIGN, 0,		/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    ARGE_RX_DMA_SIZE,		/* maxsize */
	    1,				/* nsegments */
	    ARGE_RX_DMA_SIZE,		/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->arge_cdata.arge_rx_ring_tag);
	if (error != 0) {
		device_printf(sc->arge_dev,
		    "failed to create Rx ring DMA tag\n");
		goto fail;
	}

	/* Create tag for Tx buffers. */
	error = bus_dma_tag_create(
	    sc->arge_cdata.arge_parent_tag,	/* parent */
	    arge_tx_align, 0,		/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    MCLBYTES * ARGE_MAXFRAGS,	/* maxsize */
	    ARGE_MAXFRAGS,		/* nsegments */
	    MCLBYTES,			/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->arge_cdata.arge_tx_tag);
	if (error != 0) {
		device_printf(sc->arge_dev, "failed to create Tx DMA tag\n");
		goto fail;
	}

	/* Create tag for Rx buffers. */
	error = bus_dma_tag_create(
	    sc->arge_cdata.arge_parent_tag,	/* parent */
	    arge_rx_align, 0,		/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    MCLBYTES,			/* maxsize */
	    ARGE_MAXFRAGS,		/* nsegments */
	    MCLBYTES,			/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->arge_cdata.arge_rx_tag);
	if (error != 0) {
		device_printf(sc->arge_dev, "failed to create Rx DMA tag\n");
		goto fail;
	}

	/* Allocate DMA'able memory and load the DMA map for Tx ring. */
	error = bus_dmamem_alloc(sc->arge_cdata.arge_tx_ring_tag,
	    (void **)&sc->arge_rdata.arge_tx_ring, BUS_DMA_WAITOK |
	    BUS_DMA_COHERENT | BUS_DMA_ZERO,
	    &sc->arge_cdata.arge_tx_ring_map);
	if (error != 0) {
		device_printf(sc->arge_dev,
		    "failed to allocate DMA'able memory for Tx ring\n");
		goto fail;
	}

	ctx.arge_busaddr = 0;
	error = bus_dmamap_load(sc->arge_cdata.arge_tx_ring_tag,
	    sc->arge_cdata.arge_tx_ring_map, sc->arge_rdata.arge_tx_ring,
	    ARGE_TX_DMA_SIZE, arge_dmamap_cb, &ctx, 0);
	if (error != 0 || ctx.arge_busaddr == 0) {
		device_printf(sc->arge_dev,
		    "failed to load DMA'able memory for Tx ring\n");
		goto fail;
	}
	sc->arge_rdata.arge_tx_ring_paddr = ctx.arge_busaddr;

	/* Allocate DMA'able memory and load the DMA map for Rx ring. */
	error = bus_dmamem_alloc(sc->arge_cdata.arge_rx_ring_tag,
	    (void **)&sc->arge_rdata.arge_rx_ring, BUS_DMA_WAITOK |
	    BUS_DMA_COHERENT | BUS_DMA_ZERO,
	    &sc->arge_cdata.arge_rx_ring_map);
	if (error != 0) {
		device_printf(sc->arge_dev,
		    "failed to allocate DMA'able memory for Rx ring\n");
		goto fail;
	}

	ctx.arge_busaddr = 0;
	error = bus_dmamap_load(sc->arge_cdata.arge_rx_ring_tag,
	    sc->arge_cdata.arge_rx_ring_map, sc->arge_rdata.arge_rx_ring,
	    ARGE_RX_DMA_SIZE, arge_dmamap_cb, &ctx, 0);
	if (error != 0 || ctx.arge_busaddr == 0) {
		device_printf(sc->arge_dev,
		    "failed to load DMA'able memory for Rx ring\n");
		goto fail;
	}
	sc->arge_rdata.arge_rx_ring_paddr = ctx.arge_busaddr;

	/* Create DMA maps for Tx buffers. */
	for (i = 0; i < ARGE_TX_RING_COUNT; i++) {
		txd = &sc->arge_cdata.arge_txdesc[i];
		txd->tx_m = NULL;
		txd->tx_dmamap = NULL;
		error = bus_dmamap_create(sc->arge_cdata.arge_tx_tag, 0,
		    &txd->tx_dmamap);
		if (error != 0) {
			device_printf(sc->arge_dev,
			    "failed to create Tx dmamap\n");
			goto fail;
		}
	}
	/* Create DMA maps for Rx buffers. */
	if ((error = bus_dmamap_create(sc->arge_cdata.arge_rx_tag, 0,
	    &sc->arge_cdata.arge_rx_sparemap)) != 0) {
		device_printf(sc->arge_dev,
		    "failed to create spare Rx dmamap\n");
		goto fail;
	}
	for (i = 0; i < ARGE_RX_RING_COUNT; i++) {
		rxd = &sc->arge_cdata.arge_rxdesc[i];
		rxd->rx_m = NULL;
		rxd->rx_dmamap = NULL;
		error = bus_dmamap_create(sc->arge_cdata.arge_rx_tag, 0,
		    &rxd->rx_dmamap);
		if (error != 0) {
			device_printf(sc->arge_dev,
			    "failed to create Rx dmamap\n");
			goto fail;
		}
	}

fail:
	return (error);
}

static void
arge_dma_free(struct arge_softc *sc)
{
	struct arge_txdesc	*txd;
	struct arge_rxdesc	*rxd;
	int			i;

	/* Tx ring. */
	if (sc->arge_cdata.arge_tx_ring_tag) {
		if (sc->arge_rdata.arge_tx_ring_paddr)
			bus_dmamap_unload(sc->arge_cdata.arge_tx_ring_tag,
			    sc->arge_cdata.arge_tx_ring_map);
		if (sc->arge_rdata.arge_tx_ring)
			bus_dmamem_free(sc->arge_cdata.arge_tx_ring_tag,
			    sc->arge_rdata.arge_tx_ring,
			    sc->arge_cdata.arge_tx_ring_map);
		sc->arge_rdata.arge_tx_ring = NULL;
		sc->arge_rdata.arge_tx_ring_paddr = 0;
		bus_dma_tag_destroy(sc->arge_cdata.arge_tx_ring_tag);
		sc->arge_cdata.arge_tx_ring_tag = NULL;
	}
	/* Rx ring. */
	if (sc->arge_cdata.arge_rx_ring_tag) {
		if (sc->arge_rdata.arge_rx_ring_paddr)
			bus_dmamap_unload(sc->arge_cdata.arge_rx_ring_tag,
			    sc->arge_cdata.arge_rx_ring_map);
		if (sc->arge_rdata.arge_rx_ring)
			bus_dmamem_free(sc->arge_cdata.arge_rx_ring_tag,
			    sc->arge_rdata.arge_rx_ring,
			    sc->arge_cdata.arge_rx_ring_map);
		sc->arge_rdata.arge_rx_ring = NULL;
		sc->arge_rdata.arge_rx_ring_paddr = 0;
		bus_dma_tag_destroy(sc->arge_cdata.arge_rx_ring_tag);
		sc->arge_cdata.arge_rx_ring_tag = NULL;
	}
	/* Tx buffers. */
	if (sc->arge_cdata.arge_tx_tag) {
		for (i = 0; i < ARGE_TX_RING_COUNT; i++) {
			txd = &sc->arge_cdata.arge_txdesc[i];
			if (txd->tx_dmamap) {
				bus_dmamap_destroy(sc->arge_cdata.arge_tx_tag,
				    txd->tx_dmamap);
				txd->tx_dmamap = NULL;
			}
		}
		bus_dma_tag_destroy(sc->arge_cdata.arge_tx_tag);
		sc->arge_cdata.arge_tx_tag = NULL;
	}
	/* Rx buffers. */
	if (sc->arge_cdata.arge_rx_tag) {
		for (i = 0; i < ARGE_RX_RING_COUNT; i++) {
			rxd = &sc->arge_cdata.arge_rxdesc[i];
			if (rxd->rx_dmamap) {
				bus_dmamap_destroy(sc->arge_cdata.arge_rx_tag,
				    rxd->rx_dmamap);
				rxd->rx_dmamap = NULL;
			}
		}
		if (sc->arge_cdata.arge_rx_sparemap) {
			bus_dmamap_destroy(sc->arge_cdata.arge_rx_tag,
			    sc->arge_cdata.arge_rx_sparemap);
			sc->arge_cdata.arge_rx_sparemap = 0;
		}
		bus_dma_tag_destroy(sc->arge_cdata.arge_rx_tag);
		sc->arge_cdata.arge_rx_tag = NULL;
	}

	if (sc->arge_cdata.arge_parent_tag) {
		bus_dma_tag_destroy(sc->arge_cdata.arge_parent_tag);
		sc->arge_cdata.arge_parent_tag = NULL;
	}
}

/*
 * Initialize the transmit descriptors.
 */
static int
arge_tx_ring_init(struct arge_softc *sc)
{
	struct arge_ring_data	*rd;
	struct arge_txdesc	*txd;
	bus_addr_t		addr;
	int			i;

	sc->arge_cdata.arge_tx_prod = 0;
	sc->arge_cdata.arge_tx_cons = 0;
	sc->arge_cdata.arge_tx_cnt = 0;

	rd = &sc->arge_rdata;
	bzero(rd->arge_tx_ring, sizeof(*rd->arge_tx_ring));
	for (i = 0; i < ARGE_TX_RING_COUNT; i++) {
		if (i == ARGE_TX_RING_COUNT - 1)
			addr = ARGE_TX_RING_ADDR(sc, 0);
		else
			addr = ARGE_TX_RING_ADDR(sc, i + 1);
		rd->arge_tx_ring[i].packet_ctrl = ARGE_DESC_EMPTY;
		rd->arge_tx_ring[i].next_desc = addr;
		txd = &sc->arge_cdata.arge_txdesc[i];
		txd->tx_m = NULL;
	}

	bus_dmamap_sync(sc->arge_cdata.arge_tx_ring_tag,
	    sc->arge_cdata.arge_tx_ring_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	return (0);
}

/*
 * Free the Tx ring, unload any pending dma transaction and free the mbuf.
 */
static void
arge_tx_ring_free(struct arge_softc *sc)
{
	struct arge_txdesc	*txd;
	int			i;

	/* Free the Tx buffers. */
	for (i = 0; i < ARGE_TX_RING_COUNT; i++) {
		txd = &sc->arge_cdata.arge_txdesc[i];
		if (txd->tx_dmamap) {
			bus_dmamap_sync(sc->arge_cdata.arge_tx_tag,
			    txd->tx_dmamap, BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->arge_cdata.arge_tx_tag,
			    txd->tx_dmamap);
		}
		if (txd->tx_m)
			m_freem(txd->tx_m);
		txd->tx_m = NULL;
	}
}

/*
 * Initialize the RX descriptors and allocate mbufs for them. Note that
 * we arrange the descriptors in a closed ring, so that the last descriptor
 * points back to the first.
 */
static int
arge_rx_ring_init(struct arge_softc *sc)
{
	struct arge_ring_data	*rd;
	struct arge_rxdesc	*rxd;
	bus_addr_t		addr;
	int			i;

	sc->arge_cdata.arge_rx_cons = 0;

	rd = &sc->arge_rdata;
	bzero(rd->arge_rx_ring, sizeof(*rd->arge_rx_ring));
	for (i = 0; i < ARGE_RX_RING_COUNT; i++) {
		rxd = &sc->arge_cdata.arge_rxdesc[i];
		if (rxd->rx_m != NULL) {
			device_printf(sc->arge_dev,
			    "%s: ring[%d] rx_m wasn't free?\n",
			    __func__,
			    i);
		}
		rxd->rx_m = NULL;
		rxd->desc = &rd->arge_rx_ring[i];
		if (i == ARGE_RX_RING_COUNT - 1)
			addr = ARGE_RX_RING_ADDR(sc, 0);
		else
			addr = ARGE_RX_RING_ADDR(sc, i + 1);
		rd->arge_rx_ring[i].next_desc = addr;
		if (arge_newbuf(sc, i) != 0) {
			return (ENOBUFS);
		}
	}

	bus_dmamap_sync(sc->arge_cdata.arge_rx_ring_tag,
	    sc->arge_cdata.arge_rx_ring_map,
	    BUS_DMASYNC_PREWRITE);

	return (0);
}

/*
 * Free all the buffers in the RX ring.
 *
 * TODO: ensure that DMA is disabled and no pending DMA
 * is lurking in the FIFO.
 */
static void
arge_rx_ring_free(struct arge_softc *sc)
{
	int i;
	struct arge_rxdesc	*rxd;

	ARGE_LOCK_ASSERT(sc);

	for (i = 0; i < ARGE_RX_RING_COUNT; i++) {
		rxd = &sc->arge_cdata.arge_rxdesc[i];
		/* Unmap the mbuf */
		if (rxd->rx_m != NULL) {
			bus_dmamap_unload(sc->arge_cdata.arge_rx_tag,
			    rxd->rx_dmamap);
			m_free(rxd->rx_m);
			rxd->rx_m = NULL;
		}
	}
}

/*
 * Initialize an RX descriptor and attach an MBUF cluster.
 */
static int
arge_newbuf(struct arge_softc *sc, int idx)
{
	struct arge_desc		*desc;
	struct arge_rxdesc	*rxd;
	struct mbuf		*m;
	bus_dma_segment_t	segs[1];
	bus_dmamap_t		map;
	int			nsegs;

	/* XXX TODO: should just allocate an explicit 2KiB buffer */
	m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
	if (m == NULL)
		return (ENOBUFS);
	m->m_len = m->m_pkthdr.len = MCLBYTES;

	/*
	 * Add extra space to "adjust" (copy) the packet back to be aligned
	 * for purposes of IPv4/IPv6 header contents.
	 */
	if (sc->arge_hw_flags & ARGE_HW_FLG_RX_DESC_ALIGN_4BYTE)
		m_adj(m, sizeof(uint64_t));
	/*
	 * If it's a 1-byte aligned buffer, then just offset it two bytes
	 * and that will give us a hopefully correctly DWORD aligned
	 * L3 payload - and we won't have to undo it afterwards.
	 */
	else if (sc->arge_hw_flags & ARGE_HW_FLG_RX_DESC_ALIGN_1BYTE)
		m_adj(m, sizeof(uint16_t));

	if (bus_dmamap_load_mbuf_sg(sc->arge_cdata.arge_rx_tag,
	    sc->arge_cdata.arge_rx_sparemap, m, segs, &nsegs, 0) != 0) {
		m_freem(m);
		return (ENOBUFS);
	}
	KASSERT(nsegs == 1, ("%s: %d segments returned!", __func__, nsegs));

	rxd = &sc->arge_cdata.arge_rxdesc[idx];
	if (rxd->rx_m != NULL) {
		bus_dmamap_unload(sc->arge_cdata.arge_rx_tag, rxd->rx_dmamap);
	}
	map = rxd->rx_dmamap;
	rxd->rx_dmamap = sc->arge_cdata.arge_rx_sparemap;
	sc->arge_cdata.arge_rx_sparemap = map;
	rxd->rx_m = m;
	desc = rxd->desc;
	if ((sc->arge_hw_flags & ARGE_HW_FLG_RX_DESC_ALIGN_4BYTE) &&
	    segs[0].ds_addr & 3)
		panic("RX packet address unaligned");
	desc->packet_addr = segs[0].ds_addr;
	desc->packet_ctrl = ARGE_DESC_EMPTY | ARGE_DMASIZE(segs[0].ds_len);

	bus_dmamap_sync(sc->arge_cdata.arge_rx_ring_tag,
	    sc->arge_cdata.arge_rx_ring_map,
	    BUS_DMASYNC_PREWRITE);

	return (0);
}

/*
 * Move the data backwards 16 bits to (hopefully!) ensure the
 * IPv4/IPv6 payload is aligned.
 *
 * This is required for earlier hardware where the RX path
 * requires DWORD aligned buffers.
 */
static __inline void
arge_fixup_rx(struct mbuf *m)
{
	int		i;
	uint16_t	*src, *dst;

	src = mtod(m, uint16_t *);
	dst = src - 1;

	for (i = 0; i < m->m_len / sizeof(uint16_t); i++) {
		*dst++ = *src++;
	}

	if (m->m_len % sizeof(uint16_t))
		*(uint8_t *)dst = *(uint8_t *)src;

	m->m_data -= ETHER_ALIGN;
}

#ifdef DEVICE_POLLING
static int
arge_poll(struct ifnet *ifp, enum poll_cmd cmd, int count)
{
	struct arge_softc *sc = ifp->if_softc;
	int rx_npkts = 0;

	if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
		ARGE_LOCK(sc);
		arge_tx_locked(sc);
		rx_npkts = arge_rx_locked(sc);
		ARGE_UNLOCK(sc);
	}

	return (rx_npkts);
}
#endif /* DEVICE_POLLING */


static void
arge_tx_locked(struct arge_softc *sc)
{
	struct arge_txdesc	*txd;
	struct arge_desc	*cur_tx;
	struct ifnet		*ifp;
	uint32_t		ctrl;
	int			cons, prod;

	ARGE_LOCK_ASSERT(sc);

	cons = sc->arge_cdata.arge_tx_cons;
	prod = sc->arge_cdata.arge_tx_prod;

	ARGEDEBUG(sc, ARGE_DBG_TX, "%s: cons=%d, prod=%d\n", __func__, cons,
	    prod);

	if (cons == prod)
		return;

	bus_dmamap_sync(sc->arge_cdata.arge_tx_ring_tag,
	    sc->arge_cdata.arge_tx_ring_map,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	ifp = sc->arge_ifp;
	/*
	 * Go through our tx list and free mbufs for those
	 * frames that have been transmitted.
	 */
	for (; cons != prod; ARGE_INC(cons, ARGE_TX_RING_COUNT)) {
		cur_tx = &sc->arge_rdata.arge_tx_ring[cons];
		ctrl = cur_tx->packet_ctrl;
		/* Check if descriptor has "finished" flag */
		if ((ctrl & ARGE_DESC_EMPTY) == 0)
			break;

		ARGE_WRITE(sc, AR71XX_DMA_TX_STATUS, DMA_TX_STATUS_PKT_SENT);

		sc->arge_cdata.arge_tx_cnt--;
		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

		txd = &sc->arge_cdata.arge_txdesc[cons];

		if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);

		bus_dmamap_sync(sc->arge_cdata.arge_tx_tag, txd->tx_dmamap,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->arge_cdata.arge_tx_tag, txd->tx_dmamap);

		/* Free only if it's first descriptor in list */
		if (txd->tx_m)
			m_freem(txd->tx_m);
		txd->tx_m = NULL;

		/* reset descriptor */
		cur_tx->packet_addr = 0;
	}

	sc->arge_cdata.arge_tx_cons = cons;

	bus_dmamap_sync(sc->arge_cdata.arge_tx_ring_tag,
	    sc->arge_cdata.arge_tx_ring_map, BUS_DMASYNC_PREWRITE);
}


static int
arge_rx_locked(struct arge_softc *sc)
{
	struct arge_rxdesc	*rxd;
	struct ifnet		*ifp = sc->arge_ifp;
	int			cons, prog, packet_len, i;
	struct arge_desc	*cur_rx;
	struct mbuf		*m;
	int			rx_npkts = 0;

	ARGE_LOCK_ASSERT(sc);

	cons = sc->arge_cdata.arge_rx_cons;

	bus_dmamap_sync(sc->arge_cdata.arge_rx_ring_tag,
	    sc->arge_cdata.arge_rx_ring_map,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	for (prog = 0; prog < ARGE_RX_RING_COUNT;
	    ARGE_INC(cons, ARGE_RX_RING_COUNT)) {
		cur_rx = &sc->arge_rdata.arge_rx_ring[cons];
		rxd = &sc->arge_cdata.arge_rxdesc[cons];
		m = rxd->rx_m;

		if ((cur_rx->packet_ctrl & ARGE_DESC_EMPTY) != 0)
		       break;

		ARGE_WRITE(sc, AR71XX_DMA_RX_STATUS, DMA_RX_STATUS_PKT_RECVD);

		prog++;

		packet_len = ARGE_DMASIZE(cur_rx->packet_ctrl);
		bus_dmamap_sync(sc->arge_cdata.arge_rx_tag, rxd->rx_dmamap,
		    BUS_DMASYNC_POSTREAD);
		m = rxd->rx_m;

		/*
		 * If the MAC requires 4 byte alignment then the RX setup
		 * routine will have pre-offset things; so un-offset it here.
		 */
		if (sc->arge_hw_flags & ARGE_HW_FLG_RX_DESC_ALIGN_4BYTE)
			arge_fixup_rx(m);

		m->m_pkthdr.rcvif = ifp;
		/* Skip 4 bytes of CRC */
		m->m_pkthdr.len = m->m_len = packet_len - ETHER_CRC_LEN;
		if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
		rx_npkts++;

		ARGE_UNLOCK(sc);
		(*ifp->if_input)(ifp, m);
		ARGE_LOCK(sc);
		cur_rx->packet_addr = 0;
	}

	if (prog > 0) {

		i = sc->arge_cdata.arge_rx_cons;
		for (; prog > 0 ; prog--) {
			if (arge_newbuf(sc, i) != 0) {
				device_printf(sc->arge_dev,
				    "Failed to allocate buffer\n");
				break;
			}
			ARGE_INC(i, ARGE_RX_RING_COUNT);
		}

		bus_dmamap_sync(sc->arge_cdata.arge_rx_ring_tag,
		    sc->arge_cdata.arge_rx_ring_map,
		    BUS_DMASYNC_PREWRITE);

		sc->arge_cdata.arge_rx_cons = cons;
	}

	return (rx_npkts);
}

static int
arge_intr_filter(void *arg)
{
	struct arge_softc	*sc = arg;
	uint32_t		status, ints;

	status = ARGE_READ(sc, AR71XX_DMA_INTR_STATUS);
	ints = ARGE_READ(sc, AR71XX_DMA_INTR);

	ARGEDEBUG(sc, ARGE_DBG_INTR, "int mask(filter) = %b\n", ints,
	    "\20\10RX_BUS_ERROR\7RX_OVERFLOW\5RX_PKT_RCVD"
	    "\4TX_BUS_ERROR\2TX_UNDERRUN\1TX_PKT_SENT");
	ARGEDEBUG(sc, ARGE_DBG_INTR, "status(filter) = %b\n", status,
	    "\20\10RX_BUS_ERROR\7RX_OVERFLOW\5RX_PKT_RCVD"
	    "\4TX_BUS_ERROR\2TX_UNDERRUN\1TX_PKT_SENT");

	if (status & DMA_INTR_ALL) {
		sc->arge_intr_status |= status;
		ARGE_WRITE(sc, AR71XX_DMA_INTR, 0);
		sc->stats.intr_ok++;
		return (FILTER_SCHEDULE_THREAD);
	}

	sc->arge_intr_status = 0;
	sc->stats.intr_stray++;
	return (FILTER_STRAY);
}

static void
arge_intr(void *arg)
{
	struct arge_softc	*sc = arg;
	uint32_t		status;
	struct ifnet		*ifp = sc->arge_ifp;
#ifdef	ARGE_DEBUG
	int i;
#endif

	status = ARGE_READ(sc, AR71XX_DMA_INTR_STATUS);
	status |= sc->arge_intr_status;

	ARGEDEBUG(sc, ARGE_DBG_INTR, "int status(intr) = %b\n", status,
	    "\20\10\7RX_OVERFLOW\5RX_PKT_RCVD"
	    "\4TX_BUS_ERROR\2TX_UNDERRUN\1TX_PKT_SENT");

	/*
	 * Is it our interrupt at all?
	 */
	if (status == 0) {
		sc->stats.intr_stray2++;
		return;
	}

#ifdef	ARGE_DEBUG
	for (i = 0; i < 32; i++) {
		if (status & (1U << i)) {
			sc->intr_stats.count[i]++;
		}
	}
#endif

	if (status & DMA_INTR_RX_BUS_ERROR) {
		ARGE_WRITE(sc, AR71XX_DMA_RX_STATUS, DMA_RX_STATUS_BUS_ERROR);
		device_printf(sc->arge_dev, "RX bus error");
		return;
	}

	if (status & DMA_INTR_TX_BUS_ERROR) {
		ARGE_WRITE(sc, AR71XX_DMA_TX_STATUS, DMA_TX_STATUS_BUS_ERROR);
		device_printf(sc->arge_dev, "TX bus error");
		return;
	}

	ARGE_LOCK(sc);
	arge_flush_ddr(sc);

	if (status & DMA_INTR_RX_PKT_RCVD)
		arge_rx_locked(sc);

	/*
	 * RX overrun disables the receiver.
	 * Clear indication and re-enable rx.
	 */
	if ( status & DMA_INTR_RX_OVERFLOW) {
		ARGE_WRITE(sc, AR71XX_DMA_RX_STATUS, DMA_RX_STATUS_OVERFLOW);
		ARGE_WRITE(sc, AR71XX_DMA_RX_CONTROL, DMA_RX_CONTROL_EN);
		sc->stats.rx_overflow++;
	}

	if (status & DMA_INTR_TX_PKT_SENT)
		arge_tx_locked(sc);
	/*
	 * Underrun turns off TX. Clear underrun indication.
	 * If there's anything left in the ring, reactivate the tx.
	 */
	if (status & DMA_INTR_TX_UNDERRUN) {
		ARGE_WRITE(sc, AR71XX_DMA_TX_STATUS, DMA_TX_STATUS_UNDERRUN);
		sc->stats.tx_underflow++;
		ARGEDEBUG(sc, ARGE_DBG_TX, "%s: TX underrun; tx_cnt=%d\n",
		    __func__, sc->arge_cdata.arge_tx_cnt);
		if (sc->arge_cdata.arge_tx_cnt > 0 ) {
			ARGE_WRITE(sc, AR71XX_DMA_TX_CONTROL,
			    DMA_TX_CONTROL_EN);
		}
	}

	/*
	 * If we've finished TXing and there's space for more packets
	 * to be queued for TX, do so. Otherwise we may end up in a
	 * situation where the interface send queue was filled
	 * whilst the hardware queue was full, then the hardware
	 * queue was drained by the interface send queue wasn't,
	 * and thus if_start() is never called to kick-start
	 * the send process (and all subsequent packets are simply
	 * discarded.
	 *
	 * XXX TODO: make sure that the hardware deals nicely
	 * with the possibility of the queue being enabled above
	 * after a TX underrun, then having the hardware queue added
	 * to below.
	 */
	if (status & (DMA_INTR_TX_PKT_SENT | DMA_INTR_TX_UNDERRUN) &&
	    (ifp->if_drv_flags & IFF_DRV_OACTIVE) == 0) {
		if (!IFQ_IS_EMPTY(&ifp->if_snd))
			arge_start_locked(ifp);
	}

	/*
	 * We handled all bits, clear status
	 */
	sc->arge_intr_status = 0;
	ARGE_UNLOCK(sc);
	/*
	 * re-enable all interrupts
	 */
	ARGE_WRITE(sc, AR71XX_DMA_INTR, DMA_INTR_ALL);
}


static void
arge_tick(void *xsc)
{
	struct arge_softc	*sc = xsc;
	struct mii_data		*mii;

	ARGE_LOCK_ASSERT(sc);

	if (sc->arge_miibus) {
		mii = device_get_softc(sc->arge_miibus);
		mii_tick(mii);
		callout_reset(&sc->arge_stat_callout, hz, arge_tick, sc);
	}
}

int
arge_multiphy_mediachange(struct ifnet *ifp)
{
	struct arge_softc *sc = ifp->if_softc;
	struct ifmedia *ifm = &sc->arge_ifmedia;
	struct ifmedia_entry *ife = ifm->ifm_cur;

	if (IFM_TYPE(ifm->ifm_media) != IFM_ETHER)
		return (EINVAL);

	if (IFM_SUBTYPE(ife->ifm_media) == IFM_AUTO) {
		device_printf(sc->arge_dev,
		    "AUTO is not supported for multiphy MAC");
		return (EINVAL);
	}

	/*
	 * Ignore everything
	 */
	return (0);
}

void
arge_multiphy_mediastatus(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct arge_softc *sc = ifp->if_softc;

	ifmr->ifm_status = IFM_AVALID | IFM_ACTIVE;
	ifmr->ifm_active = IFM_ETHER | sc->arge_media_type |
	    sc->arge_duplex_mode;
}

#if defined(ARGE_MDIO)
static int
argemdio_probe(device_t dev)
{
	device_set_desc(dev, "Atheros AR71xx built-in ethernet interface, MDIO controller");
	return (0);
}

static int
argemdio_attach(device_t dev)
{
	struct arge_softc	*sc;
	int			error = 0;
#ifdef	ARGE_DEBUG
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree;
#endif
	sc = device_get_softc(dev);
	sc->arge_dev = dev;
	sc->arge_mac_unit = device_get_unit(dev);
	sc->arge_rid = 0;
	sc->arge_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, 
	    &sc->arge_rid, RF_ACTIVE | RF_SHAREABLE);
	if (sc->arge_res == NULL) {
		device_printf(dev, "couldn't map memory\n");
		error = ENXIO;
		goto fail;
	}

#ifdef	ARGE_DEBUG
	ctx = device_get_sysctl_ctx(dev);
	tree = device_get_sysctl_tree(dev);
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"debug", CTLFLAG_RW, &sc->arge_debug, 0,
		"argemdio interface debugging flags");
#endif

	/* Reset MAC - required for AR71xx MDIO to successfully occur */
	arge_reset_mac(sc);
	/* Reset MII bus */
	arge_reset_miibus(sc);

	bus_generic_probe(dev);
	bus_enumerate_hinted_children(dev);
	error = bus_generic_attach(dev);
fail:
	return (error);
}

static int
argemdio_detach(device_t dev)
{
	return (0);
}

#endif
