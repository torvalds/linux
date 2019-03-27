/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1999, 2000 Matthew R. Green
 * Copyright (c) 2001 - 2003 by Thomas Moestl <tmm@FreeBSD.org>
 * Copyright (c) 2005 - 2011 by Marius Strobl <marius@FreeBSD.org>
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
 *
 *	from: NetBSD: psycho.c,v 1.39 2001/10/07 20:30:41 eeh Exp
 *	from: FreeBSD: psycho.c 183152 2008-09-18 19:45:22Z marius
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Driver for `Schizo' Fireplane/Safari to PCI 2.1, `Tomatillo' JBus to
 * PCI 2.2 and `XMITS' Fireplane/Safari to PCI-X bridges
 */

#include "opt_ofw_pci.h"
#include "opt_schizo.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/pcpu.h>
#include <sys/rman.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/timetc.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/openfirm.h>

#include <machine/bus.h>
#include <machine/bus_common.h>
#include <machine/bus_private.h>
#include <machine/iommureg.h>
#include <machine/iommuvar.h>
#include <machine/resource.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcib_private.h>

#include <sparc64/pci/ofw_pci.h>
#include <sparc64/pci/schizoreg.h>
#include <sparc64/pci/schizovar.h>

#include "pcib_if.h"

static const struct schizo_desc *schizo_get_desc(device_t);
static void schizo_set_intr(struct schizo_softc *, u_int, u_int,
    driver_filter_t);
static void schizo_dmamap_sync(bus_dma_tag_t dt, bus_dmamap_t map,
    bus_dmasync_op_t op);
static void ichip_dmamap_sync(bus_dma_tag_t dt, bus_dmamap_t map,
    bus_dmasync_op_t op);
static void schizo_intr_enable(void *);
static void schizo_intr_disable(void *);
static void schizo_intr_assign(void *);
static void schizo_intr_clear(void *);
static int schizo_intr_register(struct schizo_softc *sc, u_int ino);
static int schizo_get_intrmap(struct schizo_softc *, u_int,
    bus_addr_t *, bus_addr_t *);
static timecounter_get_t schizo_get_timecount;

/* Interrupt handlers */
static driver_filter_t schizo_pci_bus;
static driver_filter_t schizo_ue;
static driver_filter_t schizo_ce;
static driver_filter_t schizo_host_bus;
static driver_filter_t schizo_cdma;

/* IOMMU support */
static void schizo_iommu_init(struct schizo_softc *, int, uint32_t);

/*
 * Methods
 */
static device_probe_t schizo_probe;
static device_attach_t schizo_attach;
static bus_setup_intr_t schizo_setup_intr;
static bus_alloc_resource_t schizo_alloc_resource;
static pcib_maxslots_t schizo_maxslots;
static pcib_read_config_t schizo_read_config;
static pcib_write_config_t schizo_write_config;
static pcib_route_interrupt_t schizo_route_interrupt;
static ofw_pci_setup_device_t schizo_setup_device;

static device_method_t schizo_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		schizo_probe),
	DEVMETHOD(device_attach,	schizo_attach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),

	/* Bus interface */
	DEVMETHOD(bus_read_ivar,	ofw_pci_read_ivar),
	DEVMETHOD(bus_setup_intr,	schizo_setup_intr),
	DEVMETHOD(bus_teardown_intr,	bus_generic_teardown_intr),
	DEVMETHOD(bus_alloc_resource,	schizo_alloc_resource),
	DEVMETHOD(bus_activate_resource, ofw_pci_activate_resource),
	DEVMETHOD(bus_deactivate_resource, bus_generic_deactivate_resource),
	DEVMETHOD(bus_adjust_resource,	ofw_pci_adjust_resource),
	DEVMETHOD(bus_release_resource,	bus_generic_release_resource),
	DEVMETHOD(bus_get_dma_tag,	ofw_pci_get_dma_tag),

	/* pcib interface */
	DEVMETHOD(pcib_maxslots,	schizo_maxslots),
	DEVMETHOD(pcib_read_config,	schizo_read_config),
	DEVMETHOD(pcib_write_config,	schizo_write_config),
	DEVMETHOD(pcib_route_interrupt,	schizo_route_interrupt),
	DEVMETHOD(pcib_request_feature,	pcib_request_feature_allow),

	/* ofw_bus interface */
	DEVMETHOD(ofw_bus_get_node,	ofw_pci_get_node),

	/* ofw_pci interface */
	DEVMETHOD(ofw_pci_setup_device,	schizo_setup_device),

	DEVMETHOD_END
};

static devclass_t schizo_devclass;

DEFINE_CLASS_0(pcib, schizo_driver, schizo_methods,
    sizeof(struct schizo_softc));
EARLY_DRIVER_MODULE(schizo, nexus, schizo_driver, schizo_devclass, 0, 0,
    BUS_PASS_BUS);

static SLIST_HEAD(, schizo_softc) schizo_softcs =
    SLIST_HEAD_INITIALIZER(schizo_softcs);

static const struct intr_controller schizo_ic = {
	schizo_intr_enable,
	schizo_intr_disable,
	schizo_intr_assign,
	schizo_intr_clear
};

struct schizo_icarg {
	struct schizo_softc	*sica_sc;
	bus_addr_t		sica_map;
	bus_addr_t		sica_clr;
};

#define	SCHIZO_CDMA_TIMEOUT	1	/* 1 second per try */
#define	SCHIZO_CDMA_TRIES	15
#define	SCHIZO_PERF_CNT_QLTY	100

#define	SCHIZO_SPC_BARRIER(spc, sc, offs, len, flags)			\
	bus_barrier((sc)->sc_mem_res[(spc)], (offs), (len), (flags))
#define	SCHIZO_SPC_READ_8(spc, sc, offs)				\
	bus_read_8((sc)->sc_mem_res[(spc)], (offs))
#define	SCHIZO_SPC_WRITE_8(spc, sc, offs, v)				\
	bus_write_8((sc)->sc_mem_res[(spc)], (offs), (v))

#ifndef SCHIZO_DEBUG
#define	SCHIZO_SPC_SET(spc, sc, offs, reg, v)				\
	SCHIZO_SPC_WRITE_8((spc), (sc), (offs), (v))
#else
#define	SCHIZO_SPC_SET(spc, sc, offs, reg, v) do {			\
	device_printf((sc)->sc_dev, reg " 0x%016llx -> 0x%016llx\n",	\
	    (unsigned long long)SCHIZO_SPC_READ_8((spc), (sc), (offs)),	\
	    (unsigned long long)(v));					\
	SCHIZO_SPC_WRITE_8((spc), (sc), (offs), (v));			\
	} while (0)
#endif

#define	SCHIZO_PCI_READ_8(sc, offs)					\
	SCHIZO_SPC_READ_8(STX_PCI, (sc), (offs))
#define	SCHIZO_PCI_WRITE_8(sc, offs, v)					\
	SCHIZO_SPC_WRITE_8(STX_PCI, (sc), (offs), (v))
#define	SCHIZO_CTRL_READ_8(sc, offs)					\
	SCHIZO_SPC_READ_8(STX_CTRL, (sc), (offs))
#define	SCHIZO_CTRL_WRITE_8(sc, offs, v)				\
	SCHIZO_SPC_WRITE_8(STX_CTRL, (sc), (offs), (v))
#define	SCHIZO_PCICFG_READ_8(sc, offs)					\
	SCHIZO_SPC_READ_8(STX_PCICFG, (sc), (offs))
#define	SCHIZO_PCICFG_WRITE_8(sc, offs, v)				\
	SCHIZO_SPC_WRITE_8(STX_PCICFG, (sc), (offs), (v))
#define	SCHIZO_ICON_READ_8(sc, offs)					\
	SCHIZO_SPC_READ_8(STX_ICON, (sc), (offs))
#define	SCHIZO_ICON_WRITE_8(sc, offs, v)				\
	SCHIZO_SPC_WRITE_8(STX_ICON, (sc), (offs), (v))

#define	SCHIZO_PCI_SET(sc, offs, v)					\
	SCHIZO_SPC_SET(STX_PCI, (sc), (offs), # offs, (v))
#define	SCHIZO_CTRL_SET(sc, offs, v)					\
	SCHIZO_SPC_SET(STX_CTRL, (sc), (offs), # offs, (v))

struct schizo_desc {
	const char	*sd_string;
	int		sd_mode;
	const char	*sd_name;
};

static const struct schizo_desc schizo_compats[] = {
	{ "pci108e,8001",	SCHIZO_MODE_SCZ,	"Schizo" },
#if 0
	{ "pci108e,8002",	SCHIZO_MODE_XMS,	"XMITS" },
#endif
	{ "pci108e,a801",	SCHIZO_MODE_TOM,	"Tomatillo" },
	{ NULL,			0,			NULL }
};

static const struct schizo_desc *
schizo_get_desc(device_t dev)
{
	const struct schizo_desc *desc;
	const char *compat;

	compat = ofw_bus_get_compat(dev);
	if (compat == NULL)
		return (NULL);
	for (desc = schizo_compats; desc->sd_string != NULL; desc++)
		if (strcmp(desc->sd_string, compat) == 0)
			return (desc);
	return (NULL);
}

static int
schizo_probe(device_t dev)
{
	const char *dtype;

	dtype = ofw_bus_get_type(dev);
	if (dtype != NULL && strcmp(dtype, OFW_TYPE_PCI) == 0 &&
	    schizo_get_desc(dev) != NULL) {
		device_set_desc(dev, "Sun Host-PCI bridge");
		return (0);
	}
	return (ENXIO);
}

static int
schizo_attach(device_t dev)
{
	const struct schizo_desc *desc;
	struct schizo_softc *asc, *sc, *osc;
	struct timecounter *tc;
	bus_dma_tag_t dmat;
	uint64_t ino_bitmap, reg;
	phandle_t node;
	uint32_t prop, prop_array[2];
	int i, j, mode, rid, tsbsize;

	sc = device_get_softc(dev);
	node = ofw_bus_get_node(dev);
	desc = schizo_get_desc(dev);
	mode = desc->sd_mode;

	sc->sc_dev = dev;
	sc->sc_mode = mode;
	sc->sc_flags = 0;

	/*
	 * The Schizo has three register banks:
	 * (0) per-PBM PCI configuration and status registers, but for bus B
	 *     shared with the UPA64s interrupt mapping register banks
	 * (1) shared Schizo controller configuration and status registers
	 * (2) per-PBM PCI configuration space
	 *
	 * The Tomatillo has four register banks:
	 * (0) per-PBM PCI configuration and status registers
	 * (1) per-PBM Tomatillo controller configuration registers, but on
	 *     machines having the `jbusppm' device shared with its Estar
	 *     register bank for bus A
	 * (2) per-PBM PCI configuration space
	 * (3) per-PBM interrupt concentrator registers
	 */
	sc->sc_half = (bus_get_resource_start(dev, SYS_RES_MEMORY, STX_PCI) >>
	    20) & 1;
	for (i = 0; i < (mode == SCHIZO_MODE_SCZ ? SCZ_NREG : TOM_NREG);
	    i++) {
		rid = i;
		sc->sc_mem_res[i] = bus_alloc_resource_any(dev,
		    SYS_RES_MEMORY, &rid,
		    (((mode == SCHIZO_MODE_SCZ && ((sc->sc_half == 1 &&
		    i == STX_PCI) || i == STX_CTRL)) ||
		    (mode == SCHIZO_MODE_TOM && sc->sc_half == 0 &&
		    i == STX_CTRL)) ? RF_SHAREABLE : 0) | RF_ACTIVE);
		if (sc->sc_mem_res[i] == NULL)
			panic("%s: could not allocate register bank %d",
			    __func__, i);
	}

	/*
	 * Match other Schizos that are already configured against
	 * the controller base physical address.  This will be the
	 * same for a pair of devices that share register space.
	 */
	osc = NULL;
	SLIST_FOREACH(asc, &schizo_softcs, sc_link) {
		if (rman_get_start(asc->sc_mem_res[STX_CTRL]) ==
		    rman_get_start(sc->sc_mem_res[STX_CTRL])) {
			/* Found partner. */
			osc = asc;
			break;
		}
	}
	if (osc == NULL) {
		sc->sc_mtx = malloc(sizeof(*sc->sc_mtx), M_DEVBUF,
		    M_NOWAIT | M_ZERO);
		if (sc->sc_mtx == NULL)
			panic("%s: could not malloc mutex", __func__);
		mtx_init(sc->sc_mtx, "pcib_mtx", NULL, MTX_SPIN);
	} else {
		if (sc->sc_mode != SCHIZO_MODE_SCZ)
			panic("%s: no partner expected", __func__);
		if (mtx_initialized(osc->sc_mtx) == 0)
			panic("%s: mutex not initialized", __func__);
		sc->sc_mtx = osc->sc_mtx;
	}
	SLIST_INSERT_HEAD(&schizo_softcs, sc, sc_link);

	if (OF_getprop(node, "portid", &sc->sc_ign, sizeof(sc->sc_ign)) == -1)
		panic("%s: could not determine IGN", __func__);
	if (OF_getprop(node, "version#", &sc->sc_ver, sizeof(sc->sc_ver)) ==
	    -1)
		panic("%s: could not determine version", __func__);
	if (mode == SCHIZO_MODE_XMS && OF_getprop(node, "module-revision#",
	    &sc->sc_mrev, sizeof(sc->sc_mrev)) == -1)
		panic("%s: could not determine module-revision", __func__);
	if (OF_getprop(node, "clock-frequency", &prop, sizeof(prop)) == -1)
		prop = 33000000;

	if (mode == SCHIZO_MODE_XMS && (SCHIZO_PCI_READ_8(sc, STX_PCI_CTRL) &
	    XMS_PCI_CTRL_X_MODE) != 0) {
		if (sc->sc_mrev < 1)
			panic("PCI-X mode unsupported");
		sc->sc_flags |= SCHIZO_FLAGS_XMODE;
	}

	device_printf(dev, "%s, version %d, ", desc->sd_name, sc->sc_ver);
	if (mode == SCHIZO_MODE_XMS)
		printf("module-revision %d, ", sc->sc_mrev);
	printf("IGN %#x, bus %c, PCI%s mode, %dMHz\n", sc->sc_ign,
	    'A' + sc->sc_half, (sc->sc_flags & SCHIZO_FLAGS_XMODE) != 0 ?
	    "-X" : "", prop / 1000 / 1000);

	/* Set up the PCI interrupt retry timer. */
	SCHIZO_PCI_SET(sc, STX_PCI_INTR_RETRY_TIM, 5);

	/* Set up the PCI control register. */
	reg = SCHIZO_PCI_READ_8(sc, STX_PCI_CTRL);
	reg &= ~(TOM_PCI_CTRL_DTO_IEN | STX_PCI_CTRL_ARB_PARK |
	    STX_PCI_CTRL_ARB_MASK);
	reg |= STX_PCI_CTRL_MMU_IEN | STX_PCI_CTRL_SBH_IEN |
	    STX_PCI_CTRL_ERR_IEN;
	if (OF_getproplen(node, "no-bus-parking") < 0)
		reg |= STX_PCI_CTRL_ARB_PARK;
	if (mode == SCHIZO_MODE_XMS && sc->sc_mrev == 1)
		reg |= XMS_PCI_CTRL_XMITS10_ARB_MASK;
	else
		reg |= STX_PCI_CTRL_ARB_MASK;
	if (mode == SCHIZO_MODE_TOM) {
		reg |= TOM_PCI_CTRL_PRM | TOM_PCI_CTRL_PRO | TOM_PCI_CTRL_PRL;
		if (sc->sc_ver <= 1)	/* revision <= 2.0 */
			reg |= TOM_PCI_CTRL_DTO_IEN;
		else
			reg |= STX_PCI_CTRL_PTO;
	} else if (mode == SCHIZO_MODE_XMS) {
		SCHIZO_PCI_SET(sc, XMS_PCI_PARITY_DETECT, 0x3fff);
		SCHIZO_PCI_SET(sc, XMS_PCI_UPPER_RETRY_COUNTER, 0x3e8);
		reg |= XMS_PCI_CTRL_X_ERRINT_EN;
	}
	SCHIZO_PCI_SET(sc, STX_PCI_CTRL, reg);

	/* Set up the PCI diagnostic register. */
	reg = SCHIZO_PCI_READ_8(sc, STX_PCI_DIAG);
	reg &= ~(SCZ_PCI_DIAG_RTRYARB_DIS | STX_PCI_DIAG_RETRY_DIS |
	    STX_PCI_DIAG_INTRSYNC_DIS);
	SCHIZO_PCI_SET(sc, STX_PCI_DIAG, reg);

	/*
	 * Enable DMA write parity error interrupts of version >= 7 (i.e.
	 * revision >= 2.5) Schizo and XMITS (enabling it on XMITS < 3.0 has
	 * no effect though).
	 */
	if ((mode == SCHIZO_MODE_SCZ && sc->sc_ver >= 7) ||
	    mode == SCHIZO_MODE_XMS) {
		reg = SCHIZO_PCI_READ_8(sc, SX_PCI_CFG_ICD);
		reg |= SX_PCI_CFG_ICD_DMAW_PERR_IEN;
		SCHIZO_PCI_SET(sc, SX_PCI_CFG_ICD, reg);
	}

	/*
	 * On Tomatillo clear the I/O prefetch lengths (workaround for a
	 * Jalapeno bug).
	 */
	if (mode == SCHIZO_MODE_TOM)
		SCHIZO_PCI_SET(sc, TOM_PCI_IOC_CSR, TOM_PCI_IOC_PW |
		    (1 << TOM_PCI_IOC_PREF_OFF_SHIFT) | TOM_PCI_IOC_CPRM |
		    TOM_PCI_IOC_CPRO | TOM_PCI_IOC_CPRL);

	/*
	 * Hunt through all the interrupt mapping regs and register
	 * the interrupt controller for our interrupt vectors.  We do
	 * this early in order to be able to catch stray interrupts.
	 * This is complicated by the fact that a pair of Schizo PBMs
	 * shares one IGN.
	 */
	i = OF_getprop(node, "ino-bitmap", (void *)prop_array,
	    sizeof(prop_array));
	if (i != -1)
		ino_bitmap = ((uint64_t)prop_array[1] << 32) | prop_array[0];
	else {
		/*
		 * If the ino-bitmap property is missing, just provide the
		 * default set of interrupts for this controller and let
		 * schizo_setup_intr() take care of child interrupts.
		 */
		if (sc->sc_half == 0)
			ino_bitmap = (1ULL << STX_UE_INO) |
			    (1ULL << STX_CE_INO) |
			    (1ULL << STX_PCIERR_A_INO) |
			    (1ULL << STX_BUS_INO);
		else
			ino_bitmap = 1ULL << STX_PCIERR_B_INO;
	}
	for (i = 0; i <= STX_MAX_INO; i++) {
		if ((ino_bitmap & (1ULL << i)) == 0)
			continue;
		if (i == STX_FB0_INO || i == STX_FB1_INO)
			/* Leave for upa(4). */
			continue;
		j = schizo_intr_register(sc, i);
		if (j != 0)
			device_printf(dev, "could not register interrupt "
			    "controller for INO %d (%d)\n", i, j);
	}

	/*
	 * Setup Safari/JBus performance counter 0 in bus cycle counting
	 * mode as timecounter.  Unfortunately, this is broken with at
	 * least the version 4 Tomatillos found in Fire V120 and Blade
	 * 1500, which apparently actually count some different event at
	 * ~0.5 and 3MHz respectively instead (also when running in full
	 * power mode).  Besides, one counter seems to be shared by a
	 * "pair" of Tomatillos, too.
	 */
	if (sc->sc_half == 0) {
		SCHIZO_CTRL_SET(sc, STX_CTRL_PERF,
		    (STX_CTRL_PERF_DIS << STX_CTRL_PERF_CNT1_SHIFT) |
		    (STX_CTRL_PERF_BUSCYC << STX_CTRL_PERF_CNT0_SHIFT));
		tc = malloc(sizeof(*tc), M_DEVBUF, M_NOWAIT | M_ZERO);
		if (tc == NULL)
			panic("%s: could not malloc timecounter", __func__);
		tc->tc_get_timecount = schizo_get_timecount;
		tc->tc_counter_mask = STX_CTRL_PERF_CNT_MASK;
		if (OF_getprop(OF_peer(0), "clock-frequency", &prop,
		    sizeof(prop)) == -1)
			panic("%s: could not determine clock frequency",
			    __func__);
		tc->tc_frequency = prop;
		tc->tc_name = strdup(device_get_nameunit(dev), M_DEVBUF);
		if (mode == SCHIZO_MODE_SCZ)
			tc->tc_quality = SCHIZO_PERF_CNT_QLTY;
		else
			tc->tc_quality = -SCHIZO_PERF_CNT_QLTY;
		tc->tc_priv = sc;
		tc_init(tc);
	}

	/*
	 * Set up the IOMMU.  Schizo, Tomatillo and XMITS all have
	 * one per PBM.  Schizo and XMITS additionally have a streaming
	 * buffer, in Schizo version < 5 (i.e. revision < 2.3) it's
	 * affected by several errata though.  However, except for context
	 * flushes, taking advantage of it should be okay even with those.
	 */
	memcpy(&sc->sc_dma_methods, &iommu_dma_methods,
	    sizeof(sc->sc_dma_methods));
	sc->sc_is.sis_sc = sc;
	sc->sc_is.sis_is.is_flags = IOMMU_PRESERVE_PROM;
	sc->sc_is.sis_is.is_pmaxaddr = IOMMU_MAXADDR(STX_IOMMU_BITS);
	sc->sc_is.sis_is.is_sb[0] = sc->sc_is.sis_is.is_sb[1] = 0;
	if (OF_getproplen(node, "no-streaming-cache") < 0)
		sc->sc_is.sis_is.is_sb[0] = STX_PCI_STRBUF;

