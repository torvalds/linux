/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 1995, David Greenman
 * Copyright (c) 2001 Jonathan Lemon <jlemon@freebsd.org>
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
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Intel EtherExpress Pro/100B PCI Fast Ethernet driver
 */

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_device_polling.h"
#endif

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/mbuf.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/rman.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>

#include <net/bpf.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/if_vlan_var.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>

#include <machine/bus.h>
#include <machine/in_cksum.h>
#include <machine/resource.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>		/* for PCIM_CMD_xxx */

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <dev/fxp/if_fxpreg.h>
#include <dev/fxp/if_fxpvar.h>
#include <dev/fxp/rcvbundl.h>

MODULE_DEPEND(fxp, pci, 1, 1, 1);
MODULE_DEPEND(fxp, ether, 1, 1, 1);
MODULE_DEPEND(fxp, miibus, 1, 1, 1);
#include "miibus_if.h"

/*
 * NOTE!  On !x86 we typically have an alignment constraint.  The
 * card DMAs the packet immediately following the RFA.  However,
 * the first thing in the packet is a 14-byte Ethernet header.
 * This means that the packet is misaligned.  To compensate,
 * we actually offset the RFA 2 bytes into the cluster.  This
 * alignes the packet after the Ethernet header at a 32-bit
 * boundary.  HOWEVER!  This means that the RFA is misaligned!
 */
#define	RFA_ALIGNMENT_FUDGE	2

/*
 * Set initial transmit threshold at 64 (512 bytes). This is
 * increased by 64 (512 bytes) at a time, to maximum of 192
 * (1536 bytes), if an underrun occurs.
 */
static int tx_threshold = 64;

/*
 * The configuration byte map has several undefined fields which
 * must be one or must be zero.  Set up a template for these bits.
 * The actual configuration is performed in fxp_init_body.
 *
 * See struct fxp_cb_config for the bit definitions.
 */
static const u_char fxp_cb_config_template[] = {
	0x0, 0x0,		/* cb_status */
	0x0, 0x0,		/* cb_command */
	0x0, 0x0, 0x0, 0x0,	/* link_addr */
	0x0,	/*  0 */
	0x0,	/*  1 */
	0x0,	/*  2 */
	0x0,	/*  3 */
	0x0,	/*  4 */
	0x0,	/*  5 */
	0x32,	/*  6 */
	0x0,	/*  7 */
	0x0,	/*  8 */
	0x0,	/*  9 */
	0x6,	/* 10 */
	0x0,	/* 11 */
	0x0,	/* 12 */
	0x0,	/* 13 */
	0xf2,	/* 14 */
	0x48,	/* 15 */
	0x0,	/* 16 */
	0x40,	/* 17 */
	0xf0,	/* 18 */
	0x0,	/* 19 */
	0x3f,	/* 20 */
	0x5,	/* 21 */
	0x0,	/* 22 */
	0x0,	/* 23 */
	0x0,	/* 24 */
	0x0,	/* 25 */
	0x0,	/* 26 */
	0x0,	/* 27 */
	0x0,	/* 28 */
	0x0,	/* 29 */
	0x0,	/* 30 */
	0x0	/* 31 */
};

/*
 * Claim various Intel PCI device identifiers for this driver.  The
 * sub-vendor and sub-device field are extensively used to identify
 * particular variants, but we don't currently differentiate between
 * them.
 */
static const struct fxp_ident fxp_ident_table[] = {
    { 0x8086, 0x1029,	-1,	0, "Intel 82559 PCI/CardBus Pro/100" },
    { 0x8086, 0x1030,	-1,	0, "Intel 82559 Pro/100 Ethernet" },
    { 0x8086, 0x1031,	-1,	3, "Intel 82801CAM (ICH3) Pro/100 VE Ethernet" },
    { 0x8086, 0x1032,	-1,	3, "Intel 82801CAM (ICH3) Pro/100 VE Ethernet" },
    { 0x8086, 0x1033,	-1,	3, "Intel 82801CAM (ICH3) Pro/100 VM Ethernet" },
    { 0x8086, 0x1034,	-1,	3, "Intel 82801CAM (ICH3) Pro/100 VM Ethernet" },
    { 0x8086, 0x1035,	-1,	3, "Intel 82801CAM (ICH3) Pro/100 Ethernet" },
    { 0x8086, 0x1036,	-1,	3, "Intel 82801CAM (ICH3) Pro/100 Ethernet" },
    { 0x8086, 0x1037,	-1,	3, "Intel 82801CAM (ICH3) Pro/100 Ethernet" },
    { 0x8086, 0x1038,	-1,	3, "Intel 82801CAM (ICH3) Pro/100 VM Ethernet" },
    { 0x8086, 0x1039,	-1,	4, "Intel 82801DB (ICH4) Pro/100 VE Ethernet" },
    { 0x8086, 0x103A,	-1,	4, "Intel 82801DB (ICH4) Pro/100 Ethernet" },
    { 0x8086, 0x103B,	-1,	4, "Intel 82801DB (ICH4) Pro/100 VM Ethernet" },
    { 0x8086, 0x103C,	-1,	4, "Intel 82801DB (ICH4) Pro/100 Ethernet" },
    { 0x8086, 0x103D,	-1,	4, "Intel 82801DB (ICH4) Pro/100 VE Ethernet" },
    { 0x8086, 0x103E,	-1,	4, "Intel 82801DB (ICH4) Pro/100 VM Ethernet" },
    { 0x8086, 0x1050,	-1,	5, "Intel 82801BA (D865) Pro/100 VE Ethernet" },
    { 0x8086, 0x1051,	-1,	5, "Intel 82562ET (ICH5/ICH5R) Pro/100 VE Ethernet" },
    { 0x8086, 0x1059,	-1,	0, "Intel 82551QM Pro/100 M Mobile Connection" },
    { 0x8086, 0x1064,	-1,	6, "Intel 82562EZ (ICH6)" },
    { 0x8086, 0x1065,	-1,	6, "Intel 82562ET/EZ/GT/GZ PRO/100 VE Ethernet" },
    { 0x8086, 0x1068,	-1,	6, "Intel 82801FBM (ICH6-M) Pro/100 VE Ethernet" },
    { 0x8086, 0x1069,	-1,	6, "Intel 82562EM/EX/GX Pro/100 Ethernet" },
    { 0x8086, 0x1091,	-1,	7, "Intel 82562GX Pro/100 Ethernet" },
    { 0x8086, 0x1092,	-1,	7, "Intel Pro/100 VE Network Connection" },
    { 0x8086, 0x1093,	-1,	7, "Intel Pro/100 VM Network Connection" },
    { 0x8086, 0x1094,	-1,	7, "Intel Pro/100 946GZ (ICH7) Network Connection" },
    { 0x8086, 0x1209,	-1,	0, "Intel 82559ER Embedded 10/100 Ethernet" },
    { 0x8086, 0x1229,	0x01,	0, "Intel 82557 Pro/100 Ethernet" },
    { 0x8086, 0x1229,	0x02,	0, "Intel 82557 Pro/100 Ethernet" },
    { 0x8086, 0x1229,	0x03,	0, "Intel 82557 Pro/100 Ethernet" },
    { 0x8086, 0x1229,	0x04,	0, "Intel 82558 Pro/100 Ethernet" },
    { 0x8086, 0x1229,	0x05,	0, "Intel 82558 Pro/100 Ethernet" },
    { 0x8086, 0x1229,	0x06,	0, "Intel 82559 Pro/100 Ethernet" },
    { 0x8086, 0x1229,	0x07,	0, "Intel 82559 Pro/100 Ethernet" },
    { 0x8086, 0x1229,	0x08,	0, "Intel 82559 Pro/100 Ethernet" },
    { 0x8086, 0x1229,	0x09,	0, "Intel 82559ER Pro/100 Ethernet" },
    { 0x8086, 0x1229,	0x0c,	0, "Intel 82550 Pro/100 Ethernet" },
    { 0x8086, 0x1229,	0x0d,	0, "Intel 82550C Pro/100 Ethernet" },
    { 0x8086, 0x1229,	0x0e,	0, "Intel 82550 Pro/100 Ethernet" },
    { 0x8086, 0x1229,	0x0f,	0, "Intel 82551 Pro/100 Ethernet" },
    { 0x8086, 0x1229,	0x10,	0, "Intel 82551 Pro/100 Ethernet" },
    { 0x8086, 0x1229,	-1,	0, "Intel 82557/8/9 Pro/100 Ethernet" },
    { 0x8086, 0x2449,	-1,	2, "Intel 82801BA/CAM (ICH2/3) Pro/100 Ethernet" },
    { 0x8086, 0x27dc,	-1,	7, "Intel 82801GB (ICH7) 10/100 Ethernet" },
    { 0,      0,	-1,	0, NULL },
};

#ifdef FXP_IP_CSUM_WAR
#define FXP_CSUM_FEATURES    (CSUM_IP | CSUM_TCP | CSUM_UDP)
#else
#define FXP_CSUM_FEATURES    (CSUM_TCP | CSUM_UDP)
#endif

static int		fxp_probe(device_t dev);
static int		fxp_attach(device_t dev);
static int		fxp_detach(device_t dev);
static int		fxp_shutdown(device_t dev);
static int		fxp_suspend(device_t dev);
static int		fxp_resume(device_t dev);

static const struct fxp_ident *fxp_find_ident(device_t dev);
static void		fxp_intr(void *xsc);
static void		fxp_rxcsum(struct fxp_softc *sc, if_t ifp,
			    struct mbuf *m, uint16_t status, int pos);
static int		fxp_intr_body(struct fxp_softc *sc, if_t ifp,
			    uint8_t statack, int count);
static void 		fxp_init(void *xsc);
static void 		fxp_init_body(struct fxp_softc *sc, int);
static void 		fxp_tick(void *xsc);
static void 		fxp_start(if_t ifp);
static void 		fxp_start_body(if_t ifp);
static int		fxp_encap(struct fxp_softc *sc, struct mbuf **m_head);
static void		fxp_txeof(struct fxp_softc *sc);
static void		fxp_stop(struct fxp_softc *sc);
static void 		fxp_release(struct fxp_softc *sc);
static int		fxp_ioctl(if_t ifp, u_long command,
			    caddr_t data);
static void 		fxp_watchdog(struct fxp_softc *sc);
static void		fxp_add_rfabuf(struct fxp_softc *sc,
			    struct fxp_rx *rxp);
static void		fxp_discard_rfabuf(struct fxp_softc *sc,
			    struct fxp_rx *rxp);
static int		fxp_new_rfabuf(struct fxp_softc *sc,
			    struct fxp_rx *rxp);
static int		fxp_mc_addrs(struct fxp_softc *sc);
static void		fxp_mc_setup(struct fxp_softc *sc);
static uint16_t		fxp_eeprom_getword(struct fxp_softc *sc, int offset,
			    int autosize);
static void 		fxp_eeprom_putword(struct fxp_softc *sc, int offset,
			    uint16_t data);
static void		fxp_autosize_eeprom(struct fxp_softc *sc);
static void		fxp_load_eeprom(struct fxp_softc *sc);
static void		fxp_read_eeprom(struct fxp_softc *sc, u_short *data,
			    int offset, int words);
static void		fxp_write_eeprom(struct fxp_softc *sc, u_short *data,
			    int offset, int words);
static int		fxp_ifmedia_upd(if_t ifp);
static void		fxp_ifmedia_sts(if_t ifp,
			    struct ifmediareq *ifmr);
static int		fxp_serial_ifmedia_upd(if_t ifp);
static void		fxp_serial_ifmedia_sts(if_t ifp,
			    struct ifmediareq *ifmr);
static int		fxp_miibus_readreg(device_t dev, int phy, int reg);
static int		fxp_miibus_writereg(device_t dev, int phy, int reg,
			    int value);
static void		fxp_miibus_statchg(device_t dev);
static void		fxp_load_ucode(struct fxp_softc *sc);
static void		fxp_update_stats(struct fxp_softc *sc);
static void		fxp_sysctl_node(struct fxp_softc *sc);
static int		sysctl_int_range(SYSCTL_HANDLER_ARGS,
			    int low, int high);
static int		sysctl_hw_fxp_bundle_max(SYSCTL_HANDLER_ARGS);
static int		sysctl_hw_fxp_int_delay(SYSCTL_HANDLER_ARGS);
static void 		fxp_scb_wait(struct fxp_softc *sc);
static void		fxp_scb_cmd(struct fxp_softc *sc, int cmd);
static void		fxp_dma_wait(struct fxp_softc *sc,
			    volatile uint16_t *status, bus_dma_tag_t dmat,
			    bus_dmamap_t map);

static device_method_t fxp_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		fxp_probe),
	DEVMETHOD(device_attach,	fxp_attach),
	DEVMETHOD(device_detach,	fxp_detach),
	DEVMETHOD(device_shutdown,	fxp_shutdown),
	DEVMETHOD(device_suspend,	fxp_suspend),
	DEVMETHOD(device_resume,	fxp_resume),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	fxp_miibus_readreg),
	DEVMETHOD(miibus_writereg,	fxp_miibus_writereg),
	DEVMETHOD(miibus_statchg,	fxp_miibus_statchg),

	DEVMETHOD_END
};

static driver_t fxp_driver = {
	"fxp",
	fxp_methods,
	sizeof(struct fxp_softc),
};

static devclass_t fxp_devclass;

DRIVER_MODULE_ORDERED(fxp, pci, fxp_driver, fxp_devclass, NULL, NULL,
    SI_ORDER_ANY);
MODULE_PNP_INFO("U16:vendor;U16:device", pci, fxp, fxp_ident_table,
    nitems(fxp_ident_table) - 1);
DRIVER_MODULE(miibus, fxp, miibus_driver, miibus_devclass, NULL, NULL);

static struct resource_spec fxp_res_spec_mem[] = {
	{ SYS_RES_MEMORY,	FXP_PCI_MMBA,	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,		RF_ACTIVE | RF_SHAREABLE },
	{ -1, 0 }
};

static struct resource_spec fxp_res_spec_io[] = {
	{ SYS_RES_IOPORT,	FXP_PCI_IOBA,	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,		RF_ACTIVE | RF_SHAREABLE },
	{ -1, 0 }
};

/*
 * Wait for the previous command to be accepted (but not necessarily
 * completed).
 */
static void
fxp_scb_wait(struct fxp_softc *sc)
{
	union {
		uint16_t w;
		uint8_t b[2];
	} flowctl;
	int i = 10000;

	while (CSR_READ_1(sc, FXP_CSR_SCB_COMMAND) && --i)
		DELAY(2);
	if (i == 0) {
		flowctl.b[0] = CSR_READ_1(sc, FXP_CSR_FC_THRESH);
		flowctl.b[1] = CSR_READ_1(sc, FXP_CSR_FC_STATUS);
		device_printf(sc->dev, "SCB timeout: 0x%x 0x%x 0x%x 0x%x\n",
		    CSR_READ_1(sc, FXP_CSR_SCB_COMMAND),
		    CSR_READ_1(sc, FXP_CSR_SCB_STATACK),
		    CSR_READ_1(sc, FXP_CSR_SCB_RUSCUS), flowctl.w);
	}
}

static void
fxp_scb_cmd(struct fxp_softc *sc, int cmd)
{

	if (cmd == FXP_SCB_COMMAND_CU_RESUME && sc->cu_resume_bug) {
		CSR_WRITE_1(sc, FXP_CSR_SCB_COMMAND, FXP_CB_COMMAND_NOP);
		fxp_scb_wait(sc);
	}
	CSR_WRITE_1(sc, FXP_CSR_SCB_COMMAND, cmd);
}