#define	TSBCASE(x)							\
	case (IOTSB_BASESZ << (x)) << (IO_PAGE_SHIFT - IOTTE_SHIFT):	\
		tsbsize = (x);						\
		break;							\

	i = OF_getprop(node, "virtual-dma", (void *)prop_array,
	    sizeof(prop_array));
	if (i == -1 || i != sizeof(prop_array))
		schizo_iommu_init(sc, 7, -1);
	else {
		switch (prop_array[1]) {
		TSBCASE(1);
		TSBCASE(2);
		TSBCASE(3);
		TSBCASE(4);
		TSBCASE(5);
		TSBCASE(6);
		TSBCASE(7);
		TSBCASE(8);
		default:
			panic("%s: unsupported DVMA size 0x%x",
			    __func__, prop_array[1]);
			/* NOTREACHED */
		}
		schizo_iommu_init(sc, tsbsize, prop_array[0]);
	}

#undef TSBCASE

	/* Create our DMA tag. */
	if (bus_dma_tag_create(bus_get_dma_tag(dev), 8, 0,
	    sc->sc_is.sis_is.is_pmaxaddr, ~0, NULL, NULL,
	    sc->sc_is.sis_is.is_pmaxaddr, 0xff, 0xffffffff, 0, NULL, NULL,
	    &dmat) != 0)
		panic("%s: could not create PCI DMA tag", __func__);
	dmat->dt_cookie = &sc->sc_is;
	dmat->dt_mt = &sc->sc_dma_methods;

	if (ofw_pci_attach_common(dev, dmat, STX_IO_SIZE, STX_MEM_SIZE) != 0)
		panic("%s: ofw_pci_attach_common() failed", __func__);

	/* Clear any pending PCI error bits. */
	PCIB_WRITE_CONFIG(dev, sc->sc_ops.sc_pci_secbus, STX_CS_DEVICE,
	    STX_CS_FUNC, PCIR_STATUS, PCIB_READ_CONFIG(dev,
	    sc->sc_ops.sc_pci_secbus, STX_CS_DEVICE, STX_CS_FUNC, PCIR_STATUS,
	    2), 2);
	SCHIZO_PCI_SET(sc, STX_PCI_CTRL, SCHIZO_PCI_READ_8(sc, STX_PCI_CTRL));
	SCHIZO_PCI_SET(sc, STX_PCI_AFSR, SCHIZO_PCI_READ_8(sc, STX_PCI_AFSR));

	/*
	 * Establish handlers for interesting interrupts...
	 * Someone at Sun clearly was smoking crack; with Schizos PCI
	 * bus error interrupts for one PBM can be routed to the other
	 * PBM though we obviously need to use the softc of the former
	 * as the argument for the interrupt handler and the softc of
	 * the latter as the argument for the interrupt controller.
	 */
	if (sc->sc_half == 0) {
		if ((ino_bitmap & (1ULL << STX_PCIERR_A_INO)) != 0 ||
		    (osc != NULL && ((struct schizo_icarg *)intr_vectors[
		    INTMAP_VEC(sc->sc_ign, STX_PCIERR_A_INO)].iv_icarg)->
		    sica_sc == osc))
			/*
			 * We are the driver for PBM A and either also
			 * registered the interrupt controller for us or
			 * the driver for PBM B has probed first and
			 * registered it for us.
			 */
			schizo_set_intr(sc, 0, STX_PCIERR_A_INO,
			    schizo_pci_bus);
		if ((ino_bitmap & (1ULL << STX_PCIERR_B_INO)) != 0 &&
		    osc != NULL)
			/*
			 * We are the driver for PBM A but registered
			 * the interrupt controller for PBM B, i.e. the
			 * driver for PBM B attached first but couldn't
			 * set up a handler for PBM B.
			 */
			schizo_set_intr(osc, 0, STX_PCIERR_B_INO,
			    schizo_pci_bus);
	} else {
		if ((ino_bitmap & (1ULL << STX_PCIERR_B_INO)) != 0 ||
		    (osc != NULL && ((struct schizo_icarg *)intr_vectors[
		    INTMAP_VEC(sc->sc_ign, STX_PCIERR_B_INO)].iv_icarg)->
		    sica_sc == osc))
			/*
			 * We are the driver for PBM B and either also
			 * registered the interrupt controller for us or
			 * the driver for PBM A has probed first and
			 * registered it for us.
			 */
			schizo_set_intr(sc, 0, STX_PCIERR_B_INO,
			    schizo_pci_bus);
		if ((ino_bitmap & (1ULL << STX_PCIERR_A_INO)) != 0 &&
		    osc != NULL)
			/*
			 * We are the driver for PBM B but registered
			 * the interrupt controller for PBM A, i.e. the
			 * driver for PBM A attached first but couldn't
			 * set up a handler for PBM A.
			 */
			schizo_set_intr(osc, 0, STX_PCIERR_A_INO,
			    schizo_pci_bus);
	}
	if ((ino_bitmap & (1ULL << STX_UE_INO)) != 0)
		schizo_set_intr(sc, 1, STX_UE_INO, schizo_ue);
	if ((ino_bitmap & (1ULL << STX_CE_INO)) != 0)
		schizo_set_intr(sc, 2, STX_CE_INO, schizo_ce);
	if ((ino_bitmap & (1ULL << STX_BUS_INO)) != 0)
		schizo_set_intr(sc, 3, STX_BUS_INO, schizo_host_bus);

	/*
	 * According to the Schizo Errata I-13, consistent DMA flushing/
	 * syncing is FUBAR in version < 5 (i.e. revision < 2.3) bridges,
	 * so we can't use it and need to live with the consequences.  With
	 * Schizo version >= 5, CDMA flushing/syncing is usable but requires
	 * the workaround described in Schizo Errata I-23.  With Tomatillo
	 * and XMITS, CDMA flushing/syncing works as expected, Tomatillo
	 * version <= 4 (i.e. revision <= 2.3) bridges additionally require
	 * a block store after a write to TOMXMS_PCI_DMA_SYNC_PEND though.
	 */
	if ((sc->sc_mode == SCHIZO_MODE_SCZ && sc->sc_ver >= 5) ||
	    sc->sc_mode == SCHIZO_MODE_TOM ||
	    sc->sc_mode == SCHIZO_MODE_XMS) {
		if (sc->sc_mode == SCHIZO_MODE_SCZ) {
			sc->sc_dma_methods.dm_dmamap_sync =
			    schizo_dmamap_sync;
			sc->sc_cdma_state = SCHIZO_CDMA_STATE_IDLE;
			/*
			 * Some firmware versions include the CDMA interrupt
			 * at RID 4 but most don't.  With the latter we add
			 * it ourselves at the spare RID 5.
			 */
			i = INTINO(bus_get_resource_start(dev, SYS_RES_IRQ,
			    4));
			if (i == STX_CDMA_A_INO || i == STX_CDMA_B_INO) {
				sc->sc_cdma_vec = INTMAP_VEC(sc->sc_ign, i);
				(void)schizo_get_intrmap(sc, i,
				   &sc->sc_cdma_map, &sc->sc_cdma_clr);
				schizo_set_intr(sc, 4, i, schizo_cdma);
			} else {
				i = STX_CDMA_A_INO + sc->sc_half;
				sc->sc_cdma_vec = INTMAP_VEC(sc->sc_ign, i);
				if (bus_set_resource(dev, SYS_RES_IRQ, 5,
				    sc->sc_cdma_vec, 1) != 0)
					panic("%s: failed to add CDMA "
					    "interrupt", __func__);
				j = schizo_intr_register(sc, i);
				if (j != 0)
					panic("%s: could not register "
					    "interrupt controller for CDMA "
					    "(%d)", __func__, j);
				(void)schizo_get_intrmap(sc, i,
				   &sc->sc_cdma_map, &sc->sc_cdma_clr);
				schizo_set_intr(sc, 5, i, schizo_cdma);
			}
		} else {
			if (sc->sc_mode == SCHIZO_MODE_XMS)
				mtx_init(&sc->sc_sync_mtx, "pcib_sync_mtx",
				    NULL, MTX_SPIN);
			sc->sc_sync_val = 1ULL << (STX_PCIERR_A_INO +
			    sc->sc_half);
			sc->sc_dma_methods.dm_dmamap_sync =
			    ichip_dmamap_sync;
		}
		if (sc->sc_mode == SCHIZO_MODE_TOM && sc->sc_ver <= 4)
			sc->sc_flags |= SCHIZO_FLAGS_BSWAR;
	}

	/*
	 * Set the latency timer register as this isn't always done by the
	 * firmware.
	 */
	PCIB_WRITE_CONFIG(dev, sc->sc_ops.sc_pci_secbus, STX_CS_DEVICE,
	    STX_CS_FUNC, PCIR_LATTIMER, OFW_PCI_LATENCY, 1);

#define	SCHIZO_SYSCTL_ADD_UINT(name, arg, desc)				\
	SYSCTL_ADD_UINT(device_get_sysctl_ctx(dev),			\
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO,	\
	    (name), CTLFLAG_RD, (arg), 0, (desc))

	SCHIZO_SYSCTL_ADD_UINT("dma_ce", &sc->sc_stats_dma_ce,
	    "DMA correctable errors");
	SCHIZO_SYSCTL_ADD_UINT("pci_non_fatal", &sc->sc_stats_pci_non_fatal,
	    "PCI bus non-fatal errors");

#undef SCHIZO_SYSCTL_ADD_UINT

	device_add_child(dev, "pci", -1);
	return (bus_generic_attach(dev));
}

static void
schizo_set_intr(struct schizo_softc *sc, u_int index, u_int ino,
    driver_filter_t handler)
{
	u_long vec;
	int rid;

	rid = index;
	sc->sc_irq_res[index] = bus_alloc_resource_any(sc->sc_dev,
	    SYS_RES_IRQ, &rid, RF_ACTIVE);
	if (sc->sc_irq_res[index] == NULL ||
	    INTINO(vec = rman_get_start(sc->sc_irq_res[index])) != ino ||
	    INTIGN(vec) != sc->sc_ign ||
	    intr_vectors[vec].iv_ic != &schizo_ic ||
	    bus_setup_intr(sc->sc_dev, sc->sc_irq_res[index],
	    INTR_TYPE_MISC | INTR_BRIDGE, handler, NULL, sc,
	    &sc->sc_ihand[index]) != 0)
		panic("%s: failed to set up interrupt %d", __func__, index);
}