static void
fxp_dma_wait(struct fxp_softc *sc, volatile uint16_t *status,
    bus_dma_tag_t dmat, bus_dmamap_t map)
{
	int i;

	for (i = 10000; i > 0; i--) {
		DELAY(2);
		bus_dmamap_sync(dmat, map,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
		if ((le16toh(*status) & FXP_CB_STATUS_C) != 0)
			break;
	}
	if (i == 0)
		device_printf(sc->dev, "DMA timeout\n");
}

static const struct fxp_ident *
fxp_find_ident(device_t dev)
{
	uint16_t vendor;
	uint16_t device;
	uint8_t revid;
	const struct fxp_ident *ident;

	vendor = pci_get_vendor(dev);
	device = pci_get_device(dev);
	revid = pci_get_revid(dev);
	for (ident = fxp_ident_table; ident->name != NULL; ident++) {
		if (ident->vendor == vendor && ident->device == device &&
		    (ident->revid == revid || ident->revid == -1)) {
			return (ident);
		}
	}
	return (NULL);
}

/*
 * Return identification string if this device is ours.
 */
static int
fxp_probe(device_t dev)
{
	const struct fxp_ident *ident;

	ident = fxp_find_ident(dev);
	if (ident != NULL) {
		device_set_desc(dev, ident->name);
		return (BUS_PROBE_DEFAULT);
	}
	return (ENXIO);
}

static void
fxp_dma_map_addr(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	uint32_t *addr;

	if (error)
		return;

	KASSERT(nseg == 1, ("too many DMA segments, %d should be 1", nseg));
	addr = arg;
	*addr = segs->ds_addr;
}

static int
fxp_attach(device_t dev)
{
	struct fxp_softc *sc;
	struct fxp_cb_tx *tcbp;
	struct fxp_tx *txp;
	struct fxp_rx *rxp;
	if_t ifp;
	uint32_t val;
	uint16_t data;
	u_char eaddr[ETHER_ADDR_LEN];
	int error, flags, i, pmc, prefer_iomap;

	error = 0;
	sc = device_get_softc(dev);
	sc->dev = dev;
	mtx_init(&sc->sc_mtx, device_get_nameunit(dev), MTX_NETWORK_LOCK,
	    MTX_DEF);
	callout_init_mtx(&sc->stat_ch, &sc->sc_mtx, 0);
	ifmedia_init(&sc->sc_media, 0, fxp_serial_ifmedia_upd,
	    fxp_serial_ifmedia_sts);

	ifp = sc->ifp = if_gethandle(IFT_ETHER);
	if (ifp == (void *)NULL) {
		device_printf(dev, "can not if_alloc()\n");
		error = ENOSPC;
		goto fail;
	}

	/*
	 * Enable bus mastering.
	 */
	pci_enable_busmaster(dev);

	/*
	 * Figure out which we should try first - memory mapping or i/o mapping?
	 * We default to memory mapping. Then we accept an override from the
	 * command line. Then we check to see which one is enabled.
	 */
	prefer_iomap = 0;
	resource_int_value(device_get_name(dev), device_get_unit(dev),
	    "prefer_iomap", &prefer_iomap);
	if (prefer_iomap)
		sc->fxp_spec = fxp_res_spec_io;
	else
		sc->fxp_spec = fxp_res_spec_mem;

	error = bus_alloc_resources(dev, sc->fxp_spec, sc->fxp_res);
	if (error) {
		if (sc->fxp_spec == fxp_res_spec_mem)
			sc->fxp_spec = fxp_res_spec_io;
		else
			sc->fxp_spec = fxp_res_spec_mem;
		error = bus_alloc_resources(dev, sc->fxp_spec, sc->fxp_res);
	}
	if (error) {
		device_printf(dev, "could not allocate resources\n");
		error = ENXIO;
		goto fail;
	}

	if (bootverbose) {
		device_printf(dev, "using %s space register mapping\n",
		   sc->fxp_spec == fxp_res_spec_mem ? "memory" : "I/O");
	}

	/*
	 * Put CU/RU idle state and prepare full reset.
	 */
	CSR_WRITE_4(sc, FXP_CSR_PORT, FXP_PORT_SELECTIVE_RESET);
	DELAY(10);
	/* Full reset and disable interrupts. */
	CSR_WRITE_4(sc, FXP_CSR_PORT, FXP_PORT_SOFTWARE_RESET);
	DELAY(10);
	CSR_WRITE_1(sc, FXP_CSR_SCB_INTRCNTL, FXP_SCB_INTR_DISABLE);

	/*
	 * Find out how large of an SEEPROM we have.
	 */
	fxp_autosize_eeprom(sc);
	fxp_load_eeprom(sc);

	/*
	 * Find out the chip revision; lump all 82557 revs together.
	 */
	sc->ident = fxp_find_ident(dev);
	if (sc->ident->ich > 0) {
		/* Assume ICH controllers are 82559. */
		sc->revision = FXP_REV_82559_A0;
	} else {
		data = sc->eeprom[FXP_EEPROM_MAP_CNTR];
		if ((data >> 8) == 1)
			sc->revision = FXP_REV_82557;
		else
			sc->revision = pci_get_revid(dev);
	}

	/*
	 * Check availability of WOL. 82559ER does not support WOL.
	 */
	if (sc->revision >= FXP_REV_82558_A4 &&
	    sc->revision != FXP_REV_82559S_A) {
		data = sc->eeprom[FXP_EEPROM_MAP_ID];
		if ((data & 0x20) != 0 &&
		    pci_find_cap(sc->dev, PCIY_PMG, &pmc) == 0)
			sc->flags |= FXP_FLAG_WOLCAP;
	}

	if (sc->revision == FXP_REV_82550_C) {
		/*
		 * 82550C with server extension requires microcode to
		 * receive fragmented UDP datagrams.  However if the
		 * microcode is used for client-only featured 82550C
		 * it locks up controller.
		 */
		data = sc->eeprom[FXP_EEPROM_MAP_COMPAT];
		if ((data & 0x0400) == 0)
			sc->flags |= FXP_FLAG_NO_UCODE;
	}

	/* Receiver lock-up workaround detection. */
	if (sc->revision < FXP_REV_82558_A4) {
		data = sc->eeprom[FXP_EEPROM_MAP_COMPAT];
		if ((data & 0x03) != 0x03) {
			sc->flags |= FXP_FLAG_RXBUG;
			device_printf(dev, "Enabling Rx lock-up workaround\n");
		}
	}

	/*
	 * Determine whether we must use the 503 serial interface.
	 */
	data = sc->eeprom[FXP_EEPROM_MAP_PRI_PHY];
	if (sc->revision == FXP_REV_82557 && (data & FXP_PHY_DEVICE_MASK) != 0
	    && (data & FXP_PHY_SERIAL_ONLY))
		sc->flags |= FXP_FLAG_SERIAL_MEDIA;

	fxp_sysctl_node(sc);
	/*
	 * Enable workarounds for certain chip revision deficiencies.
	 *
	 * Systems based on the ICH2/ICH2-M chip from Intel, and possibly
	 * some systems based a normal 82559 design, have a defect where
	 * the chip can cause a PCI protocol violation if it receives
	 * a CU_RESUME command when it is entering the IDLE state.  The
	 * workaround is to disable Dynamic Standby Mode, so the chip never
	 * deasserts CLKRUN#, and always remains in an active state.
	 *
	 * See Intel 82801BA/82801BAM Specification Update, Errata #30.
	 */
	if ((sc->ident->ich >= 2 && sc->ident->ich <= 3) ||
	    (sc->ident->ich == 0 && sc->revision >= FXP_REV_82559_A0)) {
		data = sc->eeprom[FXP_EEPROM_MAP_ID];
		if (data & 0x02) {			/* STB enable */
			uint16_t cksum;
			int i;

			device_printf(dev,
			    "Disabling dynamic standby mode in EEPROM\n");
			data &= ~0x02;
			sc->eeprom[FXP_EEPROM_MAP_ID] = data;
			fxp_write_eeprom(sc, &data, FXP_EEPROM_MAP_ID, 1);
			device_printf(dev, "New EEPROM ID: 0x%x\n", data);
			cksum = 0;
			for (i = 0; i < (1 << sc->eeprom_size) - 1; i++)
				cksum += sc->eeprom[i];
			i = (1 << sc->eeprom_size) - 1;
			cksum = 0xBABA - cksum;
			fxp_write_eeprom(sc, &cksum, i, 1);
			device_printf(dev,
			    "EEPROM checksum @ 0x%x: 0x%x -> 0x%x\n",
			    i, sc->eeprom[i], cksum);
			sc->eeprom[i] = cksum;
			/*
			 * If the user elects to continue, try the software
			 * workaround, as it is better than nothing.
			 */
			sc->flags |= FXP_FLAG_CU_RESUME_BUG;
		}
	}

	/*
	 * If we are not a 82557 chip, we can enable extended features.
	 */
	if (sc->revision != FXP_REV_82557) {
		/*
		 * If MWI is enabled in the PCI configuration, and there
		 * is a valid cacheline size (8 or 16 dwords), then tell
		 * the board to turn on MWI.
		 */
		val = pci_read_config(dev, PCIR_COMMAND, 2);
		if (val & PCIM_CMD_MWRICEN &&
		    pci_read_config(dev, PCIR_CACHELNSZ, 1) != 0)
			sc->flags |= FXP_FLAG_MWI_ENABLE;

		/* turn on the extended TxCB feature */
		sc->flags |= FXP_FLAG_EXT_TXCB;

		/* enable reception of long frames for VLAN */
		sc->flags |= FXP_FLAG_LONG_PKT_EN;
	} else {
		/* a hack to get long VLAN frames on a 82557 */
		sc->flags |= FXP_FLAG_SAVE_BAD;
	}

	/* For 82559 or later chips, Rx checksum offload is supported. */
	if (sc->revision >= FXP_REV_82559_A0) {
		/* 82559ER does not support Rx checksum offloading. */
		if (sc->ident->device != 0x1209)
			sc->flags |= FXP_FLAG_82559_RXCSUM;
	}
	/*
	 * Enable use of extended RFDs and TCBs for 82550
	 * and later chips. Note: we need extended TXCB support
	 * too, but that's already enabled by the code above.
	 * Be careful to do this only on the right devices.
	 */
	if (sc->revision == FXP_REV_82550 || sc->revision == FXP_REV_82550_C ||
	    sc->revision == FXP_REV_82551_E || sc->revision == FXP_REV_82551_F
	    || sc->revision == FXP_REV_82551_10) {
		sc->rfa_size = sizeof (struct fxp_rfa);
		sc->tx_cmd = FXP_CB_COMMAND_IPCBXMIT;
		sc->flags |= FXP_FLAG_EXT_RFA;
		/* Use extended RFA instead of 82559 checksum mode. */
		sc->flags &= ~FXP_FLAG_82559_RXCSUM;
	} else {
		sc->rfa_size = sizeof (struct fxp_rfa) - FXP_RFAX_LEN;
		sc->tx_cmd = FXP_CB_COMMAND_XMIT;
	}

	/*
	 * Allocate DMA tags and DMA safe memory.
	 */
	sc->maxtxseg = FXP_NTXSEG;
	sc->maxsegsize = MCLBYTES;
	if (sc->flags & FXP_FLAG_EXT_RFA) {
		sc->maxtxseg--;
		sc->maxsegsize = FXP_TSO_SEGSIZE;
	}
	error = bus_dma_tag_create(bus_get_dma_tag(dev), 2, 0,
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL,
	    sc->maxsegsize * sc->maxtxseg + sizeof(struct ether_vlan_header),
	    sc->maxtxseg, sc->maxsegsize, 0,
	    busdma_lock_mutex, &Giant, &sc->fxp_txmtag);
	if (error) {
		device_printf(dev, "could not create TX DMA tag\n");
		goto fail;
	}

	error = bus_dma_tag_create(bus_get_dma_tag(dev), 2, 0,
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL,
	    MCLBYTES, 1, MCLBYTES, 0,
	    busdma_lock_mutex, &Giant, &sc->fxp_rxmtag);
	if (error) {
		device_printf(dev, "could not create RX DMA tag\n");
		goto fail;
	}

	error = bus_dma_tag_create(bus_get_dma_tag(dev), 4, 0,
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL,
	    sizeof(struct fxp_stats), 1, sizeof(struct fxp_stats), 0,
	    busdma_lock_mutex, &Giant, &sc->fxp_stag);
	if (error) {
		device_printf(dev, "could not create stats DMA tag\n");
		goto fail;
	}

	error = bus_dmamem_alloc(sc->fxp_stag, (void **)&sc->fxp_stats,
	    BUS_DMA_NOWAIT | BUS_DMA_COHERENT | BUS_DMA_ZERO, &sc->fxp_smap);
	if (error) {
		device_printf(dev, "could not allocate stats DMA memory\n");
		goto fail;
	}
	error = bus_dmamap_load(sc->fxp_stag, sc->fxp_smap, sc->fxp_stats,
	    sizeof(struct fxp_stats), fxp_dma_map_addr, &sc->stats_addr,
	    BUS_DMA_NOWAIT);
	if (error) {
		device_printf(dev, "could not load the stats DMA buffer\n");
		goto fail;
	}

	error = bus_dma_tag_create(bus_get_dma_tag(dev), 4, 0,
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL,
	    FXP_TXCB_SZ, 1, FXP_TXCB_SZ, 0,
	    busdma_lock_mutex, &Giant, &sc->cbl_tag);
	if (error) {
		device_printf(dev, "could not create TxCB DMA tag\n");
		goto fail;
	}

	error = bus_dmamem_alloc(sc->cbl_tag, (void **)&sc->fxp_desc.cbl_list,
	    BUS_DMA_NOWAIT | BUS_DMA_COHERENT | BUS_DMA_ZERO, &sc->cbl_map);
	if (error) {
		device_printf(dev, "could not allocate TxCB DMA memory\n");
		goto fail;
	}

	error = bus_dmamap_load(sc->cbl_tag, sc->cbl_map,
	    sc->fxp_desc.cbl_list, FXP_TXCB_SZ, fxp_dma_map_addr,
	    &sc->fxp_desc.cbl_addr, BUS_DMA_NOWAIT);
	if (error) {
		device_printf(dev, "could not load TxCB DMA buffer\n");
		goto fail;
	}

	error = bus_dma_tag_create(bus_get_dma_tag(dev), 4, 0,
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL,
	    sizeof(struct fxp_cb_mcs), 1, sizeof(struct fxp_cb_mcs), 0,
	    busdma_lock_mutex, &Giant, &sc->mcs_tag);
	if (error) {
		device_printf(dev,
		    "could not create multicast setup DMA tag\n");
		goto fail;
	}

	error = bus_dmamem_alloc(sc->mcs_tag, (void **)&sc->mcsp,
	    BUS_DMA_NOWAIT | BUS_DMA_COHERENT | BUS_DMA_ZERO, &sc->mcs_map);
	if (error) {
		device_printf(dev,
		    "could not allocate multicast setup DMA memory\n");
		goto fail;
	}
	error = bus_dmamap_load(sc->mcs_tag, sc->mcs_map, sc->mcsp,
	    sizeof(struct fxp_cb_mcs), fxp_dma_map_addr, &sc->mcs_addr,
	    BUS_DMA_NOWAIT);
	if (error) {
		device_printf(dev,
		    "can't load the multicast setup DMA buffer\n");
		goto fail;
	}

	/*
	 * Pre-allocate the TX DMA maps and setup the pointers to
	 * the TX command blocks.
	 */
	txp = sc->fxp_desc.tx_list;
	tcbp = sc->fxp_desc.cbl_list;
	for (i = 0; i < FXP_NTXCB; i++) {
		txp[i].tx_cb = tcbp + i;
		error = bus_dmamap_create(sc->fxp_txmtag, 0, &txp[i].tx_map);
		if (error) {
			device_printf(dev, "can't create DMA map for TX\n");
			goto fail;
		}
	}
	error = bus_dmamap_create(sc->fxp_rxmtag, 0, &sc->spare_map);
	if (error) {
		device_printf(dev, "can't create spare DMA map\n");
		goto fail;
	}

	/*
	 * Pre-allocate our receive buffers.
	 */
	sc->fxp_desc.rx_head = sc->fxp_desc.rx_tail = NULL;
	for (i = 0; i < FXP_NRFABUFS; i++) {
		rxp = &sc->fxp_desc.rx_list[i];
		error = bus_dmamap_create(sc->fxp_rxmtag, 0, &rxp->rx_map);
		if (error) {
			device_printf(dev, "can't create DMA map for RX\n");
			goto fail;
		}
		if (fxp_new_rfabuf(sc, rxp) != 0) {
			error = ENOMEM;
			goto fail;
		}
		fxp_add_rfabuf(sc, rxp);
	}

	/*
	 * Read MAC address.
	 */
	eaddr[0] = sc->eeprom[FXP_EEPROM_MAP_IA0] & 0xff;
	eaddr[1] = sc->eeprom[FXP_EEPROM_MAP_IA0] >> 8;
	eaddr[2] = sc->eeprom[FXP_EEPROM_MAP_IA1] & 0xff;
	eaddr[3] = sc->eeprom[FXP_EEPROM_MAP_IA1] >> 8;
	eaddr[4] = sc->eeprom[FXP_EEPROM_MAP_IA2] & 0xff;
	eaddr[5] = sc->eeprom[FXP_EEPROM_MAP_IA2] >> 8;
	if (bootverbose) {
		device_printf(dev, "PCI IDs: %04x %04x %04x %04x %04x\n",
		    pci_get_vendor(dev), pci_get_device(dev),
		    pci_get_subvendor(dev), pci_get_subdevice(dev),
		    pci_get_revid(dev));
		device_printf(dev, "Dynamic Standby mode is %s\n",
		    sc->eeprom[FXP_EEPROM_MAP_ID] & 0x02 ? "enabled" :
		    "disabled");
	}

	/*
	 * If this is only a 10Mbps device, then there is no MII, and
	 * the PHY will use a serial interface instead.
	 *
	 * The Seeq 80c24 AutoDUPLEX(tm) Ethernet Interface Adapter
	 * doesn't have a programming interface of any sort.  The
	 * media is sensed automatically based on how the link partner
	 * is configured.  This is, in essence, manual configuration.
	 */
	if (sc->flags & FXP_FLAG_SERIAL_MEDIA) {
		ifmedia_add(&sc->sc_media, IFM_ETHER|IFM_MANUAL, 0, NULL);
		ifmedia_set(&sc->sc_media, IFM_ETHER|IFM_MANUAL);
	} else {
		/*
		 * i82557 wedge when isolating all of their PHYs.
		 */
		flags = MIIF_NOISOLATE;
		if (sc->revision >= FXP_REV_82558_A4)
			flags |= MIIF_DOPAUSE;
		error = mii_attach(dev, &sc->miibus, ifp,
		    (ifm_change_cb_t)fxp_ifmedia_upd,
		    (ifm_stat_cb_t)fxp_ifmedia_sts, BMSR_DEFCAPMASK,
		    MII_PHY_ANY, MII_OFFSET_ANY, flags);
		if (error != 0) {
			device_printf(dev, "attaching PHYs failed\n");
			goto fail;
		}
	}

	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	if_setdev(ifp, dev);
	if_setinitfn(ifp, fxp_init);
	if_setsoftc(ifp, sc);
	if_setflags(ifp, IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST);
	if_setioctlfn(ifp, fxp_ioctl);
	if_setstartfn(ifp, fxp_start);

	if_setcapabilities(ifp, 0);
	if_setcapenable(ifp, 0);

	/* Enable checksum offload/TSO for 82550 or better chips */
	if (sc->flags & FXP_FLAG_EXT_RFA) {
		if_sethwassist(ifp, FXP_CSUM_FEATURES | CSUM_TSO);
		if_setcapabilitiesbit(ifp, IFCAP_HWCSUM | IFCAP_TSO4, 0);
		if_setcapenablebit(ifp, IFCAP_HWCSUM | IFCAP_TSO4, 0);
	}

	if (sc->flags & FXP_FLAG_82559_RXCSUM) {
		if_setcapabilitiesbit(ifp, IFCAP_RXCSUM, 0);
		if_setcapenablebit(ifp, IFCAP_RXCSUM, 0);
	}

	if (sc->flags & FXP_FLAG_WOLCAP) {
		if_setcapabilitiesbit(ifp, IFCAP_WOL_MAGIC, 0);
		if_setcapenablebit(ifp, IFCAP_WOL_MAGIC, 0);
	}

#ifdef DEVICE_POLLING
	/* Inform the world we support polling. */
	if_setcapabilitiesbit(ifp, IFCAP_POLLING, 0);
#endif

	/*
	 * Attach the interface.
	 */
	ether_ifattach(ifp, eaddr);

	/*
	 * Tell the upper layer(s) we support long frames.
	 * Must appear after the call to ether_ifattach() because
	 * ether_ifattach() sets ifi_hdrlen to the default value.
	 */
	if_setifheaderlen(ifp, sizeof(struct ether_vlan_header));
	if_setcapabilitiesbit(ifp, IFCAP_VLAN_MTU, 0);
	if_setcapenablebit(ifp, IFCAP_VLAN_MTU, 0);
	if ((sc->flags & FXP_FLAG_EXT_RFA) != 0) {
		if_setcapabilitiesbit(ifp, IFCAP_VLAN_HWTAGGING |
		    IFCAP_VLAN_HWCSUM | IFCAP_VLAN_HWTSO, 0);
		if_setcapenablebit(ifp, IFCAP_VLAN_HWTAGGING |
		    IFCAP_VLAN_HWCSUM | IFCAP_VLAN_HWTSO, 0);
	}

	/*
	 * Let the system queue as many packets as we have available
	 * TX descriptors.
	 */
	if_setsendqlen(ifp, FXP_NTXCB - 1);
	if_setsendqready(ifp);

	/*
	 * Hook our interrupt after all initialization is complete.
	 */
	error = bus_setup_intr(dev, sc->fxp_res[1], INTR_TYPE_NET | INTR_MPSAFE,
			       NULL, fxp_intr, sc, &sc->ih);
	if (error) {
		device_printf(dev, "could not setup irq\n");
		ether_ifdetach(sc->ifp);
		goto fail;
	}

	/*
	 * Configure hardware to reject magic frames otherwise
	 * system will hang on recipt of magic frames.
	 */
	if ((sc->flags & FXP_FLAG_WOLCAP) != 0) {
		FXP_LOCK(sc);
		/* Clear wakeup events. */
		CSR_WRITE_1(sc, FXP_CSR_PMDR, CSR_READ_1(sc, FXP_CSR_PMDR));
		fxp_init_body(sc, 0);
		fxp_stop(sc);
		FXP_UNLOCK(sc);
	}

fail:
	if (error)
		fxp_release(sc);
	return (error);
}

/*
 * Release all resources.  The softc lock should not be held and the
 * interrupt should already be torn down.
 */
static void
fxp_release(struct fxp_softc *sc)
{
	struct fxp_rx *rxp;
	struct fxp_tx *txp;
	int i;

	FXP_LOCK_ASSERT(sc, MA_NOTOWNED);
	KASSERT(sc->ih == NULL,
	    ("fxp_release() called with intr handle still active"));
	if (sc->miibus)
		device_delete_child(sc->dev, sc->miibus);
	bus_generic_detach(sc->dev);
	ifmedia_removeall(&sc->sc_media);
	if (sc->fxp_desc.cbl_list) {
		bus_dmamap_unload(sc->cbl_tag, sc->cbl_map);
		bus_dmamem_free(sc->cbl_tag, sc->fxp_desc.cbl_list,
		    sc->cbl_map);
	}
	if (sc->fxp_stats) {
		bus_dmamap_unload(sc->fxp_stag, sc->fxp_smap);
		bus_dmamem_free(sc->fxp_stag, sc->fxp_stats, sc->fxp_smap);
	}
	if (sc->mcsp) {
		bus_dmamap_unload(sc->mcs_tag, sc->mcs_map);
		bus_dmamem_free(sc->mcs_tag, sc->mcsp, sc->mcs_map);
	}
	bus_release_resources(sc->dev, sc->fxp_spec, sc->fxp_res);
	if (sc->fxp_rxmtag) {
		for (i = 0; i < FXP_NRFABUFS; i++) {
			rxp = &sc->fxp_desc.rx_list[i];
			if (rxp->rx_mbuf != NULL) {
				bus_dmamap_sync(sc->fxp_rxmtag, rxp->rx_map,
				    BUS_DMASYNC_POSTREAD);
				bus_dmamap_unload(sc->fxp_rxmtag, rxp->rx_map);
				m_freem(rxp->rx_mbuf);
			}
			bus_dmamap_destroy(sc->fxp_rxmtag, rxp->rx_map);
		}
		bus_dmamap_destroy(sc->fxp_rxmtag, sc->spare_map);
		bus_dma_tag_destroy(sc->fxp_rxmtag);
	}
	if (sc->fxp_txmtag) {
		for (i = 0; i < FXP_NTXCB; i++) {
			txp = &sc->fxp_desc.tx_list[i];
			if (txp->tx_mbuf != NULL) {
				bus_dmamap_sync(sc->fxp_txmtag, txp->tx_map,
				    BUS_DMASYNC_POSTWRITE);
				bus_dmamap_unload(sc->fxp_txmtag, txp->tx_map);
				m_freem(txp->tx_mbuf);
			}
			bus_dmamap_destroy(sc->fxp_txmtag, txp->tx_map);
		}
		bus_dma_tag_destroy(sc->fxp_txmtag);
	}
	if (sc->fxp_stag)
		bus_dma_tag_destroy(sc->fxp_stag);
	if (sc->cbl_tag)
		bus_dma_tag_destroy(sc->cbl_tag);
	if (sc->mcs_tag)
		bus_dma_tag_destroy(sc->mcs_tag);
	if (sc->ifp)
		if_free(sc->ifp);

	mtx_destroy(&sc->sc_mtx);
}

/*
 * Detach interface.
 */
static int
fxp_detach(device_t dev)
{
	struct fxp_softc *sc = device_get_softc(dev);

#ifdef DEVICE_POLLING
	if (if_getcapenable(sc->ifp) & IFCAP_POLLING)
		ether_poll_deregister(sc->ifp);
#endif

	FXP_LOCK(sc);
	/*
	 * Stop DMA and drop transmit queue, but disable interrupts first.
	 */
	CSR_WRITE_1(sc, FXP_CSR_SCB_INTRCNTL, FXP_SCB_INTR_DISABLE);
	fxp_stop(sc);
	FXP_UNLOCK(sc);
	callout_drain(&sc->stat_ch);

	/*
	 * Close down routes etc.
	 */
	ether_ifdetach(sc->ifp);

	/*
	 * Unhook interrupt before dropping lock. This is to prevent
	 * races with fxp_intr().
	 */
	bus_teardown_intr(sc->dev, sc->fxp_res[1], sc->ih);
	sc->ih = NULL;

	/* Release our allocated resources. */
	fxp_release(sc);
	return (0);
}

/*
 * Device shutdown routine. Called at system shutdown after sync. The
 * main purpose of this routine is to shut off receiver DMA so that
 * kernel memory doesn't get clobbered during warmboot.
 */
static int
fxp_shutdown(device_t dev)
{

	/*
	 * Make sure that DMA is disabled prior to reboot. Not doing
	 * do could allow DMA to corrupt kernel memory during the
	 * reboot before the driver initializes.
	 */
	return (fxp_suspend(dev));
}

/*
 * Device suspend routine.  Stop the interface and save some PCI
 * settings in case the BIOS doesn't restore them properly on
 * resume.
 */
static int
fxp_suspend(device_t dev)
{
	struct fxp_softc *sc = device_get_softc(dev);
	if_t ifp;
	int pmc;
	uint16_t pmstat;

	FXP_LOCK(sc);

	ifp = sc->ifp;
	if (pci_find_cap(sc->dev, PCIY_PMG, &pmc) == 0) {
		pmstat = pci_read_config(sc->dev, pmc + PCIR_POWER_STATUS, 2);
		pmstat &= ~(PCIM_PSTAT_PME | PCIM_PSTAT_PMEENABLE);
		if ((if_getcapenable(ifp) & IFCAP_WOL_MAGIC) != 0) {
			/* Request PME. */
			pmstat |= PCIM_PSTAT_PME | PCIM_PSTAT_PMEENABLE;
			sc->flags |= FXP_FLAG_WOL;
			/* Reconfigure hardware to accept magic frames. */
			if_setdrvflagbits(ifp, 0, IFF_DRV_RUNNING);
			fxp_init_body(sc, 0);
		}
		pci_write_config(sc->dev, pmc + PCIR_POWER_STATUS, pmstat, 2);
	}
	fxp_stop(sc);

	sc->suspended = 1;

	FXP_UNLOCK(sc);
	return (0);
}

/*
 * Device resume routine. re-enable busmastering, and restart the interface if
 * appropriate.
 */
static int
fxp_resume(device_t dev)
{
	struct fxp_softc *sc = device_get_softc(dev);
	if_t ifp = sc->ifp;
	int pmc;
	uint16_t pmstat;

	FXP_LOCK(sc);

	if (pci_find_cap(sc->dev, PCIY_PMG, &pmc) == 0) {
		sc->flags &= ~FXP_FLAG_WOL;
		pmstat = pci_read_config(sc->dev, pmc + PCIR_POWER_STATUS, 2);
		/* Disable PME and clear PME status. */
		pmstat &= ~PCIM_PSTAT_PMEENABLE;
		pci_write_config(sc->dev, pmc + PCIR_POWER_STATUS, pmstat, 2);
		if ((sc->flags & FXP_FLAG_WOLCAP) != 0)
			CSR_WRITE_1(sc, FXP_CSR_PMDR,
			    CSR_READ_1(sc, FXP_CSR_PMDR));
	}

	CSR_WRITE_4(sc, FXP_CSR_PORT, FXP_PORT_SELECTIVE_RESET);
	DELAY(10);

	/* reinitialize interface if necessary */
	if (if_getflags(ifp) & IFF_UP)
		fxp_init_body(sc, 1);

	sc->suspended = 0;

	FXP_UNLOCK(sc);
	return (0);
}

static void
fxp_eeprom_shiftin(struct fxp_softc *sc, int data, int length)
{
	uint16_t reg;
	int x;

	/*
	 * Shift in data.
	 */
	for (x = 1 << (length - 1); x; x >>= 1) {
		if (data & x)
			reg = FXP_EEPROM_EECS | FXP_EEPROM_EEDI;
		else
			reg = FXP_EEPROM_EECS;
		CSR_WRITE_2(sc, FXP_CSR_EEPROMCONTROL, reg);
		DELAY(1);
		CSR_WRITE_2(sc, FXP_CSR_EEPROMCONTROL, reg | FXP_EEPROM_EESK);
		DELAY(1);
		CSR_WRITE_2(sc, FXP_CSR_EEPROMCONTROL, reg);
		DELAY(1);
	}
}

/*
 * Read from the serial EEPROM. Basically, you manually shift in
 * the read opcode (one bit at a time) and then shift in the address,
 * and then you shift out the data (all of this one bit at a time).
 * The word size is 16 bits, so you have to provide the address for
 * every 16 bits of data.
 */
static uint16_t
fxp_eeprom_getword(struct fxp_softc *sc, int offset, int autosize)
{
	uint16_t reg, data;
	int x;

	CSR_WRITE_2(sc, FXP_CSR_EEPROMCONTROL, FXP_EEPROM_EECS);
	/*
	 * Shift in read opcode.
	 */
	fxp_eeprom_shiftin(sc, FXP_EEPROM_OPC_READ, 3);
	/*
	 * Shift in address.
	 */
	data = 0;
	for (x = 1 << (sc->eeprom_size - 1); x; x >>= 1) {
		if (offset & x)
			reg = FXP_EEPROM_EECS | FXP_EEPROM_EEDI;
		else
			reg = FXP_EEPROM_EECS;
		CSR_WRITE_2(sc, FXP_CSR_EEPROMCONTROL, reg);
		DELAY(1);
		CSR_WRITE_2(sc, FXP_CSR_EEPROMCONTROL, reg | FXP_EEPROM_EESK);
		DELAY(1);
		CSR_WRITE_2(sc, FXP_CSR_EEPROMCONTROL, reg);
		DELAY(1);
		reg = CSR_READ_2(sc, FXP_CSR_EEPROMCONTROL) & FXP_EEPROM_EEDO;
		data++;
		if (autosize && reg == 0) {
			sc->eeprom_size = data;
			break;
		}
	}
	/*
	 * Shift out data.
	 */
	data = 0;
	reg = FXP_EEPROM_EECS;
	for (x = 1 << 15; x; x >>= 1) {
		CSR_WRITE_2(sc, FXP_CSR_EEPROMCONTROL, reg | FXP_EEPROM_EESK);
		DELAY(1);
		if (CSR_READ_2(sc, FXP_CSR_EEPROMCONTROL) & FXP_EEPROM_EEDO)
			data |= x;
		CSR_WRITE_2(sc, FXP_CSR_EEPROMCONTROL, reg);
		DELAY(1);
	}
	CSR_WRITE_2(sc, FXP_CSR_EEPROMCONTROL, 0);
	DELAY(1);

	return (data);
}

static void
fxp_eeprom_putword(struct fxp_softc *sc, int offset, uint16_t data)
{
	int i;

	/*
	 * Erase/write enable.
	 */
	CSR_WRITE_2(sc, FXP_CSR_EEPROMCONTROL, FXP_EEPROM_EECS);
	fxp_eeprom_shiftin(sc, 0x4, 3);
	fxp_eeprom_shiftin(sc, 0x03 << (sc->eeprom_size - 2), sc->eeprom_size);
	CSR_WRITE_2(sc, FXP_CSR_EEPROMCONTROL, 0);
	DELAY(1);
	/*
	 * Shift in write opcode, address, data.
	 */
	CSR_WRITE_2(sc, FXP_CSR_EEPROMCONTROL, FXP_EEPROM_EECS);
	fxp_eeprom_shiftin(sc, FXP_EEPROM_OPC_WRITE, 3);
	fxp_eeprom_shiftin(sc, offset, sc->eeprom_size);
	fxp_eeprom_shiftin(sc, data, 16);
	CSR_WRITE_2(sc, FXP_CSR_EEPROMCONTROL, 0);
	DELAY(1);
	/*
	 * Wait for EEPROM to finish up.
	 */
	CSR_WRITE_2(sc, FXP_CSR_EEPROMCONTROL, FXP_EEPROM_EECS);
	DELAY(1);
	for (i = 0; i < 1000; i++) {
		if (CSR_READ_2(sc, FXP_CSR_EEPROMCONTROL) & FXP_EEPROM_EEDO)
			break;
		DELAY(50);
	}
	CSR_WRITE_2(sc, FXP_CSR_EEPROMCONTROL, 0);
	DELAY(1);
	/*
	 * Erase/write disable.
	 */
	CSR_WRITE_2(sc, FXP_CSR_EEPROMCONTROL, FXP_EEPROM_EECS);
	fxp_eeprom_shiftin(sc, 0x4, 3);
	fxp_eeprom_shiftin(sc, 0, sc->eeprom_size);
	CSR_WRITE_2(sc, FXP_CSR_EEPROMCONTROL, 0);
	DELAY(1);
}

/*
 * From NetBSD:
 *
 * Figure out EEPROM size.
 *
 * 559's can have either 64-word or 256-word EEPROMs, the 558
 * datasheet only talks about 64-word EEPROMs, and the 557 datasheet
 * talks about the existence of 16 to 256 word EEPROMs.
 *
 * The only known sizes are 64 and 256, where the 256 version is used
 * by CardBus cards to store CIS information.
 *
 * The address is shifted in msb-to-lsb, and after the last
 * address-bit the EEPROM is supposed to output a `dummy zero' bit,
 * after which follows the actual data. We try to detect this zero, by
 * probing the data-out bit in the EEPROM control register just after
 * having shifted in a bit. If the bit is zero, we assume we've
 * shifted enough address bits. The data-out should be tri-state,
 * before this, which should translate to a logical one.
 */
static void
fxp_autosize_eeprom(struct fxp_softc *sc)
{

	/* guess maximum size of 256 words */
	sc->eeprom_size = 8;

	/* autosize */
	(void) fxp_eeprom_getword(sc, 0, 1);
}

static void
fxp_read_eeprom(struct fxp_softc *sc, u_short *data, int offset, int words)
{
	int i;

	for (i = 0; i < words; i++)
		data[i] = fxp_eeprom_getword(sc, offset + i, 0);
}

static void
fxp_write_eeprom(struct fxp_softc *sc, u_short *data, int offset, int words)
{
	int i;

	for (i = 0; i < words; i++)
		fxp_eeprom_putword(sc, offset + i, data[i]);
}

static void
fxp_load_eeprom(struct fxp_softc *sc)
{
	int i;
	uint16_t cksum;

	fxp_read_eeprom(sc, sc->eeprom, 0, 1 << sc->eeprom_size);
	cksum = 0;
	for (i = 0; i < (1 << sc->eeprom_size) - 1; i++)
		cksum += sc->eeprom[i];
	cksum = 0xBABA - cksum;
	if (cksum != sc->eeprom[(1 << sc->eeprom_size) - 1])
		device_printf(sc->dev,
		    "EEPROM checksum mismatch! (0x%04x -> 0x%04x)\n",
		    cksum, sc->eeprom[(1 << sc->eeprom_size) - 1]);
}

/*
 * Grab the softc lock and call the real fxp_start_body() routine
 */
static void
fxp_start(if_t ifp)
{
	struct fxp_softc *sc = if_getsoftc(ifp);

	FXP_LOCK(sc);
	fxp_start_body(ifp);
	FXP_UNLOCK(sc);
}

/*
 * Start packet transmission on the interface.
 * This routine must be called with the softc lock held, and is an
 * internal entry point only.
 */
static void
fxp_start_body(if_t ifp)
{
	struct fxp_softc *sc = if_getsoftc(ifp);
	struct mbuf *mb_head;
	int txqueued;

	FXP_LOCK_ASSERT(sc, MA_OWNED);

	if ((if_getdrvflags(ifp) & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) !=
	    IFF_DRV_RUNNING)
		return;

	if (sc->tx_queued > FXP_NTXCB_HIWAT)
		fxp_txeof(sc);
	/*
	 * We're finished if there is nothing more to add to the list or if
	 * we're all filled up with buffers to transmit.
	 * NOTE: One TxCB is reserved to guarantee that fxp_mc_setup() can add
	 *       a NOP command when needed.
	 */
	txqueued = 0;
	while (!if_sendq_empty(ifp) && sc->tx_queued < FXP_NTXCB - 1) {

		/*
		 * Grab a packet to transmit.
		 */
		mb_head = if_dequeue(ifp);
		if (mb_head == NULL)
			break;

		if (fxp_encap(sc, &mb_head)) {
			if (mb_head == NULL)
				break;
			if_sendq_prepend(ifp, mb_head);
			if_setdrvflagbits(ifp, IFF_DRV_OACTIVE, 0);
		}
		txqueued++;
		/*
		 * Pass packet to bpf if there is a listener.
		 */
		if_bpfmtap(ifp, mb_head);
	}

	/*
	 * We're finished. If we added to the list, issue a RESUME to get DMA
	 * going again if suspended.
	 */
	if (txqueued > 0) {
		bus_dmamap_sync(sc->cbl_tag, sc->cbl_map,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
		fxp_scb_wait(sc);
		fxp_scb_cmd(sc, FXP_SCB_COMMAND_CU_RESUME);
		/*
		 * Set a 5 second timer just in case we don't hear
		 * from the card again.
		 */
		sc->watchdog_timer = 5;
	}
}

static int
fxp_encap(struct fxp_softc *sc, struct mbuf **m_head)
{
	if_t ifp;
	struct mbuf *m;
	struct fxp_tx *txp;
	struct fxp_cb_tx *cbp;
	struct tcphdr *tcp;
	bus_dma_segment_t segs[FXP_NTXSEG];
	int error, i, nseg, tcp_payload;

	FXP_LOCK_ASSERT(sc, MA_OWNED);
	ifp = sc->ifp;

	tcp_payload = 0;
	tcp = NULL;
	/*
	 * Get pointer to next available tx desc.
	 */
	txp = sc->fxp_desc.tx_last->tx_next;

	/*
	 * A note in Appendix B of the Intel 8255x 10/100 Mbps
	 * Ethernet Controller Family Open Source Software
	 * Developer Manual says:
	 *   Using software parsing is only allowed with legal
	 *   TCP/IP or UDP/IP packets.
	 *   ...
	 *   For all other datagrams, hardware parsing must
	 *   be used.
	 * Software parsing appears to truncate ICMP and
	 * fragmented UDP packets that contain one to three
	 * bytes in the second (and final) mbuf of the packet.
	 */
	if (sc->flags & FXP_FLAG_EXT_RFA)
		txp->tx_cb->ipcb_ip_activation_high =
		    FXP_IPCB_HARDWAREPARSING_ENABLE;

	m = *m_head;
	if (m->m_pkthdr.csum_flags & CSUM_TSO) {
		/*
		 * 82550/82551 requires ethernet/IP/TCP headers must be
		 * contained in the first active transmit buffer.
		 */
		struct ether_header *eh;
		struct ip *ip;
		uint32_t ip_off, poff;

		if (M_WRITABLE(*m_head) == 0) {
			/* Get a writable copy. */
			m = m_dup(*m_head, M_NOWAIT);
			m_freem(*m_head);
			if (m == NULL) {
				*m_head = NULL;
				return (ENOBUFS);
			}
			*m_head = m;
		}
		ip_off = sizeof(struct ether_header);
		m = m_pullup(*m_head, ip_off);
		if (m == NULL) {
			*m_head = NULL;
			return (ENOBUFS);
		}
		eh = mtod(m, struct ether_header *);
		/* Check the existence of VLAN tag. */
		if (eh->ether_type == htons(ETHERTYPE_VLAN)) {
			ip_off = sizeof(struct ether_vlan_header);
			m = m_pullup(m, ip_off);
			if (m == NULL) {
				*m_head = NULL;
				return (ENOBUFS);
			}
		}
		m = m_pullup(m, ip_off + sizeof(struct ip));
		if (m == NULL) {
			*m_head = NULL;
			return (ENOBUFS);
		}
		ip = (struct ip *)(mtod(m, char *) + ip_off);
		poff = ip_off + (ip->ip_hl << 2);
		m = m_pullup(m, poff + sizeof(struct tcphdr));
		if (m == NULL) {
			*m_head = NULL;
			return (ENOBUFS);
		}
		tcp = (struct tcphdr *)(mtod(m, char *) + poff);
		m = m_pullup(m, poff + (tcp->th_off << 2));
		if (m == NULL) {
			*m_head = NULL;
			return (ENOBUFS);
		}

		/*
		 * Since 82550/82551 doesn't modify IP length and pseudo
		 * checksum in the first frame driver should compute it.
		 */
		ip = (struct ip *)(mtod(m, char *) + ip_off);
		tcp = (struct tcphdr *)(mtod(m, char *) + poff);
		ip->ip_sum = 0;
		ip->ip_len = htons(m->m_pkthdr.tso_segsz + (ip->ip_hl << 2) +
		    (tcp->th_off << 2));
		tcp->th_sum = in_pseudo(ip->ip_src.s_addr, ip->ip_dst.s_addr,
		    htons(IPPROTO_TCP + (tcp->th_off << 2) +
		    m->m_pkthdr.tso_segsz));
		/* Compute total TCP payload. */
		tcp_payload = m->m_pkthdr.len - ip_off - (ip->ip_hl << 2);
		tcp_payload -= tcp->th_off << 2;
		*m_head = m;
	} else if (m->m_pkthdr.csum_flags & FXP_CSUM_FEATURES) {
		/*
		 * Deal with TCP/IP checksum offload. Note that
		 * in order for TCP checksum offload to work,
		 * the pseudo header checksum must have already
		 * been computed and stored in the checksum field
		 * in the TCP header. The stack should have
		 * already done this for us.
		 */
		txp->tx_cb->ipcb_ip_schedule = FXP_IPCB_TCPUDP_CHECKSUM_ENABLE;
		if (m->m_pkthdr.csum_flags & CSUM_TCP)
			txp->tx_cb->ipcb_ip_schedule |= FXP_IPCB_TCP_PACKET;

#ifdef FXP_IP_CSUM_WAR
		/*
		 * XXX The 82550 chip appears to have trouble
		 * dealing with IP header checksums in very small
		 * datagrams, namely fragments from 1 to 3 bytes
		 * in size. For example, say you want to transmit
		 * a UDP packet of 1473 bytes. The packet will be
		 * fragmented over two IP datagrams, the latter
		 * containing only one byte of data. The 82550 will
		 * botch the header checksum on the 1-byte fragment.
		 * As long as the datagram contains 4 or more bytes
		 * of data, you're ok.
		 *
                 * The following code attempts to work around this
		 * problem: if the datagram is less than 38 bytes
		 * in size (14 bytes ether header, 20 bytes IP header,
		 * plus 4 bytes of data), we punt and compute the IP
		 * header checksum by hand. This workaround doesn't
		 * work very well, however, since it can be fooled
		 * by things like VLAN tags and IP options that make
		 * the header sizes/offsets vary.
		 */

		if (m->m_pkthdr.csum_flags & CSUM_IP) {
			if (m->m_pkthdr.len < 38) {
				struct ip *ip;
				m->m_data += ETHER_HDR_LEN;
				ip = mtod(m, struct ip *);
				ip->ip_sum = in_cksum(m, ip->ip_hl << 2);
				m->m_data -= ETHER_HDR_LEN;
				m->m_pkthdr.csum_flags &= ~CSUM_IP;
			} else {
				txp->tx_cb->ipcb_ip_activation_high =
				    FXP_IPCB_HARDWAREPARSING_ENABLE;
				txp->tx_cb->ipcb_ip_schedule |=
				    FXP_IPCB_IP_CHECKSUM_ENABLE;
			}
		}
#endif
	}

	error = bus_dmamap_load_mbuf_sg(sc->fxp_txmtag, txp->tx_map, *m_head,
	    segs, &nseg, 0);
	if (error == EFBIG) {
		m = m_collapse(*m_head, M_NOWAIT, sc->maxtxseg);
		if (m == NULL) {
			m_freem(*m_head);
			*m_head = NULL;
			return (ENOMEM);
		}
		*m_head = m;
		error = bus_dmamap_load_mbuf_sg(sc->fxp_txmtag, txp->tx_map,
		    *m_head, segs, &nseg, 0);
		if (error != 0) {
			m_freem(*m_head);
			*m_head = NULL;
			return (ENOMEM);
		}
	} else if (error != 0)
		return (error);
	if (nseg == 0) {
		m_freem(*m_head);
		*m_head = NULL;
		return (EIO);
	}

	KASSERT(nseg <= sc->maxtxseg, ("too many DMA segments"));
	bus_dmamap_sync(sc->fxp_txmtag, txp->tx_map, BUS_DMASYNC_PREWRITE);

	cbp = txp->tx_cb;
	for (i = 0; i < nseg; i++) {
		/*
		 * If this is an 82550/82551, then we're using extended
		 * TxCBs _and_ we're using checksum offload. This means
		 * that the TxCB is really an IPCB. One major difference
		 * between the two is that with plain extended TxCBs,
		 * the bottom half of the TxCB contains two entries from
		 * the TBD array, whereas IPCBs contain just one entry:
		 * one entry (8 bytes) has been sacrificed for the TCP/IP
		 * checksum offload control bits. So to make things work
		 * right, we have to start filling in the TBD array
		 * starting from a different place depending on whether
		 * the chip is an 82550/82551 or not.
		 */
		if (sc->flags & FXP_FLAG_EXT_RFA) {
			cbp->tbd[i + 1].tb_addr = htole32(segs[i].ds_addr);
			cbp->tbd[i + 1].tb_size = htole32(segs[i].ds_len);
		} else {
			cbp->tbd[i].tb_addr = htole32(segs[i].ds_addr);
			cbp->tbd[i].tb_size = htole32(segs[i].ds_len);
		}
	}
	if (sc->flags & FXP_FLAG_EXT_RFA) {
		/* Configure dynamic TBD for 82550/82551. */
		cbp->tbd_number = 0xFF;
		cbp->tbd[nseg].tb_size |= htole32(0x8000);
	} else
		cbp->tbd_number = nseg;
	/* Configure TSO. */
	if (m->m_pkthdr.csum_flags & CSUM_TSO) {
		cbp->tbdtso.tb_size = htole32(m->m_pkthdr.tso_segsz << 16);
		cbp->tbd[1].tb_size |= htole32(tcp_payload << 16);
		cbp->ipcb_ip_schedule |= FXP_IPCB_LARGESEND_ENABLE |
		    FXP_IPCB_IP_CHECKSUM_ENABLE |
		    FXP_IPCB_TCP_PACKET |
		    FXP_IPCB_TCPUDP_CHECKSUM_ENABLE;
	}
	/* Configure VLAN hardware tag insertion. */
	if ((m->m_flags & M_VLANTAG) != 0) {
		cbp->ipcb_vlan_id = htons(m->m_pkthdr.ether_vtag);
		txp->tx_cb->ipcb_ip_activation_high |=
		    FXP_IPCB_INSERTVLAN_ENABLE;
	}

	txp->tx_mbuf = m;
	txp->tx_cb->cb_status = 0;
	txp->tx_cb->byte_count = 0;
	if (sc->tx_queued != FXP_CXINT_THRESH - 1)
		txp->tx_cb->cb_command =
		    htole16(sc->tx_cmd | FXP_CB_COMMAND_SF |
		    FXP_CB_COMMAND_S);
	else
		txp->tx_cb->cb_command =
		    htole16(sc->tx_cmd | FXP_CB_COMMAND_SF |
		    FXP_CB_COMMAND_S | FXP_CB_COMMAND_I);
	if ((m->m_pkthdr.csum_flags & CSUM_TSO) == 0)
		txp->tx_cb->tx_threshold = tx_threshold;

	/*
	 * Advance the end of list forward.
	 */
	sc->fxp_desc.tx_last->tx_cb->cb_command &= htole16(~FXP_CB_COMMAND_S);
	sc->fxp_desc.tx_last = txp;

	/*
	 * Advance the beginning of the list forward if there are
	 * no other packets queued (when nothing is queued, tx_first
	 * sits on the last TxCB that was sent out).
	 */
	if (sc->tx_queued == 0)
		sc->fxp_desc.tx_first = txp;

	sc->tx_queued++;

	return (0);
}

#ifdef DEVICE_POLLING
static poll_handler_t fxp_poll;

static int
fxp_poll(if_t ifp, enum poll_cmd cmd, int count)
{
	struct fxp_softc *sc = if_getsoftc(ifp);
	uint8_t statack;
	int rx_npkts = 0;

	FXP_LOCK(sc);
	if (!(if_getdrvflags(ifp) & IFF_DRV_RUNNING)) {
		FXP_UNLOCK(sc);
		return (rx_npkts);
	}

	statack = FXP_SCB_STATACK_CXTNO | FXP_SCB_STATACK_CNA |
	    FXP_SCB_STATACK_FR;
	if (cmd == POLL_AND_CHECK_STATUS) {
		uint8_t tmp;

		tmp = CSR_READ_1(sc, FXP_CSR_SCB_STATACK);
		if (tmp == 0xff || tmp == 0) {
			FXP_UNLOCK(sc);
			return (rx_npkts); /* nothing to do */
		}
		tmp &= ~statack;
		/* ack what we can */
		if (tmp != 0)
			CSR_WRITE_1(sc, FXP_CSR_SCB_STATACK, tmp);
		statack |= tmp;
	}
	rx_npkts = fxp_intr_body(sc, ifp, statack, count);
	FXP_UNLOCK(sc);
	return (rx_npkts);
}
#endif /* DEVICE_POLLING */

/*
 * Process interface interrupts.
 */
static void
fxp_intr(void *xsc)
{
	struct fxp_softc *sc = xsc;
	if_t ifp = sc->ifp;
	uint8_t statack;

	FXP_LOCK(sc);
	if (sc->suspended) {
		FXP_UNLOCK(sc);
		return;
	}

#ifdef DEVICE_POLLING
	if (if_getcapenable(ifp) & IFCAP_POLLING) {
		FXP_UNLOCK(sc);
		return;
	}
#endif
	while ((statack = CSR_READ_1(sc, FXP_CSR_SCB_STATACK)) != 0) {
		/*
		 * It should not be possible to have all bits set; the
		 * FXP_SCB_INTR_SWI bit always returns 0 on a read.  If
		 * all bits are set, this may indicate that the card has
		 * been physically ejected, so ignore it.
		 */
		if (statack == 0xff) {
			FXP_UNLOCK(sc);
			return;
		}

		/*
		 * First ACK all the interrupts in this pass.
		 */
		CSR_WRITE_1(sc, FXP_CSR_SCB_STATACK, statack);
		if ((if_getdrvflags(ifp) & IFF_DRV_RUNNING) != 0)
			fxp_intr_body(sc, ifp, statack, -1);
	}
	FXP_UNLOCK(sc);
}

static void
fxp_txeof(struct fxp_softc *sc)
{
	if_t ifp;
	struct fxp_tx *txp;

	ifp = sc->ifp;
	bus_dmamap_sync(sc->cbl_tag, sc->cbl_map,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	for (txp = sc->fxp_desc.tx_first; sc->tx_queued &&
	    (le16toh(txp->tx_cb->cb_status) & FXP_CB_STATUS_C) != 0;
	    txp = txp->tx_next) {
		if (txp->tx_mbuf != NULL) {
			bus_dmamap_sync(sc->fxp_txmtag, txp->tx_map,
			    BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->fxp_txmtag, txp->tx_map);
			m_freem(txp->tx_mbuf);
			txp->tx_mbuf = NULL;
			/* clear this to reset csum offload bits */
			txp->tx_cb->tbd[0].tb_addr = 0;
		}
		sc->tx_queued--;
		if_setdrvflagbits(ifp, 0, IFF_DRV_OACTIVE);
	}
	sc->fxp_desc.tx_first = txp;
	bus_dmamap_sync(sc->cbl_tag, sc->cbl_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	if (sc->tx_queued == 0)
		sc->watchdog_timer = 0;
}

static void
fxp_rxcsum(struct fxp_softc *sc, if_t ifp, struct mbuf *m,
    uint16_t status, int pos)
{
	struct ether_header *eh;
	struct ip *ip;
	struct udphdr *uh;
	int32_t hlen, len, pktlen, temp32;
	uint16_t csum, *opts;

	if ((sc->flags & FXP_FLAG_82559_RXCSUM) == 0) {
		if ((status & FXP_RFA_STATUS_PARSE) != 0) {
			if (status & FXP_RFDX_CS_IP_CSUM_BIT_VALID)
				m->m_pkthdr.csum_flags |= CSUM_IP_CHECKED;
			if (status & FXP_RFDX_CS_IP_CSUM_VALID)
				m->m_pkthdr.csum_flags |= CSUM_IP_VALID;
			if ((status & FXP_RFDX_CS_TCPUDP_CSUM_BIT_VALID) &&
			    (status & FXP_RFDX_CS_TCPUDP_CSUM_VALID)) {
				m->m_pkthdr.csum_flags |= CSUM_DATA_VALID |
				    CSUM_PSEUDO_HDR;
				m->m_pkthdr.csum_data = 0xffff;
			}
		}
		return;
	}

	pktlen = m->m_pkthdr.len;
	if (pktlen < sizeof(struct ether_header) + sizeof(struct ip))
		return;
	eh = mtod(m, struct ether_header *);
	if (eh->ether_type != htons(ETHERTYPE_IP))
		return;
	ip = (struct ip *)(eh + 1);
	if (ip->ip_v != IPVERSION)
		return;

	hlen = ip->ip_hl << 2;
	pktlen -= sizeof(struct ether_header);
	if (hlen < sizeof(struct ip))
		return;
	if (ntohs(ip->ip_len) < hlen)
		return;
	if (ntohs(ip->ip_len) != pktlen)
		return;
	if (ip->ip_off & htons(IP_MF | IP_OFFMASK))
		return;	/* can't handle fragmented packet */

	switch (ip->ip_p) {
	case IPPROTO_TCP:
		if (pktlen < (hlen + sizeof(struct tcphdr)))
			return;
		break;
	case IPPROTO_UDP:
		if (pktlen < (hlen + sizeof(struct udphdr)))
			return;
		uh = (struct udphdr *)((caddr_t)ip + hlen);
		if (uh->uh_sum == 0)
			return; /* no checksum */
		break;
	default:
		return;
	}
	/* Extract computed checksum. */
	csum = be16dec(mtod(m, char *) + pos);
	/* checksum fixup for IP options */
	len = hlen - sizeof(struct ip);
	if (len > 0) {
		opts = (uint16_t *)(ip + 1);
		for (; len > 0; len -= sizeof(uint16_t), opts++) {
			temp32 = csum - *opts;
			temp32 = (temp32 >> 16) + (temp32 & 65535);
			csum = temp32 & 65535;
		}
	}
	m->m_pkthdr.csum_flags |= CSUM_DATA_VALID;
	m->m_pkthdr.csum_data = csum;
}

static int
fxp_intr_body(struct fxp_softc *sc, if_t ifp, uint8_t statack,
    int count)
{
	struct mbuf *m;
	struct fxp_rx *rxp;
	struct fxp_rfa *rfa;
	int rnr = (statack & FXP_SCB_STATACK_RNR) ? 1 : 0;
	int rx_npkts;
	uint16_t status;

	rx_npkts = 0;
	FXP_LOCK_ASSERT(sc, MA_OWNED);

	if (rnr)
		sc->rnr++;
#ifdef DEVICE_POLLING
	/* Pick up a deferred RNR condition if `count' ran out last time. */
	if (sc->flags & FXP_FLAG_DEFERRED_RNR) {
		sc->flags &= ~FXP_FLAG_DEFERRED_RNR;
		rnr = 1;
	}
#endif

	/*
	 * Free any finished transmit mbuf chains.
	 *
	 * Handle the CNA event likt a CXTNO event. It used to
	 * be that this event (control unit not ready) was not
	 * encountered, but it is now with the SMPng modifications.
	 * The exact sequence of events that occur when the interface
	 * is brought up are different now, and if this event
	 * goes unhandled, the configuration/rxfilter setup sequence
	 * can stall for several seconds. The result is that no
	 * packets go out onto the wire for about 5 to 10 seconds
	 * after the interface is ifconfig'ed for the first time.
	 */
	if (statack & (FXP_SCB_STATACK_CXTNO | FXP_SCB_STATACK_CNA))
		fxp_txeof(sc);

	/*
	 * Try to start more packets transmitting.
	 */
	if (!if_sendq_empty(ifp))
		fxp_start_body(ifp);

	/*
	 * Just return if nothing happened on the receive side.
	 */
	if (!rnr && (statack & FXP_SCB_STATACK_FR) == 0)
		return (rx_npkts);

	/*
	 * Process receiver interrupts. If a no-resource (RNR)
	 * condition exists, get whatever packets we can and
	 * re-start the receiver.
	 *
	 * When using polling, we do not process the list to completion,
	 * so when we get an RNR interrupt we must defer the restart
	 * until we hit the last buffer with the C bit set.
	 * If we run out of cycles and rfa_headm has the C bit set,
	 * record the pending RNR in the FXP_FLAG_DEFERRED_RNR flag so
	 * that the info will be used in the subsequent polling cycle.
	 */
	for (;;) {
		rxp = sc->fxp_desc.rx_head;
		m = rxp->rx_mbuf;
		rfa = (struct fxp_rfa *)(m->m_ext.ext_buf +
		    RFA_ALIGNMENT_FUDGE);
		bus_dmamap_sync(sc->fxp_rxmtag, rxp->rx_map,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

#ifdef DEVICE_POLLING /* loop at most count times if count >=0 */
		if (count >= 0 && count-- == 0) {
			if (rnr) {
				/* Defer RNR processing until the next time. */
				sc->flags |= FXP_FLAG_DEFERRED_RNR;
				rnr = 0;
			}
			break;
		}
#endif /* DEVICE_POLLING */

		status = le16toh(rfa->rfa_status);
		if ((status & FXP_RFA_STATUS_C) == 0)
			break;

		if ((status & FXP_RFA_STATUS_RNR) != 0)
			rnr++;
		/*
		 * Advance head forward.
		 */
		sc->fxp_desc.rx_head = rxp->rx_next;

		/*
		 * Add a new buffer to the receive chain.
		 * If this fails, the old buffer is recycled
		 * instead.
		 */
		if (fxp_new_rfabuf(sc, rxp) == 0) {
			int total_len;

			/*
			 * Fetch packet length (the top 2 bits of
			 * actual_size are flags set by the controller
			 * upon completion), and drop the packet in case
			 * of bogus length or CRC errors.
			 */
			total_len = le16toh(rfa->actual_size) & 0x3fff;
			if ((sc->flags & FXP_FLAG_82559_RXCSUM) != 0 &&
			    (if_getcapenable(ifp) & IFCAP_RXCSUM) != 0) {
				/* Adjust for appended checksum bytes. */
				total_len -= 2;
			}
			if (total_len < (int)sizeof(struct ether_header) ||
			    total_len > (MCLBYTES - RFA_ALIGNMENT_FUDGE -
			    sc->rfa_size) ||
			    status & (FXP_RFA_STATUS_CRC |
			    FXP_RFA_STATUS_ALIGN | FXP_RFA_STATUS_OVERRUN)) {
				m_freem(m);
				fxp_add_rfabuf(sc, rxp);
				continue;
			}

			m->m_pkthdr.len = m->m_len = total_len;
			if_setrcvif(m, ifp);

                        /* Do IP checksum checking. */
			if ((if_getcapenable(ifp) & IFCAP_RXCSUM) != 0)
				fxp_rxcsum(sc, ifp, m, status, total_len);
			if ((if_getcapenable(ifp) & IFCAP_VLAN_HWTAGGING) != 0 &&
			    (status & FXP_RFA_STATUS_VLAN) != 0) {
				m->m_pkthdr.ether_vtag =
				    ntohs(rfa->rfax_vlan_id);
				m->m_flags |= M_VLANTAG;
			}
			/*
			 * Drop locks before calling if_input() since it
			 * may re-enter fxp_start() in the netisr case.
			 * This would result in a lock reversal.  Better
			 * performance might be obtained by chaining all
			 * packets received, dropping the lock, and then
			 * calling if_input() on each one.
			 */
			FXP_UNLOCK(sc);
			if_input(ifp, m);
			FXP_LOCK(sc);
			rx_npkts++;
			if ((if_getdrvflags(ifp) & IFF_DRV_RUNNING) == 0)
				return (rx_npkts);
		} else {
			/* Reuse RFA and loaded DMA map. */
			if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
			fxp_discard_rfabuf(sc, rxp);
		}
		fxp_add_rfabuf(sc, rxp);
	}
	if (rnr) {
		fxp_scb_wait(sc);
		CSR_WRITE_4(sc, FXP_CSR_SCB_GENERAL,
		    sc->fxp_desc.rx_head->rx_addr);
		fxp_scb_cmd(sc, FXP_SCB_COMMAND_RU_START);
	}
	return (rx_npkts);
}

static void
fxp_update_stats(struct fxp_softc *sc)
{
	if_t ifp = sc->ifp;
	struct fxp_stats *sp = sc->fxp_stats;
	struct fxp_hwstats *hsp;
	uint32_t *status;

	FXP_LOCK_ASSERT(sc, MA_OWNED);

	bus_dmamap_sync(sc->fxp_stag, sc->fxp_smap,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	/* Update statistical counters. */
	if (sc->revision >= FXP_REV_82559_A0)
		status = &sp->completion_status;
	else if (sc->revision >= FXP_REV_82558_A4)
		status = (uint32_t *)&sp->tx_tco;
	else
		status = &sp->tx_pause;
	if (*status == htole32(FXP_STATS_DR_COMPLETE)) {
		hsp = &sc->fxp_hwstats;
		hsp->tx_good += le32toh(sp->tx_good);
		hsp->tx_maxcols += le32toh(sp->tx_maxcols);
		hsp->tx_latecols += le32toh(sp->tx_latecols);
		hsp->tx_underruns += le32toh(sp->tx_underruns);
		hsp->tx_lostcrs += le32toh(sp->tx_lostcrs);
		hsp->tx_deffered += le32toh(sp->tx_deffered);
		hsp->tx_single_collisions += le32toh(sp->tx_single_collisions);
		hsp->tx_multiple_collisions +=
		    le32toh(sp->tx_multiple_collisions);
		hsp->tx_total_collisions += le32toh(sp->tx_total_collisions);
		hsp->rx_good += le32toh(sp->rx_good);
		hsp->rx_crc_errors += le32toh(sp->rx_crc_errors);
		hsp->rx_alignment_errors += le32toh(sp->rx_alignment_errors);
		hsp->rx_rnr_errors += le32toh(sp->rx_rnr_errors);
		hsp->rx_overrun_errors += le32toh(sp->rx_overrun_errors);
		hsp->rx_cdt_errors += le32toh(sp->rx_cdt_errors);
		hsp->rx_shortframes += le32toh(sp->rx_shortframes);
		hsp->tx_pause += le32toh(sp->tx_pause);
		hsp->rx_pause += le32toh(sp->rx_pause);
		hsp->rx_controls += le32toh(sp->rx_controls);
		hsp->tx_tco += le16toh(sp->tx_tco);
		hsp->rx_tco += le16toh(sp->rx_tco);

		if_inc_counter(ifp, IFCOUNTER_OPACKETS, le32toh(sp->tx_good));
		if_inc_counter(ifp, IFCOUNTER_COLLISIONS,
		    le32toh(sp->tx_total_collisions));
		if (sp->rx_good) {
			if_inc_counter(ifp, IFCOUNTER_IPACKETS,
			    le32toh(sp->rx_good));
			sc->rx_idle_secs = 0;
		} else if (sc->flags & FXP_FLAG_RXBUG) {
			/*
			 * Receiver's been idle for another second.
			 */
			sc->rx_idle_secs++;
		}
		if_inc_counter(ifp, IFCOUNTER_IERRORS,
		    le32toh(sp->rx_crc_errors) +
		    le32toh(sp->rx_alignment_errors) +
		    le32toh(sp->rx_rnr_errors) +
		    le32toh(sp->rx_overrun_errors));
		/*
		 * If any transmit underruns occurred, bump up the transmit
		 * threshold by another 512 bytes (64 * 8).
		 */
		if (sp->tx_underruns) {
			if_inc_counter(ifp, IFCOUNTER_OERRORS,
			    le32toh(sp->tx_underruns));
			if (tx_threshold < 192)
				tx_threshold += 64;
		}
		*status = 0;
		bus_dmamap_sync(sc->fxp_stag, sc->fxp_smap,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	}
}

/*
 * Update packet in/out/collision statistics. The i82557 doesn't
 * allow you to access these counters without doing a fairly
 * expensive DMA to get _all_ of the statistics it maintains, so
 * we do this operation here only once per second. The statistics
 * counters in the kernel are updated from the previous dump-stats
 * DMA and then a new dump-stats DMA is started. The on-chip
 * counters are zeroed when the DMA completes. If we can't start
 * the DMA immediately, we don't wait - we just prepare to read
 * them again next time.
 */
static void
fxp_tick(void *xsc)
{
	struct fxp_softc *sc = xsc;
	if_t ifp = sc->ifp;

	FXP_LOCK_ASSERT(sc, MA_OWNED);

	/* Update statistical counters. */
	fxp_update_stats(sc);

	/*
	 * Release any xmit buffers that have completed DMA. This isn't
	 * strictly necessary to do here, but it's advantagous for mbufs
	 * with external storage to be released in a timely manner rather
	 * than being defered for a potentially long time. This limits
	 * the delay to a maximum of one second.
	 */
	fxp_txeof(sc);

	/*
	 * If we haven't received any packets in FXP_MAC_RX_IDLE seconds,
	 * then assume the receiver has locked up and attempt to clear
	 * the condition by reprogramming the multicast filter. This is
	 * a work-around for a bug in the 82557 where the receiver locks
	 * up if it gets certain types of garbage in the synchronization
	 * bits prior to the packet header. This bug is supposed to only
	 * occur in 10Mbps mode, but has been seen to occur in 100Mbps
	 * mode as well (perhaps due to a 10/100 speed transition).
	 */
	if (sc->rx_idle_secs > FXP_MAX_RX_IDLE) {
		sc->rx_idle_secs = 0;
		if ((if_getdrvflags(ifp) & IFF_DRV_RUNNING) != 0) {
			if_setdrvflagbits(ifp, 0, IFF_DRV_RUNNING);
			fxp_init_body(sc, 1);
		}
		return;
	}
	/*
	 * If there is no pending command, start another stats
	 * dump. Otherwise punt for now.
	 */
	if (CSR_READ_1(sc, FXP_CSR_SCB_COMMAND) == 0) {
		/*
		 * Start another stats dump.
		 */
		fxp_scb_cmd(sc, FXP_SCB_COMMAND_CU_DUMPRESET);
	}
	if (sc->miibus != NULL)
		mii_tick(device_get_softc(sc->miibus));

	/*
	 * Check that chip hasn't hung.
	 */
	fxp_watchdog(sc);

	/*
	 * Schedule another timeout one second from now.
	 */
	callout_reset(&sc->stat_ch, hz, fxp_tick, sc);
}

/*
 * Stop the interface. Cancels the statistics updater and resets
 * the interface.
 */
static void
fxp_stop(struct fxp_softc *sc)
{
	if_t ifp = sc->ifp;
	struct fxp_tx *txp;
	int i;

	if_setdrvflagbits(ifp, 0, (IFF_DRV_RUNNING | IFF_DRV_OACTIVE));
	sc->watchdog_timer = 0;

	/*
	 * Cancel stats updater.
	 */
	callout_stop(&sc->stat_ch);

	/*
	 * Preserve PCI configuration, configure, IA/multicast
	 * setup and put RU and CU into idle state.
	 */
	CSR_WRITE_4(sc, FXP_CSR_PORT, FXP_PORT_SELECTIVE_RESET);
	DELAY(50);
	/* Disable interrupts. */
	CSR_WRITE_1(sc, FXP_CSR_SCB_INTRCNTL, FXP_SCB_INTR_DISABLE);

	fxp_update_stats(sc);

	/*
	 * Release any xmit buffers.
	 */
	txp = sc->fxp_desc.tx_list;
	for (i = 0; i < FXP_NTXCB; i++) {
		if (txp[i].tx_mbuf != NULL) {
			bus_dmamap_sync(sc->fxp_txmtag, txp[i].tx_map,
			    BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->fxp_txmtag, txp[i].tx_map);
			m_freem(txp[i].tx_mbuf);
			txp[i].tx_mbuf = NULL;
			/* clear this to reset csum offload bits */
			txp[i].tx_cb->tbd[0].tb_addr = 0;
		}
	}
	bus_dmamap_sync(sc->cbl_tag, sc->cbl_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	sc->tx_queued = 0;
}

/*
 * Watchdog/transmission transmit timeout handler. Called when a
 * transmission is started on the interface, but no interrupt is
 * received before the timeout. This usually indicates that the
 * card has wedged for some reason.
 */
static void
fxp_watchdog(struct fxp_softc *sc)
{
	if_t ifp = sc->ifp;

	FXP_LOCK_ASSERT(sc, MA_OWNED);

	if (sc->watchdog_timer == 0 || --sc->watchdog_timer)
		return;

	device_printf(sc->dev, "device timeout\n");
	if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);

	if_setdrvflagbits(ifp, 0, IFF_DRV_RUNNING);
	fxp_init_body(sc, 1);
}

/*
 * Acquire locks and then call the real initialization function.  This
 * is necessary because ether_ioctl() calls if_init() and this would
 * result in mutex recursion if the mutex was held.
 */
static void
fxp_init(void *xsc)
{
	struct fxp_softc *sc = xsc;

	FXP_LOCK(sc);
	fxp_init_body(sc, 1);
	FXP_UNLOCK(sc);
}

/*
 * Perform device initialization. This routine must be called with the
 * softc lock held.
 */
static void
fxp_init_body(struct fxp_softc *sc, int setmedia)
{
	if_t ifp = sc->ifp;
	struct mii_data *mii;
	struct fxp_cb_config *cbp;
	struct fxp_cb_ias *cb_ias;
	struct fxp_cb_tx *tcbp;
	struct fxp_tx *txp;
	int i, prm;

	FXP_LOCK_ASSERT(sc, MA_OWNED);

	if (if_getdrvflags(ifp) & IFF_DRV_RUNNING)
		return;

	/*
	 * Cancel any pending I/O
	 */
	fxp_stop(sc);

	/*
	 * Issue software reset, which also unloads the microcode.
	 */
	sc->flags &= ~FXP_FLAG_UCODE;
	CSR_WRITE_4(sc, FXP_CSR_PORT, FXP_PORT_SOFTWARE_RESET);
	DELAY(50);

	prm = (if_getflags(ifp) & IFF_PROMISC) ? 1 : 0;

	/*
	 * Initialize base of CBL and RFA memory. Loading with zero
	 * sets it up for regular linear addressing.
	 */
	CSR_WRITE_4(sc, FXP_CSR_SCB_GENERAL, 0);
	fxp_scb_cmd(sc, FXP_SCB_COMMAND_CU_BASE);

	fxp_scb_wait(sc);
	fxp_scb_cmd(sc, FXP_SCB_COMMAND_RU_BASE);

	/*
	 * Initialize base of dump-stats buffer.
	 */
	fxp_scb_wait(sc);
	bzero(sc->fxp_stats, sizeof(struct fxp_stats));
	bus_dmamap_sync(sc->fxp_stag, sc->fxp_smap,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	CSR_WRITE_4(sc, FXP_CSR_SCB_GENERAL, sc->stats_addr);
	fxp_scb_cmd(sc, FXP_SCB_COMMAND_CU_DUMP_ADR);

	/*
	 * Attempt to load microcode if requested.
	 * For ICH based controllers do not load microcode.
	 */
	if (sc->ident->ich == 0) {
		if (if_getflags(ifp) & IFF_LINK0 &&
		    (sc->flags & FXP_FLAG_UCODE) == 0)
			fxp_load_ucode(sc);
	}

	/*
	 * Set IFF_ALLMULTI status. It's needed in configure action
	 * command.
	 */
	fxp_mc_addrs(sc);

	/*
	 * We temporarily use memory that contains the TxCB list to
	 * construct the config CB. The TxCB list memory is rebuilt
	 * later.
	 */
	cbp = (struct fxp_cb_config *)sc->fxp_desc.cbl_list;

	/*
	 * This bcopy is kind of disgusting, but there are a bunch of must be
	 * zero and must be one bits in this structure and this is the easiest
	 * way to initialize them all to proper values.
	 */
	bcopy(fxp_cb_config_template, cbp, sizeof(fxp_cb_config_template));

	cbp->cb_status =	0;
	cbp->cb_command =	htole16(FXP_CB_COMMAND_CONFIG |
	    FXP_CB_COMMAND_EL);
	cbp->link_addr =	0xffffffff;	/* (no) next command */
	cbp->byte_count =	sc->flags & FXP_FLAG_EXT_RFA ? 32 : 22;
	cbp->rx_fifo_limit =	8;	/* rx fifo threshold (32 bytes) */
	cbp->tx_fifo_limit =	0;	/* tx fifo threshold (0 bytes) */
	cbp->adaptive_ifs =	0;	/* (no) adaptive interframe spacing */
	cbp->mwi_enable =	sc->flags & FXP_FLAG_MWI_ENABLE ? 1 : 0;
	cbp->type_enable =	0;	/* actually reserved */
	cbp->read_align_en =	sc->flags & FXP_FLAG_READ_ALIGN ? 1 : 0;
	cbp->end_wr_on_cl =	sc->flags & FXP_FLAG_WRITE_ALIGN ? 1 : 0;
	cbp->rx_dma_bytecount =	0;	/* (no) rx DMA max */
	cbp->tx_dma_bytecount =	0;	/* (no) tx DMA max */
	cbp->dma_mbce =		0;	/* (disable) dma max counters */
	cbp->late_scb =		0;	/* (don't) defer SCB update */
	cbp->direct_dma_dis =	1;	/* disable direct rcv dma mode */
	cbp->tno_int_or_tco_en =0;	/* (disable) tx not okay interrupt */
	cbp->ci_int =		1;	/* interrupt on CU idle */
	cbp->ext_txcb_dis = 	sc->flags & FXP_FLAG_EXT_TXCB ? 0 : 1;
	cbp->ext_stats_dis = 	1;	/* disable extended counters */
	cbp->keep_overrun_rx = 	0;	/* don't pass overrun frames to host */
	cbp->save_bf =		sc->flags & FXP_FLAG_SAVE_BAD ? 1 : prm;
	cbp->disc_short_rx =	!prm;	/* discard short packets */
	cbp->underrun_retry =	1;	/* retry mode (once) on DMA underrun */
	cbp->two_frames =	0;	/* do not limit FIFO to 2 frames */
	cbp->dyn_tbd =		sc->flags & FXP_FLAG_EXT_RFA ? 1 : 0;
	cbp->ext_rfa =		sc->flags & FXP_FLAG_EXT_RFA ? 1 : 0;
	cbp->mediatype =	sc->flags & FXP_FLAG_SERIAL_MEDIA ? 0 : 1;
	cbp->csma_dis =		0;	/* (don't) disable link */
	cbp->tcp_udp_cksum =	((sc->flags & FXP_FLAG_82559_RXCSUM) != 0 &&
	    (if_getcapenable(ifp) & IFCAP_RXCSUM) != 0) ? 1 : 0;
	cbp->vlan_tco =		0;	/* (don't) enable vlan wakeup */
	cbp->link_wake_en =	0;	/* (don't) assert PME# on link change */
	cbp->arp_wake_en =	0;	/* (don't) assert PME# on arp */
	cbp->mc_wake_en =	0;	/* (don't) enable PME# on mcmatch */
	cbp->nsai =		1;	/* (don't) disable source addr insert */
	cbp->preamble_length =	2;	/* (7 byte) preamble */
	cbp->loopback =		0;	/* (don't) loopback */
	cbp->linear_priority =	0;	/* (normal CSMA/CD operation) */
	cbp->linear_pri_mode =	0;	/* (wait after xmit only) */
	cbp->interfrm_spacing =	6;	/* (96 bits of) interframe spacing */
	cbp->promiscuous =	prm;	/* promiscuous mode */
	cbp->bcast_disable =	0;	/* (don't) disable broadcasts */
	cbp->wait_after_win =	0;	/* (don't) enable modified backoff alg*/
	cbp->ignore_ul =	0;	/* consider U/L bit in IA matching */
	cbp->crc16_en =		0;	/* (don't) enable crc-16 algorithm */
	cbp->crscdt =		sc->flags & FXP_FLAG_SERIAL_MEDIA ? 1 : 0;

	cbp->stripping =	!prm;	/* truncate rx packet to byte count */
	cbp->padding =		1;	/* (do) pad short tx packets */
	cbp->rcv_crc_xfer =	0;	/* (don't) xfer CRC to host */
	cbp->long_rx_en =	sc->flags & FXP_FLAG_LONG_PKT_EN ? 1 : 0;
	cbp->ia_wake_en =	0;	/* (don't) wake up on address match */
	cbp->magic_pkt_dis =	sc->flags & FXP_FLAG_WOL ? 0 : 1;
	cbp->force_fdx =	0;	/* (don't) force full duplex */
	cbp->fdx_pin_en =	1;	/* (enable) FDX# pin */
	cbp->multi_ia =		0;	/* (don't) accept multiple IAs */
	cbp->mc_all =		if_getflags(ifp) & IFF_ALLMULTI ? 1 : prm;
	cbp->gamla_rx =		sc->flags & FXP_FLAG_EXT_RFA ? 1 : 0;
	cbp->vlan_strip_en =	((sc->flags & FXP_FLAG_EXT_RFA) != 0 &&
	    (if_getcapenable(ifp) & IFCAP_VLAN_HWTAGGING) != 0) ? 1 : 0;

	if (sc->revision == FXP_REV_82557) {
		/*
		 * The 82557 has no hardware flow control, the values
		 * below are the defaults for the chip.
		 */
		cbp->fc_delay_lsb =	0;
		cbp->fc_delay_msb =	0x40;
		cbp->pri_fc_thresh =	3;
		cbp->tx_fc_dis =	0;
		cbp->rx_fc_restop =	0;
		cbp->rx_fc_restart =	0;
		cbp->fc_filter =	0;
		cbp->pri_fc_loc =	1;
	} else {
		/* Set pause RX FIFO threshold to 1KB. */
		CSR_WRITE_1(sc, FXP_CSR_FC_THRESH, 1);
		/* Set pause time. */
		cbp->fc_delay_lsb =	0xff;
		cbp->fc_delay_msb =	0xff;
		cbp->pri_fc_thresh =	3;
		mii = device_get_softc(sc->miibus);
		if ((IFM_OPTIONS(mii->mii_media_active) &
		    IFM_ETH_TXPAUSE) != 0)
			/* enable transmit FC */
			cbp->tx_fc_dis = 0;
		else
			/* disable transmit FC */
			cbp->tx_fc_dis = 1;
		if ((IFM_OPTIONS(mii->mii_media_active) &
		    IFM_ETH_RXPAUSE) != 0) {
			/* enable FC restart/restop frames */
			cbp->rx_fc_restart = 1;
			cbp->rx_fc_restop = 1;
		} else {
			/* disable FC restart/restop frames */
			cbp->rx_fc_restart = 0;
			cbp->rx_fc_restop = 0;
		}
		cbp->fc_filter =	!prm;	/* drop FC frames to host */
		cbp->pri_fc_loc =	1;	/* FC pri location (byte31) */
	}

	/* Enable 82558 and 82559 extended statistics functionality. */
	if (sc->revision >= FXP_REV_82558_A4) {
		if (sc->revision >= FXP_REV_82559_A0) {
			/*
			 * Extend configuration table size to 32
			 * to include TCO configuration.
			 */
			cbp->byte_count = 32;
			cbp->ext_stats_dis = 1;
			/* Enable TCO stats. */
			cbp->tno_int_or_tco_en = 1;
			cbp->gamla_rx = 1;
		} else
			cbp->ext_stats_dis = 0;
	}

	/*
	 * Start the config command/DMA.
	 */
	fxp_scb_wait(sc);
	bus_dmamap_sync(sc->cbl_tag, sc->cbl_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	CSR_WRITE_4(sc, FXP_CSR_SCB_GENERAL, sc->fxp_desc.cbl_addr);
	fxp_scb_cmd(sc, FXP_SCB_COMMAND_CU_START);
	/* ...and wait for it to complete. */
	fxp_dma_wait(sc, &cbp->cb_status, sc->cbl_tag, sc->cbl_map);

	/*
	 * Now initialize the station address. Temporarily use the TxCB
	 * memory area like we did above for the config CB.
	 */
	cb_ias = (struct fxp_cb_ias *)sc->fxp_desc.cbl_list;
	cb_ias->cb_status = 0;
	cb_ias->cb_command = htole16(FXP_CB_COMMAND_IAS | FXP_CB_COMMAND_EL);
	cb_ias->link_addr = 0xffffffff;
	bcopy(if_getlladdr(sc->ifp), cb_ias->macaddr, ETHER_ADDR_LEN);

	/*
	 * Start the IAS (Individual Address Setup) command/DMA.
	 */
	fxp_scb_wait(sc);
	bus_dmamap_sync(sc->cbl_tag, sc->cbl_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	CSR_WRITE_4(sc, FXP_CSR_SCB_GENERAL, sc->fxp_desc.cbl_addr);
	fxp_scb_cmd(sc, FXP_SCB_COMMAND_CU_START);
	/* ...and wait for it to complete. */
	fxp_dma_wait(sc, &cb_ias->cb_status, sc->cbl_tag, sc->cbl_map);

	/*
	 * Initialize the multicast address list.
	 */
	fxp_mc_setup(sc);

	/*
	 * Initialize transmit control block (TxCB) list.
	 */
	txp = sc->fxp_desc.tx_list;
	tcbp = sc->fxp_desc.cbl_list;
	bzero(tcbp, FXP_TXCB_SZ);
	for (i = 0; i < FXP_NTXCB; i++) {
		txp[i].tx_mbuf = NULL;
		tcbp[i].cb_status = htole16(FXP_CB_STATUS_C | FXP_CB_STATUS_OK);
		tcbp[i].cb_command = htole16(FXP_CB_COMMAND_NOP);
		tcbp[i].link_addr = htole32(sc->fxp_desc.cbl_addr +
		    (((i + 1) & FXP_TXCB_MASK) * sizeof(struct fxp_cb_tx)));
		if (sc->flags & FXP_FLAG_EXT_TXCB)
			tcbp[i].tbd_array_addr =
			    htole32(FXP_TXCB_DMA_ADDR(sc, &tcbp[i].tbd[2]));
		else
			tcbp[i].tbd_array_addr =
			    htole32(FXP_TXCB_DMA_ADDR(sc, &tcbp[i].tbd[0]));
		txp[i].tx_next = &txp[(i + 1) & FXP_TXCB_MASK];
	}
	/*
	 * Set the suspend flag on the first TxCB and start the control
	 * unit. It will execute the NOP and then suspend.
	 */
	tcbp->cb_command = htole16(FXP_CB_COMMAND_NOP | FXP_CB_COMMAND_S);
	bus_dmamap_sync(sc->cbl_tag, sc->cbl_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	sc->fxp_desc.tx_first = sc->fxp_desc.tx_last = txp;
	sc->tx_queued = 1;

	fxp_scb_wait(sc);
	CSR_WRITE_4(sc, FXP_CSR_SCB_GENERAL, sc->fxp_desc.cbl_addr);
	fxp_scb_cmd(sc, FXP_SCB_COMMAND_CU_START);

	/*
	 * Initialize receiver buffer area - RFA.
	 */
	fxp_scb_wait(sc);
	CSR_WRITE_4(sc, FXP_CSR_SCB_GENERAL, sc->fxp_desc.rx_head->rx_addr);
	fxp_scb_cmd(sc, FXP_SCB_COMMAND_RU_START);

	if (sc->miibus != NULL && setmedia != 0)
		mii_mediachg(device_get_softc(sc->miibus));

	if_setdrvflagbits(ifp, IFF_DRV_RUNNING, IFF_DRV_OACTIVE);

	/*
	 * Enable interrupts.
	 */
#ifdef DEVICE_POLLING
	/*
	 * ... but only do that if we are not polling. And because (presumably)
	 * the default is interrupts on, we need to disable them explicitly!
	 */
	if (if_getcapenable(ifp) & IFCAP_POLLING )
		CSR_WRITE_1(sc, FXP_CSR_SCB_INTRCNTL, FXP_SCB_INTR_DISABLE);
	else
#endif /* DEVICE_POLLING */
	CSR_WRITE_1(sc, FXP_CSR_SCB_INTRCNTL, 0);

	/*
	 * Start stats updater.
	 */
	callout_reset(&sc->stat_ch, hz, fxp_tick, sc);
}

static int
fxp_serial_ifmedia_upd(if_t ifp)
{

	return (0);
}

static void
fxp_serial_ifmedia_sts(if_t ifp, struct ifmediareq *ifmr)
{

	ifmr->ifm_active = IFM_ETHER|IFM_MANUAL;
}

/*
 * Change media according to request.
 */
static int
fxp_ifmedia_upd(if_t ifp)
{
	struct fxp_softc *sc = if_getsoftc(ifp);
	struct mii_data *mii;
	struct mii_softc	*miisc;

	mii = device_get_softc(sc->miibus);
	FXP_LOCK(sc);
	LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
		PHY_RESET(miisc);
	mii_mediachg(mii);
	FXP_UNLOCK(sc);
	return (0);
}

/*
 * Notify the world which media we're using.
 */
static void
fxp_ifmedia_sts(if_t ifp, struct ifmediareq *ifmr)
{
	struct fxp_softc *sc = if_getsoftc(ifp);
	struct mii_data *mii;

	mii = device_get_softc(sc->miibus);
	FXP_LOCK(sc);
	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
	FXP_UNLOCK(sc);
}

/*
 * Add a buffer to the end of the RFA buffer list.
 * Return 0 if successful, 1 for failure. A failure results in
 * reusing the RFA buffer.
 * The RFA struct is stuck at the beginning of mbuf cluster and the
 * data pointer is fixed up to point just past it.
 */
static int
fxp_new_rfabuf(struct fxp_softc *sc, struct fxp_rx *rxp)
{
	struct mbuf *m;
	struct fxp_rfa *rfa;
	bus_dmamap_t tmp_map;
	int error;

	m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
	if (m == NULL)
		return (ENOBUFS);

	/*
	 * Move the data pointer up so that the incoming data packet
	 * will be 32-bit aligned.
	 */
	m->m_data += RFA_ALIGNMENT_FUDGE;

	/*
	 * Get a pointer to the base of the mbuf cluster and move
	 * data start past it.
	 */
	rfa = mtod(m, struct fxp_rfa *);
	m->m_data += sc->rfa_size;
	rfa->size = htole16(MCLBYTES - sc->rfa_size - RFA_ALIGNMENT_FUDGE);

	rfa->rfa_status = 0;
	rfa->rfa_control = htole16(FXP_RFA_CONTROL_EL);
	rfa->actual_size = 0;
	m->m_len = m->m_pkthdr.len = MCLBYTES - RFA_ALIGNMENT_FUDGE -
	    sc->rfa_size;

	/*
	 * Initialize the rest of the RFA.  Note that since the RFA
	 * is misaligned, we cannot store values directly.  We're thus
	 * using the le32enc() function which handles endianness and
	 * is also alignment-safe.
	 */
	le32enc(&rfa->link_addr, 0xffffffff);
	le32enc(&rfa->rbd_addr, 0xffffffff);

	/* Map the RFA into DMA memory. */
	error = bus_dmamap_load(sc->fxp_rxmtag, sc->spare_map, rfa,
	    MCLBYTES - RFA_ALIGNMENT_FUDGE, fxp_dma_map_addr,
	    &rxp->rx_addr, BUS_DMA_NOWAIT);
	if (error) {
		m_freem(m);
		return (error);
	}

	if (rxp->rx_mbuf != NULL)
		bus_dmamap_unload(sc->fxp_rxmtag, rxp->rx_map);
	tmp_map = sc->spare_map;
	sc->spare_map = rxp->rx_map;
	rxp->rx_map = tmp_map;
	rxp->rx_mbuf = m;

	bus_dmamap_sync(sc->fxp_rxmtag, rxp->rx_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	return (0);
}

static void
fxp_add_rfabuf(struct fxp_softc *sc, struct fxp_rx *rxp)
{
	struct fxp_rfa *p_rfa;
	struct fxp_rx *p_rx;

	/*
	 * If there are other buffers already on the list, attach this
	 * one to the end by fixing up the tail to point to this one.
	 */
	if (sc->fxp_desc.rx_head != NULL) {
		p_rx = sc->fxp_desc.rx_tail;
		p_rfa = (struct fxp_rfa *)
		    (p_rx->rx_mbuf->m_ext.ext_buf + RFA_ALIGNMENT_FUDGE);
		p_rx->rx_next = rxp;
		le32enc(&p_rfa->link_addr, rxp->rx_addr);
		p_rfa->rfa_control = 0;
		bus_dmamap_sync(sc->fxp_rxmtag, p_rx->rx_map,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	} else {
		rxp->rx_next = NULL;
		sc->fxp_desc.rx_head = rxp;
	}
	sc->fxp_desc.rx_tail = rxp;
}

static void
fxp_discard_rfabuf(struct fxp_softc *sc, struct fxp_rx *rxp)
{
	struct mbuf *m;
	struct fxp_rfa *rfa;

	m = rxp->rx_mbuf;
	m->m_data = m->m_ext.ext_buf;
	/*
	 * Move the data pointer up so that the incoming data packet
	 * will be 32-bit aligned.
	 */
	m->m_data += RFA_ALIGNMENT_FUDGE;

	/*
	 * Get a pointer to the base of the mbuf cluster and move
	 * data start past it.
	 */
	rfa = mtod(m, struct fxp_rfa *);
	m->m_data += sc->rfa_size;
	rfa->size = htole16(MCLBYTES - sc->rfa_size - RFA_ALIGNMENT_FUDGE);

	rfa->rfa_status = 0;
	rfa->rfa_control = htole16(FXP_RFA_CONTROL_EL);
	rfa->actual_size = 0;

	/*
	 * Initialize the rest of the RFA.  Note that since the RFA
	 * is misaligned, we cannot store values directly.  We're thus
	 * using the le32enc() function which handles endianness and
	 * is also alignment-safe.
	 */
	le32enc(&rfa->link_addr, 0xffffffff);
	le32enc(&rfa->rbd_addr, 0xffffffff);

	bus_dmamap_sync(sc->fxp_rxmtag, rxp->rx_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

static int
fxp_miibus_readreg(device_t dev, int phy, int reg)
{
	struct fxp_softc *sc = device_get_softc(dev);
	int count = 10000;
	int value;

	CSR_WRITE_4(sc, FXP_CSR_MDICONTROL,
	    (FXP_MDI_READ << 26) | (reg << 16) | (phy << 21));

	while (((value = CSR_READ_4(sc, FXP_CSR_MDICONTROL)) & 0x10000000) == 0
	    && count--)
		DELAY(10);

	if (count <= 0)
		device_printf(dev, "fxp_miibus_readreg: timed out\n");

	return (value & 0xffff);
}

static int
fxp_miibus_writereg(device_t dev, int phy, int reg, int value)
{
	struct fxp_softc *sc = device_get_softc(dev);
	int count = 10000;

	CSR_WRITE_4(sc, FXP_CSR_MDICONTROL,
	    (FXP_MDI_WRITE << 26) | (reg << 16) | (phy << 21) |
	    (value & 0xffff));

	while ((CSR_READ_4(sc, FXP_CSR_MDICONTROL) & 0x10000000) == 0 &&
	    count--)
		DELAY(10);

	if (count <= 0)
		device_printf(dev, "fxp_miibus_writereg: timed out\n");
	return (0);
}

static void
fxp_miibus_statchg(device_t dev)
{
	struct fxp_softc *sc;
	struct mii_data *mii;
	if_t ifp;

	sc = device_get_softc(dev);
	mii = device_get_softc(sc->miibus);
	ifp = sc->ifp;
	if (mii == NULL || ifp == (void *)NULL ||
	    (if_getdrvflags(ifp) & IFF_DRV_RUNNING) == 0 ||
	    (mii->mii_media_status & (IFM_AVALID | IFM_ACTIVE)) !=
	    (IFM_AVALID | IFM_ACTIVE))
		return;

	if (IFM_SUBTYPE(mii->mii_media_active) == IFM_10_T &&
	    sc->flags & FXP_FLAG_CU_RESUME_BUG)
		sc->cu_resume_bug = 1;
	else
		sc->cu_resume_bug = 0;
	/*
	 * Call fxp_init_body in order to adjust the flow control settings.
	 * Note that the 82557 doesn't support hardware flow control.
	 */
	if (sc->revision == FXP_REV_82557)
		return;
	if_setdrvflagbits(ifp, 0, IFF_DRV_RUNNING);
	fxp_init_body(sc, 0);
}

static int
fxp_ioctl(if_t ifp, u_long command, caddr_t data)
{
	struct fxp_softc *sc = if_getsoftc(ifp);
	struct ifreq *ifr = (struct ifreq *)data;
	struct mii_data *mii;
	int flag, mask, error = 0, reinit;

	switch (command) {
	case SIOCSIFFLAGS:
		FXP_LOCK(sc);
		/*
		 * If interface is marked up and not running, then start it.
		 * If it is marked down and running, stop it.
		 * XXX If it's up then re-initialize it. This is so flags
		 * such as IFF_PROMISC are handled.
		 */
		if (if_getflags(ifp) & IFF_UP) {
			if (((if_getdrvflags(ifp) & IFF_DRV_RUNNING) != 0) &&
			    ((if_getflags(ifp) ^ sc->if_flags) &
			    (IFF_PROMISC | IFF_ALLMULTI | IFF_LINK0)) != 0) {
				if_setdrvflagbits(ifp, 0, IFF_DRV_RUNNING);
				fxp_init_body(sc, 0);
			} else if ((if_getdrvflags(ifp) & IFF_DRV_RUNNING) == 0)
				fxp_init_body(sc, 1);
		} else {
			if ((if_getdrvflags(ifp) & IFF_DRV_RUNNING) != 0)
				fxp_stop(sc);
		}
		sc->if_flags = if_getflags(ifp);
		FXP_UNLOCK(sc);
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		FXP_LOCK(sc);
		if ((if_getdrvflags(ifp) & IFF_DRV_RUNNING) != 0) {
			if_setdrvflagbits(ifp, 0, IFF_DRV_RUNNING);
			fxp_init_body(sc, 0);
		}
		FXP_UNLOCK(sc);
		break;

	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		if (sc->miibus != NULL) {
			mii = device_get_softc(sc->miibus);
                        error = ifmedia_ioctl(ifp, ifr,
                            &mii->mii_media, command);
		} else {
                        error = ifmedia_ioctl(ifp, ifr, &sc->sc_media, command);
		}
		break;

	case SIOCSIFCAP:
		reinit = 0;
		mask = if_getcapenable(ifp) ^ ifr->ifr_reqcap;
#ifdef DEVICE_POLLING
		if (mask & IFCAP_POLLING) {
			if (ifr->ifr_reqcap & IFCAP_POLLING) {
				error = ether_poll_register(fxp_poll, ifp);
				if (error)
					return(error);
				FXP_LOCK(sc);
				CSR_WRITE_1(sc, FXP_CSR_SCB_INTRCNTL,
				    FXP_SCB_INTR_DISABLE);
				if_setcapenablebit(ifp, IFCAP_POLLING, 0);
				FXP_UNLOCK(sc);
			} else {
				error = ether_poll_deregister(ifp);
				/* Enable interrupts in any case */
				FXP_LOCK(sc);
				CSR_WRITE_1(sc, FXP_CSR_SCB_INTRCNTL, 0);
				if_setcapenablebit(ifp, 0, IFCAP_POLLING);
				FXP_UNLOCK(sc);
			}
		}
#endif
		FXP_LOCK(sc);
		if ((mask & IFCAP_TXCSUM) != 0 &&
		    (if_getcapabilities(ifp) & IFCAP_TXCSUM) != 0) {
			if_togglecapenable(ifp, IFCAP_TXCSUM);
			if ((if_getcapenable(ifp) & IFCAP_TXCSUM) != 0)
				if_sethwassistbits(ifp, FXP_CSUM_FEATURES, 0);
			else
				if_sethwassistbits(ifp, 0, FXP_CSUM_FEATURES);
		}
		if ((mask & IFCAP_RXCSUM) != 0 &&
		    (if_getcapabilities(ifp) & IFCAP_RXCSUM) != 0) {
			if_togglecapenable(ifp, IFCAP_RXCSUM);
			if ((sc->flags & FXP_FLAG_82559_RXCSUM) != 0)
				reinit++;
		}
		if ((mask & IFCAP_TSO4) != 0 &&
		    (if_getcapabilities(ifp) & IFCAP_TSO4) != 0) {
			if_togglecapenable(ifp, IFCAP_TSO4);
			if ((if_getcapenable(ifp) & IFCAP_TSO4) != 0)
				if_sethwassistbits(ifp, CSUM_TSO, 0);
			else
				if_sethwassistbits(ifp, 0, CSUM_TSO);
		}
		if ((mask & IFCAP_WOL_MAGIC) != 0 &&
		    (if_getcapabilities(ifp) & IFCAP_WOL_MAGIC) != 0)
			if_togglecapenable(ifp, IFCAP_WOL_MAGIC);
		if ((mask & IFCAP_VLAN_MTU) != 0 &&
		    (if_getcapabilities(ifp) & IFCAP_VLAN_MTU) != 0) {
			if_togglecapenable(ifp, IFCAP_VLAN_MTU);
			if (sc->revision != FXP_REV_82557)
				flag = FXP_FLAG_LONG_PKT_EN;
			else /* a hack to get long frames on the old chip */
				flag = FXP_FLAG_SAVE_BAD;
			sc->flags ^= flag;
			if (if_getflags(ifp) & IFF_UP)
				reinit++;
		}
		if ((mask & IFCAP_VLAN_HWCSUM) != 0 &&
		    (if_getcapabilities(ifp) & IFCAP_VLAN_HWCSUM) != 0)
			if_togglecapenable(ifp, IFCAP_VLAN_HWCSUM);
		if ((mask & IFCAP_VLAN_HWTSO) != 0 &&
		    (if_getcapabilities(ifp) & IFCAP_VLAN_HWTSO) != 0)
			if_togglecapenable(ifp, IFCAP_VLAN_HWTSO);
		if ((mask & IFCAP_VLAN_HWTAGGING) != 0 &&
		    (if_getcapabilities(ifp) & IFCAP_VLAN_HWTAGGING) != 0) {
			if_togglecapenable(ifp, IFCAP_VLAN_HWTAGGING);
			if ((if_getcapenable(ifp) & IFCAP_VLAN_HWTAGGING) == 0)
				if_setcapenablebit(ifp, 0, IFCAP_VLAN_HWTSO |
				    IFCAP_VLAN_HWCSUM);
			reinit++;
		}
		if (reinit > 0 &&
		    (if_getdrvflags(ifp) & IFF_DRV_RUNNING) != 0) {
			if_setdrvflagbits(ifp, 0, IFF_DRV_RUNNING);
			fxp_init_body(sc, 0);
		}
		FXP_UNLOCK(sc);
		if_vlancap(ifp);
		break;

	default:
		error = ether_ioctl(ifp, command, data);
	}
	return (error);
}

/*
 * Fill in the multicast address list and return number of entries.
 */
static int
fxp_mc_addrs(struct fxp_softc *sc)
{
	struct fxp_cb_mcs *mcsp = sc->mcsp;
	if_t ifp = sc->ifp;
	int nmcasts = 0;

	if ((if_getflags(ifp) & IFF_ALLMULTI) == 0) {
		if_maddr_rlock(ifp);
		if_setupmultiaddr(ifp, mcsp->mc_addr, &nmcasts, MAXMCADDR);
		if (nmcasts >= MAXMCADDR) {
			if_setflagbits(ifp, IFF_ALLMULTI, 0);
			nmcasts = 0;
		}
		if_maddr_runlock(ifp);
	}
	mcsp->mc_cnt = htole16(nmcasts * ETHER_ADDR_LEN);
	return (nmcasts);
}

/*
 * Program the multicast filter.
 *
 * We have an artificial restriction that the multicast setup command
 * must be the first command in the chain, so we take steps to ensure
 * this. By requiring this, it allows us to keep up the performance of
 * the pre-initialized command ring (esp. link pointers) by not actually
 * inserting the mcsetup command in the ring - i.e. its link pointer
 * points to the TxCB ring, but the mcsetup descriptor itself is not part
 * of it. We then can do 'CU_START' on the mcsetup descriptor and have it
 * lead into the regular TxCB ring when it completes.
 */
static void
fxp_mc_setup(struct fxp_softc *sc)
{
	struct fxp_cb_mcs *mcsp;
	int count;

	FXP_LOCK_ASSERT(sc, MA_OWNED);

	mcsp = sc->mcsp;
	mcsp->cb_status = 0;
	mcsp->cb_command = htole16(FXP_CB_COMMAND_MCAS | FXP_CB_COMMAND_EL);
	mcsp->link_addr = 0xffffffff;
	fxp_mc_addrs(sc);

	/*
	 * Wait until command unit is idle. This should never be the
	 * case when nothing is queued, but make sure anyway.
	 */
	count = 100;
	while ((CSR_READ_1(sc, FXP_CSR_SCB_RUSCUS) >> 6) !=
	    FXP_SCB_CUS_IDLE && --count)
		DELAY(10);
	if (count == 0) {
		device_printf(sc->dev, "command queue timeout\n");
		return;
	}

	/*
	 * Start the multicast setup command.
	 */
	fxp_scb_wait(sc);
	bus_dmamap_sync(sc->mcs_tag, sc->mcs_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	CSR_WRITE_4(sc, FXP_CSR_SCB_GENERAL, sc->mcs_addr);
	fxp_scb_cmd(sc, FXP_SCB_COMMAND_CU_START);
	/* ...and wait for it to complete. */
	fxp_dma_wait(sc, &mcsp->cb_status, sc->mcs_tag, sc->mcs_map);
}

static uint32_t fxp_ucode_d101a[] = D101_A_RCVBUNDLE_UCODE;
static uint32_t fxp_ucode_d101b0[] = D101_B0_RCVBUNDLE_UCODE;
static uint32_t fxp_ucode_d101ma[] = D101M_B_RCVBUNDLE_UCODE;
static uint32_t fxp_ucode_d101s[] = D101S_RCVBUNDLE_UCODE;
static uint32_t fxp_ucode_d102[] = D102_B_RCVBUNDLE_UCODE;
static uint32_t fxp_ucode_d102c[] = D102_C_RCVBUNDLE_UCODE;
static uint32_t fxp_ucode_d102e[] = D102_E_RCVBUNDLE_UCODE;

#define UCODE(x)	x, sizeof(x)/sizeof(uint32_t)

static const struct ucode {
	uint32_t	revision;
	uint32_t	*ucode;
	int		length;
	u_short		int_delay_offset;
	u_short		bundle_max_offset;
} ucode_table[] = {
	{ FXP_REV_82558_A4, UCODE(fxp_ucode_d101a), D101_CPUSAVER_DWORD, 0 },
	{ FXP_REV_82558_B0, UCODE(fxp_ucode_d101b0), D101_CPUSAVER_DWORD, 0 },
	{ FXP_REV_82559_A0, UCODE(fxp_ucode_d101ma),
	    D101M_CPUSAVER_DWORD, D101M_CPUSAVER_BUNDLE_MAX_DWORD },
	{ FXP_REV_82559S_A, UCODE(fxp_ucode_d101s),
	    D101S_CPUSAVER_DWORD, D101S_CPUSAVER_BUNDLE_MAX_DWORD },
	{ FXP_REV_82550, UCODE(fxp_ucode_d102),
	    D102_B_CPUSAVER_DWORD, D102_B_CPUSAVER_BUNDLE_MAX_DWORD },
	{ FXP_REV_82550_C, UCODE(fxp_ucode_d102c),
	    D102_C_CPUSAVER_DWORD, D102_C_CPUSAVER_BUNDLE_MAX_DWORD },
	{ FXP_REV_82551_F, UCODE(fxp_ucode_d102e),
	    D102_E_CPUSAVER_DWORD, D102_E_CPUSAVER_BUNDLE_MAX_DWORD },
	{ FXP_REV_82551_10, UCODE(fxp_ucode_d102e),
	    D102_E_CPUSAVER_DWORD, D102_E_CPUSAVER_BUNDLE_MAX_DWORD },
	{ 0, NULL, 0, 0, 0 }
};

static void
fxp_load_ucode(struct fxp_softc *sc)
{
	const struct ucode *uc;
	struct fxp_cb_ucode *cbp;
	int i;

	if (sc->flags & FXP_FLAG_NO_UCODE)
		return;

	for (uc = ucode_table; uc->ucode != NULL; uc++)
		if (sc->revision == uc->revision)
			break;
	if (uc->ucode == NULL)
		return;
	cbp = (struct fxp_cb_ucode *)sc->fxp_desc.cbl_list;
	cbp->cb_status = 0;
	cbp->cb_command = htole16(FXP_CB_COMMAND_UCODE | FXP_CB_COMMAND_EL);
	cbp->link_addr = 0xffffffff;    	/* (no) next command */
	for (i = 0; i < uc->length; i++)
		cbp->ucode[i] = htole32(uc->ucode[i]);
	if (uc->int_delay_offset)
		*(uint16_t *)&cbp->ucode[uc->int_delay_offset] =
		    htole16(sc->tunable_int_delay + sc->tunable_int_delay / 2);
	if (uc->bundle_max_offset)
		*(uint16_t *)&cbp->ucode[uc->bundle_max_offset] =
		    htole16(sc->tunable_bundle_max);
	/*
	 * Download the ucode to the chip.
	 */
	fxp_scb_wait(sc);
	bus_dmamap_sync(sc->cbl_tag, sc->cbl_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	CSR_WRITE_4(sc, FXP_CSR_SCB_GENERAL, sc->fxp_desc.cbl_addr);
	fxp_scb_cmd(sc, FXP_SCB_COMMAND_CU_START);
	/* ...and wait for it to complete. */
	fxp_dma_wait(sc, &cbp->cb_status, sc->cbl_tag, sc->cbl_map);
	device_printf(sc->dev,
	    "Microcode loaded, int_delay: %d usec  bundle_max: %d\n",
	    sc->tunable_int_delay,
	    uc->bundle_max_offset == 0 ? 0 : sc->tunable_bundle_max);
	sc->flags |= FXP_FLAG_UCODE;
	bzero(cbp, FXP_TXCB_SZ);
}

#define FXP_SYSCTL_STAT_ADD(c, h, n, p, d)	\
	SYSCTL_ADD_UINT(c, h, OID_AUTO, n, CTLFLAG_RD, p, 0, d)

static void
fxp_sysctl_node(struct fxp_softc *sc)
{
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid_list *child, *parent;
	struct sysctl_oid *tree;
	struct fxp_hwstats *hsp;

	ctx = device_get_sysctl_ctx(sc->dev);
	child = SYSCTL_CHILDREN(device_get_sysctl_tree(sc->dev));

	SYSCTL_ADD_PROC(ctx, child,
	    OID_AUTO, "int_delay", CTLTYPE_INT | CTLFLAG_RW,
	    &sc->tunable_int_delay, 0, sysctl_hw_fxp_int_delay, "I",
	    "FXP driver receive interrupt microcode bundling delay");
	SYSCTL_ADD_PROC(ctx, child,
	    OID_AUTO, "bundle_max", CTLTYPE_INT | CTLFLAG_RW,
	    &sc->tunable_bundle_max, 0, sysctl_hw_fxp_bundle_max, "I",
	    "FXP driver receive interrupt microcode bundle size limit");
	SYSCTL_ADD_INT(ctx, child,OID_AUTO, "rnr", CTLFLAG_RD, &sc->rnr, 0,
	    "FXP RNR events");

	/*
	 * Pull in device tunables.
	 */
	sc->tunable_int_delay = TUNABLE_INT_DELAY;
	sc->tunable_bundle_max = TUNABLE_BUNDLE_MAX;
	(void) resource_int_value(device_get_name(sc->dev),
	    device_get_unit(sc->dev), "int_delay", &sc->tunable_int_delay);
	(void) resource_int_value(device_get_name(sc->dev),
	    device_get_unit(sc->dev), "bundle_max", &sc->tunable_bundle_max);
	sc->rnr = 0;

	hsp = &sc->fxp_hwstats;
	tree = SYSCTL_ADD_NODE(ctx, child, OID_AUTO, "stats", CTLFLAG_RD,
	    NULL, "FXP statistics");
	parent = SYSCTL_CHILDREN(tree);

	/* Rx MAC statistics. */
	tree = SYSCTL_ADD_NODE(ctx, parent, OID_AUTO, "rx", CTLFLAG_RD,
	    NULL, "Rx MAC statistics");
	child = SYSCTL_CHILDREN(tree);
	FXP_SYSCTL_STAT_ADD(ctx, child, "good_frames",
	    &hsp->rx_good, "Good frames");
	FXP_SYSCTL_STAT_ADD(ctx, child, "crc_errors",
	    &hsp->rx_crc_errors, "CRC errors");
	FXP_SYSCTL_STAT_ADD(ctx, child, "alignment_errors",
	    &hsp->rx_alignment_errors, "Alignment errors");
	FXP_SYSCTL_STAT_ADD(ctx, child, "rnr_errors",
	    &hsp->rx_rnr_errors, "RNR errors");
	FXP_SYSCTL_STAT_ADD(ctx, child, "overrun_errors",
	    &hsp->rx_overrun_errors, "Overrun errors");
	FXP_SYSCTL_STAT_ADD(ctx, child, "cdt_errors",
	    &hsp->rx_cdt_errors, "Collision detect errors");
	FXP_SYSCTL_STAT_ADD(ctx, child, "shortframes",
	    &hsp->rx_shortframes, "Short frame errors");
	if (sc->revision >= FXP_REV_82558_A4) {
		FXP_SYSCTL_STAT_ADD(ctx, child, "pause",
		    &hsp->rx_pause, "Pause frames");
		FXP_SYSCTL_STAT_ADD(ctx, child, "controls",
		    &hsp->rx_controls, "Unsupported control frames");
	}
	if (sc->revision >= FXP_REV_82559_A0)
		FXP_SYSCTL_STAT_ADD(ctx, child, "tco",
		    &hsp->rx_tco, "TCO frames");

	/* Tx MAC statistics. */
	tree = SYSCTL_ADD_NODE(ctx, parent, OID_AUTO, "tx", CTLFLAG_RD,
	    NULL, "Tx MAC statistics");
	child = SYSCTL_CHILDREN(tree);
	FXP_SYSCTL_STAT_ADD(ctx, child, "good_frames",
	    &hsp->tx_good, "Good frames");
	FXP_SYSCTL_STAT_ADD(ctx, child, "maxcols",
	    &hsp->tx_maxcols, "Maximum collisions errors");
	FXP_SYSCTL_STAT_ADD(ctx, child, "latecols",
	    &hsp->tx_latecols, "Late collisions errors");
	FXP_SYSCTL_STAT_ADD(ctx, child, "underruns",
	    &hsp->tx_underruns, "Underrun errors");
	FXP_SYSCTL_STAT_ADD(ctx, child, "lostcrs",
	    &hsp->tx_lostcrs, "Lost carrier sense");
	FXP_SYSCTL_STAT_ADD(ctx, child, "deffered",
	    &hsp->tx_deffered, "Deferred");
	FXP_SYSCTL_STAT_ADD(ctx, child, "single_collisions",
	    &hsp->tx_single_collisions, "Single collisions");
	FXP_SYSCTL_STAT_ADD(ctx, child, "multiple_collisions",
	    &hsp->tx_multiple_collisions, "Multiple collisions");
	FXP_SYSCTL_STAT_ADD(ctx, child, "total_collisions",
	    &hsp->tx_total_collisions, "Total collisions");
	if (sc->revision >= FXP_REV_82558_A4)
		FXP_SYSCTL_STAT_ADD(ctx, child, "pause",
		    &hsp->tx_pause, "Pause frames");
	if (sc->revision >= FXP_REV_82559_A0)
		FXP_SYSCTL_STAT_ADD(ctx, child, "tco",
		    &hsp->tx_tco, "TCO frames");
}

#undef FXP_SYSCTL_STAT_ADD

static int
sysctl_int_range(SYSCTL_HANDLER_ARGS, int low, int high)
{
	int error, value;

	value = *(int *)arg1;
	error = sysctl_handle_int(oidp, &value, 0, req);
	if (error || !req->newptr)
		return (error);
	if (value < low || value > high)
		return (EINVAL);
	*(int *)arg1 = value;
	return (0);
}

/*
 * Interrupt delay is expressed in microseconds, a multiplier is used
 * to convert this to the appropriate clock ticks before using.
 */
static int
sysctl_hw_fxp_int_delay(SYSCTL_HANDLER_ARGS)
{

	return (sysctl_int_range(oidp, arg1, arg2, req, 300, 3000));
}

static int
sysctl_hw_fxp_bundle_max(SYSCTL_HANDLER_ARGS)
{

	return (sysctl_int_range(oidp, arg1, arg2, req, 1, 0xffff));
}