static int
schizo_intr_register(struct schizo_softc *sc, u_int ino)
{
	struct schizo_icarg *sica;
	bus_addr_t intrclr, intrmap;
	int error;

	if (schizo_get_intrmap(sc, ino, &intrmap, &intrclr) == 0)
		return (ENXIO);
	sica = malloc(sizeof(*sica), M_DEVBUF, M_NOWAIT);
	if (sica == NULL)
		return (ENOMEM);
	sica->sica_sc = sc;
	sica->sica_map = intrmap;
	sica->sica_clr = intrclr;
#ifdef SCHIZO_DEBUG
	device_printf(sc->sc_dev, "intr map (INO %d) %#lx: %#lx, clr: %#lx\n",
	    ino, (u_long)intrmap, (u_long)SCHIZO_PCI_READ_8(sc, intrmap),
	    (u_long)intrclr);
#endif
	error = (intr_controller_register(INTMAP_VEC(sc->sc_ign, ino),
	    &schizo_ic, sica));
	if (error != 0)
		free(sica, M_DEVBUF);
	return (error);
}

static int
schizo_get_intrmap(struct schizo_softc *sc, u_int ino,
    bus_addr_t *intrmapptr, bus_addr_t *intrclrptr)
{
	bus_addr_t intrclr, intrmap;
	uint64_t mr;

	/*
	 * XXX we only look for INOs rather than INRs since the firmware
	 * may not provide the IGN and the IGN is constant for all devices
	 * on that PCI controller.
	 */

	if (ino > STX_MAX_INO) {
		device_printf(sc->sc_dev, "out of range INO %d requested\n",
		    ino);
		return (0);
	}

	intrmap = STX_PCI_IMAP_BASE + (ino << 3);
	intrclr = STX_PCI_ICLR_BASE + (ino << 3);
	mr = SCHIZO_PCI_READ_8(sc, intrmap);
	if (INTINO(mr) != ino) {
		device_printf(sc->sc_dev,
		    "interrupt map entry does not match INO (%d != %d)\n",
		    (int)INTINO(mr), ino);
		return (0);
	}

	if (intrmapptr != NULL)
		*intrmapptr = intrmap;
	if (intrclrptr != NULL)
		*intrclrptr = intrclr;
	return (1);
}

/*
 * Interrupt handlers
 */
static int
schizo_pci_bus(void *arg)
{
	struct schizo_softc *sc = arg;
	uint64_t afar, afsr, csr, iommu, xstat;
	uint32_t status;
	u_int fatal;

	fatal = 0;

	mtx_lock_spin(sc->sc_mtx);

	afar = SCHIZO_PCI_READ_8(sc, STX_PCI_AFAR);
	afsr = SCHIZO_PCI_READ_8(sc, STX_PCI_AFSR);
	csr = SCHIZO_PCI_READ_8(sc, STX_PCI_CTRL);
	iommu = SCHIZO_PCI_READ_8(sc, STX_PCI_IOMMU);
	if ((sc->sc_flags & SCHIZO_FLAGS_XMODE) != 0)
		xstat = SCHIZO_PCI_READ_8(sc, XMS_PCI_X_ERR_STAT);
	else
		xstat = 0;
	status = PCIB_READ_CONFIG(sc->sc_dev, sc->sc_ops.sc_pci_secbus,
	    STX_CS_DEVICE, STX_CS_FUNC, PCIR_STATUS, 2);

	/*
	 * IOMMU errors are only fatal on Tomatillo and there also only if
	 * target abort was not signaled.
	 */
	if ((csr & STX_PCI_CTRL_MMU_ERR) != 0 &&
	    (iommu & TOM_PCI_IOMMU_ERR) != 0 &&
	    ((status & PCIM_STATUS_STABORT) == 0 ||
	    ((iommu & TOM_PCI_IOMMU_ERRMASK) != TOM_PCI_IOMMU_INVALID_ERR &&
	    (iommu & TOM_PCI_IOMMU_ERR_ILLTSBTBW) == 0 &&
	    (iommu & TOM_PCI_IOMMU_ERR_BAD_VA) == 0)))
		fatal = 1;
	else if ((status & PCIM_STATUS_STABORT) != 0)
		fatal = 1;
	if ((status & (PCIM_STATUS_PERR | PCIM_STATUS_SERR |
	    PCIM_STATUS_RMABORT | PCIM_STATUS_RTABORT |
	    PCIM_STATUS_MDPERR)) != 0 ||
	    (csr & (SCZ_PCI_CTRL_BUS_UNUS | TOM_PCI_CTRL_DTO_ERR |
	    STX_PCI_CTRL_TTO_ERR | STX_PCI_CTRL_RTRY_ERR |
	    SCZ_PCI_CTRL_SBH_ERR | STX_PCI_CTRL_SERR)) != 0 ||
	    (afsr & (STX_PCI_AFSR_P_MA | STX_PCI_AFSR_P_TA |
	    STX_PCI_AFSR_P_RTRY | STX_PCI_AFSR_P_PERR | STX_PCI_AFSR_P_TTO |
	    STX_PCI_AFSR_P_UNUS)) != 0)
		fatal = 1;
	if (xstat & (XMS_PCI_X_ERR_STAT_P_SC_DSCRD |
	    XMS_PCI_X_ERR_STAT_P_SC_TTO | XMS_PCI_X_ERR_STAT_P_SDSTAT |
	    XMS_PCI_X_ERR_STAT_P_SMMU | XMS_PCI_X_ERR_STAT_P_CDSTAT |
	    XMS_PCI_X_ERR_STAT_P_CMMU | XMS_PCI_X_ERR_STAT_PERR_RCV))
		fatal = 1;
	if (fatal == 0)
		sc->sc_stats_pci_non_fatal++;

	device_printf(sc->sc_dev, "PCI bus %c error AFAR %#llx AFSR %#llx "
	    "PCI CSR %#llx IOMMU %#llx PCI-X %#llx STATUS %#x\n",
	    'A' + sc->sc_half, (unsigned long long)afar,
	    (unsigned long long)afsr, (unsigned long long)csr,
	    (unsigned long long)iommu, (unsigned long long)xstat, status);

	/* Clear the error bits that we caught. */
	PCIB_WRITE_CONFIG(sc->sc_dev, sc->sc_ops.sc_pci_secbus, STX_CS_DEVICE,
	    STX_CS_FUNC, PCIR_STATUS, status, 2);
	SCHIZO_PCI_WRITE_8(sc, STX_PCI_CTRL, csr);
	SCHIZO_PCI_WRITE_8(sc, STX_PCI_AFSR, afsr);
	SCHIZO_PCI_WRITE_8(sc, STX_PCI_IOMMU, iommu);
	if ((sc->sc_flags & SCHIZO_FLAGS_XMODE) != 0)
		SCHIZO_PCI_WRITE_8(sc, XMS_PCI_X_ERR_STAT, xstat);

	mtx_unlock_spin(sc->sc_mtx);

	if (fatal != 0)
		panic("%s: fatal PCI bus error",
		    device_get_nameunit(sc->sc_dev));
	return (FILTER_HANDLED);
}

static int
schizo_ue(void *arg)
{
	struct schizo_softc *sc = arg;
	uint64_t afar, afsr;
	int i;

	afar = SCHIZO_CTRL_READ_8(sc, STX_CTRL_UE_AFAR);
	for (i = 0; i < 1000; i++)
		if (((afsr = SCHIZO_CTRL_READ_8(sc, STX_CTRL_UE_AFSR)) &
		    STX_CTRL_CE_AFSR_ERRPNDG) == 0)
			break;
	panic("%s: uncorrectable DMA error AFAR %#llx AFSR %#llx",
	    device_get_nameunit(sc->sc_dev), (unsigned long long)afar,
	    (unsigned long long)afsr);
	return (FILTER_HANDLED);
}

static int
schizo_ce(void *arg)
{
	struct schizo_softc *sc = arg;
	uint64_t afar, afsr;
	int i;

	mtx_lock_spin(sc->sc_mtx);

	afar = SCHIZO_CTRL_READ_8(sc, STX_CTRL_CE_AFAR);
	for (i = 0; i < 1000; i++)
		if (((afsr = SCHIZO_CTRL_READ_8(sc, STX_CTRL_UE_AFSR)) &
		    STX_CTRL_CE_AFSR_ERRPNDG) == 0)
			break;
	sc->sc_stats_dma_ce++;
	device_printf(sc->sc_dev,
	    "correctable DMA error AFAR %#llx AFSR %#llx\n",
	    (unsigned long long)afar, (unsigned long long)afsr);

	/* Clear the error bits that we caught. */
	SCHIZO_CTRL_WRITE_8(sc, STX_CTRL_UE_AFSR, afsr);

	mtx_unlock_spin(sc->sc_mtx);

	return (FILTER_HANDLED);
}

static int
schizo_host_bus(void *arg)
{
	struct schizo_softc *sc = arg;
	uint64_t errlog;

	errlog = SCHIZO_CTRL_READ_8(sc, STX_CTRL_BUS_ERRLOG);
	panic("%s: %s error %#llx", device_get_nameunit(sc->sc_dev),
	    sc->sc_mode == SCHIZO_MODE_TOM ? "JBus" : "Safari",
	    (unsigned long long)errlog);
	return (FILTER_HANDLED);
}

static int
schizo_cdma(void *arg)
{
	struct schizo_softc *sc = arg;

	atomic_cmpset_32(&sc->sc_cdma_state, SCHIZO_CDMA_STATE_PENDING,
	    SCHIZO_CDMA_STATE_RECEIVED);
	return (FILTER_HANDLED);
}

static void
schizo_iommu_init(struct schizo_softc *sc, int tsbsize, uint32_t dvmabase)
{

	/* Punch in our copies. */
	sc->sc_is.sis_is.is_bustag = rman_get_bustag(sc->sc_mem_res[STX_PCI]);
	sc->sc_is.sis_is.is_bushandle =
	    rman_get_bushandle(sc->sc_mem_res[STX_PCI]);
	sc->sc_is.sis_is.is_iommu = STX_PCI_IOMMU;
	sc->sc_is.sis_is.is_dtag = STX_PCI_IOMMU_TLB_TAG_DIAG;
	sc->sc_is.sis_is.is_ddram = STX_PCI_IOMMU_TLB_DATA_DIAG;
	sc->sc_is.sis_is.is_dqueue = STX_PCI_IOMMU_QUEUE_DIAG;
	sc->sc_is.sis_is.is_dva = STX_PCI_IOMMU_SVADIAG;
	sc->sc_is.sis_is.is_dtcmp = STX_PCI_IOMMU_TLB_CMP_DIAG;

	iommu_init(device_get_nameunit(sc->sc_dev),
	    (struct iommu_state *)&sc->sc_is, tsbsize, dvmabase, 0);
}

static int
schizo_maxslots(device_t dev)
{
	struct schizo_softc *sc;

	sc = device_get_softc(dev);
	if (sc->sc_mode == SCHIZO_MODE_SCZ)
		return (sc->sc_half == 0 ? 4 : 6);

	/* XXX: is this correct? */
	return (PCI_SLOTMAX);
}

static uint32_t
schizo_read_config(device_t dev, u_int bus, u_int slot, u_int func, u_int reg,
    int width)
{
	struct schizo_softc *sc;

	sc = device_get_softc(dev);
	/*
	 * The Schizo bridges contain a dupe of their header at 0x80.
	 */
	if (sc->sc_mode == SCHIZO_MODE_SCZ &&
	    bus == sc->sc_ops.sc_pci_secbus && slot == STX_CS_DEVICE &&
	    func == STX_CS_FUNC && reg + width > 0x80)
		return (0);

	return (ofw_pci_read_config_common(dev, PCI_REGMAX, STX_CONF_OFF(bus,
	    slot, func, reg), bus, slot, func, reg, width));
}

static void
schizo_write_config(device_t dev, u_int bus, u_int slot, u_int func,
    u_int reg, uint32_t val, int width)
{

	ofw_pci_write_config_common(dev, PCI_REGMAX, STX_CONF_OFF(bus, slot,
	    func, reg), bus, slot, func, reg, val, width);
}

static int
schizo_route_interrupt(device_t bridge, device_t dev, int pin)
{
	ofw_pci_intr_t mintr;

	mintr = ofw_pci_route_interrupt_common(bridge, dev, pin);
	if (!PCI_INTERRUPT_VALID(mintr))
		device_printf(bridge,
		    "could not route pin %d for device %d.%d\n",
		    pin, pci_get_slot(dev), pci_get_function(dev));
	return (mintr);
}

static void
schizo_dmamap_sync(bus_dma_tag_t dt, bus_dmamap_t map, bus_dmasync_op_t op)
{
	struct timeval cur, end;
	struct schizo_iommu_state *sis = dt->dt_cookie;
	struct schizo_softc *sc = sis->sis_sc;
	int i, res;
#ifdef INVARIANTS
	register_t pil;
#endif

	if ((map->dm_flags & DMF_STREAMED) != 0) {
		iommu_dma_methods.dm_dmamap_sync(dt, map, op);
		return;
	}

	if ((map->dm_flags & DMF_LOADED) == 0)
		return;

	if ((op & BUS_DMASYNC_POSTREAD) != 0) {
		/*
		 * Note that in order to allow this function to be called from
		 * filters we would need to use a spin mutex for serialization
		 * but given that these disable interrupts we have to emulate
		 * one.
		 */
		critical_enter();
		KASSERT((rdpr(pstate) & PSTATE_IE) != 0,
		    ("%s: interrupts disabled", __func__));
		KASSERT((pil = rdpr(pil)) <= PIL_BRIDGE,
		    ("%s: PIL too low (%ld)", __func__, pil));
		for (; atomic_cmpset_acq_32(&sc->sc_cdma_state,
		    SCHIZO_CDMA_STATE_IDLE, SCHIZO_CDMA_STATE_PENDING) == 0;)
			;
		SCHIZO_PCI_WRITE_8(sc, sc->sc_cdma_map,
		    INTMAP_ENABLE(sc->sc_cdma_vec, PCPU_GET(mid)));
		for (i = 0; i < SCHIZO_CDMA_TRIES; i++) {
			if (i > 0)
				printf("%s: try %d\n", __func__, i);
			SCHIZO_PCI_WRITE_8(sc, sc->sc_cdma_clr,
			    INTCLR_RECEIVED);
			microuptime(&cur);
			end.tv_sec = SCHIZO_CDMA_TIMEOUT;
			end.tv_usec = 0;
			timevaladd(&end, &cur);
			for (; (res = atomic_cmpset_rel_32(&sc->sc_cdma_state,
			    SCHIZO_CDMA_STATE_RECEIVED,
			    SCHIZO_CDMA_STATE_IDLE)) == 0 &&
			    timevalcmp(&cur, &end, <=);)
				microuptime(&cur);
			if (res != 0)
				break;
		}
		if (res == 0)
			panic("%s: DMA does not sync", __func__);
		critical_exit();
	}

	if ((op & BUS_DMASYNC_PREWRITE) != 0)
		membar(Sync);
}

static void
ichip_dmamap_sync(bus_dma_tag_t dt, bus_dmamap_t map, bus_dmasync_op_t op)
{
	struct timeval cur, end;
	struct schizo_iommu_state *sis = dt->dt_cookie;
	struct schizo_softc *sc = sis->sis_sc;
	uint64_t reg;

	if ((map->dm_flags & DMF_STREAMED) != 0) {
		iommu_dma_methods.dm_dmamap_sync(dt, map, op);
		return;
	}

	if ((map->dm_flags & DMF_LOADED) == 0)
		return;

	if ((op & BUS_DMASYNC_POSTREAD) != 0) {
		if (sc->sc_mode == SCHIZO_MODE_XMS)
			mtx_lock_spin(&sc->sc_sync_mtx);
		SCHIZO_PCI_WRITE_8(sc, TOMXMS_PCI_DMA_SYNC_PEND,
		    sc->sc_sync_val);
		microuptime(&cur);
		end.tv_sec = 1;
		end.tv_usec = 0;
		timevaladd(&end, &cur);
		for (; ((reg = SCHIZO_PCI_READ_8(sc,
		    TOMXMS_PCI_DMA_SYNC_PEND)) & sc->sc_sync_val) != 0 &&
		    timevalcmp(&cur, &end, <=);)
			microuptime(&cur);
		if ((reg & sc->sc_sync_val) != 0)
			panic("%s: DMA does not sync", __func__);
		if (sc->sc_mode == SCHIZO_MODE_XMS)
			mtx_unlock_spin(&sc->sc_sync_mtx);
		else if ((sc->sc_flags & SCHIZO_FLAGS_BSWAR) != 0) {
			ofw_pci_dmamap_sync_stst_order_common();
			return;
		}
	}

	if ((op & BUS_DMASYNC_PREWRITE) != 0)
		membar(Sync);
}

static void
schizo_intr_enable(void *arg)
{
	struct intr_vector *iv = arg;
	struct schizo_icarg *sica = iv->iv_icarg;

	SCHIZO_PCI_WRITE_8(sica->sica_sc, sica->sica_map,
	    INTMAP_ENABLE(iv->iv_vec, iv->iv_mid));
}

static void
schizo_intr_disable(void *arg)
{
	struct intr_vector *iv = arg;
	struct schizo_icarg *sica = iv->iv_icarg;

	SCHIZO_PCI_WRITE_8(sica->sica_sc, sica->sica_map, iv->iv_vec);
}

static void
schizo_intr_assign(void *arg)
{
	struct intr_vector *iv = arg;
	struct schizo_icarg *sica = iv->iv_icarg;

	SCHIZO_PCI_WRITE_8(sica->sica_sc, sica->sica_map, INTMAP_TID(
	    SCHIZO_PCI_READ_8(sica->sica_sc, sica->sica_map), iv->iv_mid));
}

static void
schizo_intr_clear(void *arg)
{
	struct intr_vector *iv = arg;
	struct schizo_icarg *sica = iv->iv_icarg;

	SCHIZO_PCI_WRITE_8(sica->sica_sc, sica->sica_clr, INTCLR_IDLE);
}

static int
schizo_setup_intr(device_t dev, device_t child, struct resource *ires,
    int flags, driver_filter_t *filt, driver_intr_t *intr, void *arg,
    void **cookiep)
{
	struct schizo_softc *sc;
	u_long vec;
	int error;

	sc = device_get_softc(dev);
	/*
	 * Make sure the vector is fully specified.
	 */
	vec = rman_get_start(ires);
	if (INTIGN(vec) != sc->sc_ign) {
		device_printf(dev, "invalid interrupt vector 0x%lx\n", vec);
		return (EINVAL);
	}

	if (intr_vectors[vec].iv_ic == &schizo_ic) {
		/*
		 * Ensure we use the right softc in case the interrupt
		 * is routed to our companion PBM for some odd reason.
		 */
		sc = ((struct schizo_icarg *)intr_vectors[vec].iv_icarg)->
		    sica_sc;
	} else if (intr_vectors[vec].iv_ic == NULL) {
		/*
		 * Work around broken firmware which misses entries in
		 * the ino-bitmap.
		 */
		error = schizo_intr_register(sc, INTINO(vec));
		if (error != 0) {
			device_printf(dev, "could not register interrupt "
			    "controller for vector 0x%lx (%d)\n", vec, error);
			return (error);
		}
		if (bootverbose)
			device_printf(dev, "belatedly registered as "
			    "interrupt controller for vector 0x%lx\n", vec);
	} else {
		device_printf(dev,
		    "invalid interrupt controller for vector 0x%lx\n", vec);
		return (EINVAL);
	}
	return (bus_generic_setup_intr(dev, child, ires, flags, filt, intr,
	    arg, cookiep));
}

static struct resource *
schizo_alloc_resource(device_t bus, device_t child, int type, int *rid,
    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct schizo_softc *sc;

	if (type == SYS_RES_IRQ) {
		sc = device_get_softc(bus);
		start = end = INTMAP_VEC(sc->sc_ign, end);
	}
	return (ofw_pci_alloc_resource(bus, child, type, rid, start, end,
	    count, flags));
}

static void
schizo_setup_device(device_t bus, device_t child)
{
	struct schizo_softc *sc;
	uint64_t reg;
	int capreg;

	sc = device_get_softc(bus);
	/*
	 * Disable bus parking in order to work around a bus hang caused by
	 * Casinni/Skyhawk combinations.
	 */
	if (OF_getproplen(ofw_bus_get_node(child), "pci-req-removal") >= 0)
		SCHIZO_PCI_SET(sc, STX_PCI_CTRL, SCHIZO_PCI_READ_8(sc,
		    STX_PCI_CTRL) & ~STX_PCI_CTRL_ARB_PARK);

	if (sc->sc_mode == SCHIZO_MODE_XMS) {
		/* XMITS NCPQ WAR: set outstanding split transactions to 1. */
		if ((sc->sc_flags & SCHIZO_FLAGS_XMODE) != 0 &&
		    (pci_read_config(child, PCIR_HDRTYPE, 1) &
		    PCIM_HDRTYPE) != PCIM_HDRTYPE_BRIDGE &&
		    pci_find_cap(child, PCIY_PCIX, &capreg) == 0)
			pci_write_config(child, capreg + PCIXR_COMMAND,
			    pci_read_config(child, capreg + PCIXR_COMMAND,
			    2) & 0x7c, 2);
		/* XMITS 3.x WAR: set BUGCNTL iff value is unexpected. */
		if (sc->sc_mrev >= 4) {
			reg = ((sc->sc_flags & SCHIZO_FLAGS_XMODE) != 0 ?
			    0xa0UL : 0xffUL) << XMS_PCI_X_DIAG_BUGCNTL_SHIFT;
			if ((SCHIZO_PCI_READ_8(sc, XMS_PCI_X_DIAG) &
			    XMS_PCI_X_DIAG_BUGCNTL_MASK) != reg)
				SCHIZO_PCI_SET(sc, XMS_PCI_X_DIAG, reg);
		}
	}
}

static u_int
schizo_get_timecount(struct timecounter *tc)
{
	struct schizo_softc *sc;

	sc = tc->tc_priv;
	return ((SCHIZO_CTRL_READ_8(sc, STX_CTRL_PERF_CNT) &
	    (STX_CTRL_PERF_CNT_MASK << STX_CTRL_PERF_CNT_CNT0_SHIFT)) >>
	    STX_CTRL_PERF_CNT_CNT0_SHIFT);
}
