/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1999, 2000 Matthew R. Green
 * Copyright (c) 2001 - 2003 by Thomas Moestl <tmm@FreeBSD.org>
 * Copyright (c) 2009 by Marius Strobl <marius@FreeBSD.org>
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
 * Driver for `Fire' JBus to PCI Express and `Oberon' Uranus to PCI Express
 * bridges
 */

#include "opt_fire.h"
#include "opt_ofw_pci.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/interrupt.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/pciio.h>
#include <sys/pcpu.h>
#include <sys/rman.h>
#include <sys/smp.h>
#include <sys/sysctl.h>
#include <sys/timetc.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/openfirm.h>

#include <vm/vm.h>
#include <vm/pmap.h>

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
#include <sparc64/pci/firereg.h>
#include <sparc64/pci/firevar.h>

#include "pcib_if.h"

struct fire_msiqarg;

static const struct fire_desc *fire_get_desc(device_t dev);
static void fire_dmamap_sync(bus_dma_tag_t dt __unused, bus_dmamap_t map,
    bus_dmasync_op_t op);
static int fire_get_intrmap(struct fire_softc *sc, u_int ino,
    bus_addr_t *intrmapptr, bus_addr_t *intrclrptr);
static void fire_intr_assign(void *arg);
static void fire_intr_clear(void *arg);
static void fire_intr_disable(void *arg);
static void fire_intr_enable(void *arg);
static int fire_intr_register(struct fire_softc *sc, u_int ino);
static inline void fire_msiq_common(struct intr_vector *iv,
    struct fire_msiqarg *fmqa);
static void fire_msiq_filter(void *cookie);
static void fire_msiq_handler(void *cookie);
static void fire_set_intr(struct fire_softc *sc, u_int index, u_int ino,
    driver_filter_t handler, void *arg);
static timecounter_get_t fire_get_timecount;

/* Interrupt handlers */
static driver_filter_t fire_dmc_pec;
static driver_filter_t fire_pcie;
static driver_filter_t fire_xcb;

/*
 * Methods
 */
static pcib_alloc_msi_t fire_alloc_msi;
static pcib_alloc_msix_t fire_alloc_msix;
static bus_alloc_resource_t fire_alloc_resource;
static device_attach_t fire_attach;
static pcib_map_msi_t fire_map_msi;
static pcib_maxslots_t fire_maxslots;
static device_probe_t fire_probe;
static pcib_read_config_t fire_read_config;
static pcib_release_msi_t fire_release_msi;
static pcib_release_msix_t fire_release_msix;
static pcib_route_interrupt_t fire_route_interrupt;
static bus_setup_intr_t fire_setup_intr;
static bus_teardown_intr_t fire_teardown_intr;
static pcib_write_config_t fire_write_config;

static device_method_t fire_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		fire_probe),
	DEVMETHOD(device_attach,	fire_attach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),

	/* Bus interface */
	DEVMETHOD(bus_read_ivar,	ofw_pci_read_ivar),
	DEVMETHOD(bus_setup_intr,	fire_setup_intr),
	DEVMETHOD(bus_teardown_intr,	fire_teardown_intr),
	DEVMETHOD(bus_alloc_resource,	fire_alloc_resource),
	DEVMETHOD(bus_activate_resource, ofw_pci_activate_resource),
	DEVMETHOD(bus_deactivate_resource, bus_generic_deactivate_resource),
	DEVMETHOD(bus_adjust_resource,	ofw_pci_adjust_resource),
	DEVMETHOD(bus_release_resource,	bus_generic_release_resource),
	DEVMETHOD(bus_get_dma_tag,	ofw_pci_get_dma_tag),

	/* pcib interface */
	DEVMETHOD(pcib_maxslots,	fire_maxslots),
	DEVMETHOD(pcib_read_config,	fire_read_config),
	DEVMETHOD(pcib_write_config,	fire_write_config),
	DEVMETHOD(pcib_route_interrupt,	fire_route_interrupt),
	DEVMETHOD(pcib_alloc_msi,	fire_alloc_msi),
	DEVMETHOD(pcib_release_msi,	fire_release_msi),
	DEVMETHOD(pcib_alloc_msix,	fire_alloc_msix),
	DEVMETHOD(pcib_release_msix,	fire_release_msix),
	DEVMETHOD(pcib_map_msi,		fire_map_msi),
	DEVMETHOD(pcib_request_feature,	pcib_request_feature_allow),

	/* ofw_bus interface */
	DEVMETHOD(ofw_bus_get_node,	ofw_pci_get_node),

	DEVMETHOD_END
};

static devclass_t fire_devclass;

DEFINE_CLASS_0(pcib, fire_driver, fire_methods, sizeof(struct fire_softc));
EARLY_DRIVER_MODULE(fire, nexus, fire_driver, fire_devclass, 0, 0,
    BUS_PASS_BUS);
MODULE_DEPEND(fire, nexus, 1, 1, 1);

static const struct intr_controller fire_ic = {
	fire_intr_enable,
	fire_intr_disable,
	fire_intr_assign,
	fire_intr_clear
};

struct fire_icarg {
	struct fire_softc	*fica_sc;
	bus_addr_t		fica_map;
	bus_addr_t		fica_clr;
};

static const struct intr_controller fire_msiqc_filter = {
	fire_intr_enable,
	fire_intr_disable,
	fire_intr_assign,
	NULL
};

struct fire_msiqarg {
	struct fire_icarg	fmqa_fica;
	struct mtx		fmqa_mtx;
	struct fo_msiq_record	*fmqa_base;
	uint64_t		fmqa_head;
	uint64_t		fmqa_tail;
	uint32_t		fmqa_msiq;
	uint32_t		fmqa_msi;
};

#define	FIRE_PERF_CNT_QLTY	100

#define	FIRE_SPC_BARRIER(spc, sc, offs, len, flags)			\
	bus_barrier((sc)->sc_mem_res[(spc)], (offs), (len), (flags))
#define	FIRE_SPC_READ_8(spc, sc, offs)					\
	bus_read_8((sc)->sc_mem_res[(spc)], (offs))
#define	FIRE_SPC_WRITE_8(spc, sc, offs, v)				\
	bus_write_8((sc)->sc_mem_res[(spc)], (offs), (v))

#ifndef FIRE_DEBUG
#define	FIRE_SPC_SET(spc, sc, offs, reg, v)				\
	FIRE_SPC_WRITE_8((spc), (sc), (offs), (v))
#else
#define	FIRE_SPC_SET(spc, sc, offs, reg, v) do {			\
	device_printf((sc)->sc_dev, reg " 0x%016llx -> 0x%016llx\n",	\
	    (unsigned long long)FIRE_SPC_READ_8((spc), (sc), (offs)),	\
	    (unsigned long long)(v));					\
	FIRE_SPC_WRITE_8((spc), (sc), (offs), (v));			\
	} while (0)
#endif

#define	FIRE_PCI_BARRIER(sc, offs, len, flags)				\
	FIRE_SPC_BARRIER(FIRE_PCI, (sc), (offs), len, flags)
#define	FIRE_PCI_READ_8(sc, offs)					\
	FIRE_SPC_READ_8(FIRE_PCI, (sc), (offs))
#define	FIRE_PCI_WRITE_8(sc, offs, v)					\
	FIRE_SPC_WRITE_8(FIRE_PCI, (sc), (offs), (v))
#define	FIRE_CTRL_BARRIER(sc, offs, len, flags)				\
	FIRE_SPC_BARRIER(FIRE_CTRL, (sc), (offs), len, flags)
#define	FIRE_CTRL_READ_8(sc, offs)					\
	FIRE_SPC_READ_8(FIRE_CTRL, (sc), (offs))
#define	FIRE_CTRL_WRITE_8(sc, offs, v)					\
	FIRE_SPC_WRITE_8(FIRE_CTRL, (sc), (offs), (v))

#define	FIRE_PCI_SET(sc, offs, v)					\
	FIRE_SPC_SET(FIRE_PCI, (sc), (offs), # offs, (v))
#define	FIRE_CTRL_SET(sc, offs, v)					\
	FIRE_SPC_SET(FIRE_CTRL, (sc), (offs), # offs, (v))

struct fire_desc {
	const char	*fd_string;
	int		fd_mode;
	const char	*fd_name;
};

static const struct fire_desc fire_compats[] = {
	{ "pciex108e,80f0",	FIRE_MODE_FIRE,		"Fire" },
#if 0
	{ "pciex108e,80f8",	FIRE_MODE_OBERON,	"Oberon" },
#endif
	{ NULL,			0,			NULL }
};

static const struct fire_desc *
fire_get_desc(device_t dev)
{
	const struct fire_desc *desc;
	const char *compat;

	compat = ofw_bus_get_compat(dev);
	if (compat == NULL)
		return (NULL);
	for (desc = fire_compats; desc->fd_string != NULL; desc++)
		if (strcmp(desc->fd_string, compat) == 0)
			return (desc);
	return (NULL);
}

static int
fire_probe(device_t dev)
{
	const char *dtype;

	dtype = ofw_bus_get_type(dev);
	if (dtype != NULL && strcmp(dtype, OFW_TYPE_PCIE) == 0 &&
	    fire_get_desc(dev) != NULL) {
		device_set_desc(dev, "Sun Host-PCIe bridge");
		return (BUS_PROBE_GENERIC);
	}
	return (ENXIO);
}

static int
fire_attach(device_t dev)
{
	struct fire_softc *sc;
	const struct fire_desc *desc;
	struct ofw_pci_msi_ranges msi_ranges;
	struct ofw_pci_msi_addr_ranges msi_addr_ranges;
	struct ofw_pci_msi_eq_to_devino msi_eq_to_devino;
	struct fire_msiqarg *fmqa;
	struct timecounter *tc;
	bus_dma_tag_t dmat;
	uint64_t ino_bitmap, val;
	phandle_t node;
	uint32_t prop, prop_array[2];
	int i, j, mode;
	u_int lw;
	uint16_t mps;

	sc = device_get_softc(dev);
	node = ofw_bus_get_node(dev);
	desc = fire_get_desc(dev);
	mode = desc->fd_mode;

	sc->sc_dev = dev;
	sc->sc_mode = mode;
	sc->sc_flags = 0;

	mtx_init(&sc->sc_msi_mtx, "msi_mtx", NULL, MTX_DEF);
	mtx_init(&sc->sc_pcib_mtx, "pcib_mtx", NULL, MTX_SPIN);

	/*
	 * Fire and Oberon have two register banks:
	 * (0) per-PBM PCI Express configuration and status registers
	 * (1) (shared) Fire/Oberon controller configuration and status
	 *     registers
	 */
	for (i = 0; i < FIRE_NREG; i++) {
		j = i;
		sc->sc_mem_res[i] = bus_alloc_resource_any(dev,
		    SYS_RES_MEMORY, &j, RF_ACTIVE);
		if (sc->sc_mem_res[i] == NULL)
			panic("%s: could not allocate register bank %d",
			    __func__, i);
	}

	if (OF_getprop(node, "portid", &sc->sc_ign, sizeof(sc->sc_ign)) == -1)
		panic("%s: could not determine IGN", __func__);
	if (OF_getprop(node, "module-revision#", &prop, sizeof(prop)) == -1)
		panic("%s: could not determine module-revision", __func__);

	device_printf(dev, "%s, module-revision %d, IGN %#x\n",
	    desc->fd_name, prop, sc->sc_ign);

	/*
	 * Hunt through all the interrupt mapping regs and register
	 * the interrupt controller for our interrupt vectors.  We do
	 * this early in order to be able to catch stray interrupts.
	 */
	i = OF_getprop(node, "ino-bitmap", (void *)prop_array,
	    sizeof(prop_array));
	if (i == -1)
		panic("%s: could not get ino-bitmap", __func__);
	ino_bitmap = ((uint64_t)prop_array[1] << 32) | prop_array[0];
	for (i = 0; i <= FO_MAX_INO; i++) {
		if ((ino_bitmap & (1ULL << i)) == 0)
			continue;
		j = fire_intr_register(sc, i);
		if (j != 0)
			device_printf(dev, "could not register interrupt "
			    "controller for INO %d (%d)\n", i, j);
	}

	/* JBC/UBC module initialization */
	FIRE_CTRL_SET(sc, FO_XBC_ERR_LOG_EN, ~0ULL);
	FIRE_CTRL_SET(sc, FO_XBC_ERR_STAT_CLR, ~0ULL);
	/* not enabled by OpenSolaris */
	FIRE_CTRL_SET(sc, FO_XBC_INT_EN, ~0ULL);
	if (sc->sc_mode == FIRE_MODE_FIRE) {
		FIRE_CTRL_SET(sc, FIRE_JBUS_PAR_CTRL,
		    FIRE_JBUS_PAR_CTRL_P_EN);
		FIRE_CTRL_SET(sc, FIRE_JBC_FATAL_RST_EN,
		    ((1ULL << FIRE_JBC_FATAL_RST_EN_SPARE_P_INT_SHFT) &
		    FIRE_JBC_FATAL_RST_EN_SPARE_P_INT_MASK) |
		    FIRE_JBC_FATAL_RST_EN_MB_PEA_P_INT |
		    FIRE_JBC_FATAL_RST_EN_CPE_P_INT |
		    FIRE_JBC_FATAL_RST_EN_APE_P_INT |
		    FIRE_JBC_FATAL_RST_EN_PIO_CPE_INT |
		    FIRE_JBC_FATAL_RST_EN_JTCEEW_P_INT |
		    FIRE_JBC_FATAL_RST_EN_JTCEEI_P_INT |
		    FIRE_JBC_FATAL_RST_EN_JTCEER_P_INT);
		FIRE_CTRL_SET(sc, FIRE_JBC_CORE_BLOCK_INT_EN, ~0ULL);
	}

	/* TLU initialization */
	FIRE_PCI_SET(sc, FO_PCI_TLU_OEVENT_STAT_CLR,
	    FO_PCI_TLU_OEVENT_S_MASK | FO_PCI_TLU_OEVENT_P_MASK);
	/* not enabled by OpenSolaris */
	FIRE_PCI_SET(sc, FO_PCI_TLU_OEVENT_INT_EN,
	    FO_PCI_TLU_OEVENT_S_MASK | FO_PCI_TLU_OEVENT_P_MASK);
	FIRE_PCI_SET(sc, FO_PCI_TLU_UERR_STAT_CLR,
	    FO_PCI_TLU_UERR_INT_S_MASK | FO_PCI_TLU_UERR_INT_P_MASK);
	/* not enabled by OpenSolaris */
	FIRE_PCI_SET(sc, FO_PCI_TLU_UERR_INT_EN,
	    FO_PCI_TLU_UERR_INT_S_MASK | FO_PCI_TLU_UERR_INT_P_MASK);
	FIRE_PCI_SET(sc, FO_PCI_TLU_CERR_STAT_CLR,
	    FO_PCI_TLU_CERR_INT_S_MASK | FO_PCI_TLU_CERR_INT_P_MASK);
	/* not enabled by OpenSolaris */
	FIRE_PCI_SET(sc, FO_PCI_TLU_CERR_INT_EN,
	    FO_PCI_TLU_CERR_INT_S_MASK | FO_PCI_TLU_CERR_INT_P_MASK);
	val = FIRE_PCI_READ_8(sc, FO_PCI_TLU_CTRL) |
	    ((FO_PCI_TLU_CTRL_L0S_TIM_DFLT << FO_PCI_TLU_CTRL_L0S_TIM_SHFT) &
	    FO_PCI_TLU_CTRL_L0S_TIM_MASK) |
	    ((FO_PCI_TLU_CTRL_CFG_DFLT << FO_PCI_TLU_CTRL_CFG_SHFT) &
	    FO_PCI_TLU_CTRL_CFG_MASK);
	if (sc->sc_mode == FIRE_MODE_OBERON)
		val &= ~FO_PCI_TLU_CTRL_NWPR_EN;
	val |= FO_PCI_TLU_CTRL_CFG_REMAIN_DETECT_QUIET;
	FIRE_PCI_SET(sc, FO_PCI_TLU_CTRL, val);
	FIRE_PCI_SET(sc, FO_PCI_TLU_DEV_CTRL, 0);
	FIRE_PCI_SET(sc, FO_PCI_TLU_LNK_CTRL, FO_PCI_TLU_LNK_CTRL_CLK);

	/* DLU/LPU initialization */
	if (sc->sc_mode == FIRE_MODE_OBERON)
		FIRE_PCI_SET(sc, FO_PCI_LPU_INT_MASK, 0);
	else
		FIRE_PCI_SET(sc, FO_PCI_LPU_RST, 0);
	FIRE_PCI_SET(sc, FO_PCI_LPU_LNK_LYR_CFG,
	    FO_PCI_LPU_LNK_LYR_CFG_VC0_EN);
	FIRE_PCI_SET(sc, FO_PCI_LPU_FLW_CTRL_UPDT_CTRL,
	    FO_PCI_LPU_FLW_CTRL_UPDT_CTRL_FC0_NP_EN |
	    FO_PCI_LPU_FLW_CTRL_UPDT_CTRL_FC0_P_EN);
	if (sc->sc_mode == FIRE_MODE_OBERON)
		FIRE_PCI_SET(sc, FO_PCI_LPU_TXLNK_RPLY_TMR_THRS,
		    (OBERON_PCI_LPU_TXLNK_RPLY_TMR_THRS_DFLT <<
		    FO_PCI_LPU_TXLNK_RPLY_TMR_THRS_SHFT) &
		    FO_PCI_LPU_TXLNK_RPLY_TMR_THRS_MASK);
	else {
		switch ((FIRE_PCI_READ_8(sc, FO_PCI_TLU_LNK_STAT) &
		    FO_PCI_TLU_LNK_STAT_WDTH_MASK) >>
		    FO_PCI_TLU_LNK_STAT_WDTH_SHFT) {
		case 1:
			lw = 0;
			break;
		case 4:
			lw = 1;
			break;
		case 8:
			lw = 2;
			break;
		case 16:
			lw = 3;
			break;
		default:
			lw = 0;
		}
		mps = (FIRE_PCI_READ_8(sc, FO_PCI_TLU_CTRL) &
		    FO_PCI_TLU_CTRL_CFG_MPS_MASK) >>
		    FO_PCI_TLU_CTRL_CFG_MPS_SHFT;
		i = sizeof(fire_freq_nak_tmr_thrs) /
		    sizeof(*fire_freq_nak_tmr_thrs);
		if (mps >= i)
			mps = i - 1;
		FIRE_PCI_SET(sc, FO_PCI_LPU_TXLNK_FREQ_LAT_TMR_THRS,
		    (fire_freq_nak_tmr_thrs[mps][lw] <<
		    FO_PCI_LPU_TXLNK_FREQ_LAT_TMR_THRS_SHFT) &
		    FO_PCI_LPU_TXLNK_FREQ_LAT_TMR_THRS_MASK);
		FIRE_PCI_SET(sc, FO_PCI_LPU_TXLNK_RPLY_TMR_THRS,
		    (fire_rply_tmr_thrs[mps][lw] <<
		    FO_PCI_LPU_TXLNK_RPLY_TMR_THRS_SHFT) &
		    FO_PCI_LPU_TXLNK_RPLY_TMR_THRS_MASK);
		FIRE_PCI_SET(sc, FO_PCI_LPU_TXLNK_RTR_FIFO_PTR,
		    ((FO_PCI_LPU_TXLNK_RTR_FIFO_PTR_TL_DFLT <<
		    FO_PCI_LPU_TXLNK_RTR_FIFO_PTR_TL_SHFT) &
		    FO_PCI_LPU_TXLNK_RTR_FIFO_PTR_TL_MASK) |
		    ((FO_PCI_LPU_TXLNK_RTR_FIFO_PTR_HD_DFLT <<
		    FO_PCI_LPU_TXLNK_RTR_FIFO_PTR_HD_SHFT) &
		    FO_PCI_LPU_TXLNK_RTR_FIFO_PTR_HD_MASK));
		FIRE_PCI_SET(sc, FO_PCI_LPU_LTSSM_CFG2,
		    (FO_PCI_LPU_LTSSM_CFG2_12_TO_DFLT <<
		    FO_PCI_LPU_LTSSM_CFG2_12_TO_SHFT) &
		    FO_PCI_LPU_LTSSM_CFG2_12_TO_MASK);
		FIRE_PCI_SET(sc, FO_PCI_LPU_LTSSM_CFG3,
		    (FO_PCI_LPU_LTSSM_CFG3_2_TO_DFLT <<
		    FO_PCI_LPU_LTSSM_CFG3_2_TO_SHFT) &
		    FO_PCI_LPU_LTSSM_CFG3_2_TO_MASK);
		FIRE_PCI_SET(sc, FO_PCI_LPU_LTSSM_CFG4,
		    ((FO_PCI_LPU_LTSSM_CFG4_DATA_RATE_DFLT <<
		    FO_PCI_LPU_LTSSM_CFG4_DATA_RATE_SHFT) &
		    FO_PCI_LPU_LTSSM_CFG4_DATA_RATE_MASK) |
		    ((FO_PCI_LPU_LTSSM_CFG4_N_FTS_DFLT <<
		    FO_PCI_LPU_LTSSM_CFG4_N_FTS_SHFT) &
		    FO_PCI_LPU_LTSSM_CFG4_N_FTS_MASK));
		FIRE_PCI_SET(sc, FO_PCI_LPU_LTSSM_CFG5, 0);
	}

	/* ILU initialization */
	FIRE_PCI_SET(sc, FO_PCI_ILU_ERR_STAT_CLR, ~0ULL);
	/* not enabled by OpenSolaris */
	FIRE_PCI_SET(sc, FO_PCI_ILU_INT_EN, ~0ULL);

	/* IMU initialization */
	FIRE_PCI_SET(sc, FO_PCI_IMU_ERR_STAT_CLR, ~0ULL);
	FIRE_PCI_SET(sc, FO_PCI_IMU_INT_EN,
	    FIRE_PCI_READ_8(sc, FO_PCI_IMU_INT_EN) &
	    ~(FO_PCI_IMU_ERR_INT_FATAL_MES_NOT_EN_S |
	    FO_PCI_IMU_ERR_INT_NFATAL_MES_NOT_EN_S |
	    FO_PCI_IMU_ERR_INT_COR_MES_NOT_EN_S |
	    FO_PCI_IMU_ERR_INT_FATAL_MES_NOT_EN_P |
	    FO_PCI_IMU_ERR_INT_NFATAL_MES_NOT_EN_P |
	    FO_PCI_IMU_ERR_INT_COR_MES_NOT_EN_P));

	/* MMU initialization */
	FIRE_PCI_SET(sc, FO_PCI_MMU_ERR_STAT_CLR,
	    FO_PCI_MMU_ERR_INT_S_MASK | FO_PCI_MMU_ERR_INT_P_MASK);
	/* not enabled by OpenSolaris */
	FIRE_PCI_SET(sc, FO_PCI_MMU_INT_EN,
	    FO_PCI_MMU_ERR_INT_S_MASK | FO_PCI_MMU_ERR_INT_P_MASK);

	/* DMC initialization */
	FIRE_PCI_SET(sc, FO_PCI_DMC_CORE_BLOCK_INT_EN, ~0ULL);
	FIRE_PCI_SET(sc, FO_PCI_DMC_DBG_SEL_PORTA, 0);
	FIRE_PCI_SET(sc, FO_PCI_DMC_DBG_SEL_PORTB, 0);

	/* PEC initialization */
	FIRE_PCI_SET(sc, FO_PCI_PEC_CORE_BLOCK_INT_EN, ~0ULL);

	/* Establish handlers for interesting interrupts. */
	if ((ino_bitmap & (1ULL << FO_DMC_PEC_INO)) != 0)
		fire_set_intr(sc, 1, FO_DMC_PEC_INO, fire_dmc_pec, sc);
	if ((ino_bitmap & (1ULL << FO_XCB_INO)) != 0)
		fire_set_intr(sc, 0, FO_XCB_INO, fire_xcb, sc);

	/* MSI/MSI-X support */
	if (OF_getprop(node, "#msi", &sc->sc_msi_count,
	    sizeof(sc->sc_msi_count)) == -1)
		panic("%s: could not determine MSI count", __func__);
	if (OF_getprop(node, "msi-ranges", &msi_ranges,
	    sizeof(msi_ranges)) == -1)
		sc->sc_msi_first = 0;
	else
		sc->sc_msi_first = msi_ranges.first;
	if (OF_getprop(node, "msi-data-mask", &sc->sc_msi_data_mask,
	    sizeof(sc->sc_msi_data_mask)) == -1)
		panic("%s: could not determine MSI data mask", __func__);
	if (OF_getprop(node, "msix-data-width", &sc->sc_msix_data_width,
	    sizeof(sc->sc_msix_data_width)) > 0)
		sc->sc_flags |= FIRE_MSIX;
	if (OF_getprop(node, "msi-address-ranges", &msi_addr_ranges,
	    sizeof(msi_addr_ranges)) == -1)
		panic("%s: could not determine MSI address ranges", __func__);
	sc->sc_msi_addr32 = OFW_PCI_MSI_ADDR_RANGE_32(&msi_addr_ranges);
	sc->sc_msi_addr64 = OFW_PCI_MSI_ADDR_RANGE_64(&msi_addr_ranges);
	if (OF_getprop(node, "#msi-eqs", &sc->sc_msiq_count,
	    sizeof(sc->sc_msiq_count)) == -1)
		panic("%s: could not determine MSI event queue count",
		    __func__);
	if (OF_getprop(node, "msi-eq-size", &sc->sc_msiq_size,
	    sizeof(sc->sc_msiq_size)) == -1)
		panic("%s: could not determine MSI event queue size",
		    __func__);
	if (OF_getprop(node, "msi-eq-to-devino", &msi_eq_to_devino,
	    sizeof(msi_eq_to_devino)) == -1 &&
	    OF_getprop(node, "msi-eq-devino", &msi_eq_to_devino,
	    sizeof(msi_eq_to_devino)) == -1) {
		sc->sc_msiq_first = 0;
		sc->sc_msiq_ino_first = FO_EQ_FIRST_INO;
	} else {
		sc->sc_msiq_first = msi_eq_to_devino.eq_first;
		sc->sc_msiq_ino_first = msi_eq_to_devino.devino_first;
	}
	if (sc->sc_msiq_ino_first < FO_EQ_FIRST_INO ||
	    sc->sc_msiq_ino_first + sc->sc_msiq_count - 1 > FO_EQ_LAST_INO)
		panic("%s: event queues exceed INO range", __func__);
	sc->sc_msi_bitmap = malloc(roundup2(sc->sc_msi_count, NBBY) / NBBY,
	    M_DEVBUF, M_NOWAIT | M_ZERO);
	if (sc->sc_msi_bitmap == NULL)
		panic("%s: could not malloc MSI bitmap", __func__);
	sc->sc_msi_msiq_table = malloc(sc->sc_msi_count *
	    sizeof(*sc->sc_msi_msiq_table), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (sc->sc_msi_msiq_table == NULL)
		panic("%s: could not malloc MSI-MSI event queue table",
		    __func__);
	sc->sc_msiq_bitmap = malloc(roundup2(sc->sc_msiq_count, NBBY) / NBBY,
	    M_DEVBUF, M_NOWAIT | M_ZERO);
	if (sc->sc_msiq_bitmap == NULL)
		panic("%s: could not malloc MSI event queue bitmap", __func__);
	j = FO_EQ_RECORD_SIZE * FO_EQ_NRECORDS * sc->sc_msiq_count;
	sc->sc_msiq = contigmalloc(j, M_DEVBUF, M_NOWAIT, 0, ~0UL,
	    FO_EQ_ALIGNMENT, 0);
	if (sc->sc_msiq == NULL)
		panic("%s: could not contigmalloc MSI event queue", __func__);
	memset(sc->sc_msiq, 0, j);
	FIRE_PCI_SET(sc, FO_PCI_EQ_BASE_ADDR, FO_PCI_EQ_BASE_ADDR_BYPASS |
	    (pmap_kextract((vm_offset_t)sc->sc_msiq) &
	    FO_PCI_EQ_BASE_ADDR_MASK));
	for (i = 0; i < sc->sc_msi_count; i++) {
		j = (i + sc->sc_msi_first) << 3;
		FIRE_PCI_WRITE_8(sc, FO_PCI_MSI_MAP_BASE + j,
		    FIRE_PCI_READ_8(sc, FO_PCI_MSI_MAP_BASE + j) &
		    ~FO_PCI_MSI_MAP_V);
	}
	for (i = 0; i < sc->sc_msiq_count; i++) {
		j = i + sc->sc_msiq_ino_first;
		if ((ino_bitmap & (1ULL << j)) == 0) {
			mtx_lock(&sc->sc_msi_mtx);
			setbit(sc->sc_msiq_bitmap, i);
			mtx_unlock(&sc->sc_msi_mtx);
		}
		fmqa = intr_vectors[INTMAP_VEC(sc->sc_ign, j)].iv_icarg;
		mtx_init(&fmqa->fmqa_mtx, "msiq_mtx", NULL, MTX_SPIN);
		fmqa->fmqa_base =
		    (struct fo_msiq_record *)((caddr_t)sc->sc_msiq +
		    (FO_EQ_RECORD_SIZE * FO_EQ_NRECORDS * i));
		j = i + sc->sc_msiq_first;
		fmqa->fmqa_msiq = j;
		j <<= 3;
		fmqa->fmqa_head = FO_PCI_EQ_HD_BASE + j;
		fmqa->fmqa_tail = FO_PCI_EQ_TL_BASE + j;
		FIRE_PCI_WRITE_8(sc, FO_PCI_EQ_CTRL_CLR_BASE + j,
		    FO_PCI_EQ_CTRL_CLR_COVERR | FO_PCI_EQ_CTRL_CLR_E2I |
		    FO_PCI_EQ_CTRL_CLR_DIS);
		FIRE_PCI_WRITE_8(sc, fmqa->fmqa_tail,
		    (0 << FO_PCI_EQ_TL_SHFT) & FO_PCI_EQ_TL_MASK);
		FIRE_PCI_WRITE_8(sc, fmqa->fmqa_head,
		    (0 << FO_PCI_EQ_HD_SHFT) & FO_PCI_EQ_HD_MASK);
	}
	FIRE_PCI_SET(sc, FO_PCI_MSI_32_BIT_ADDR, sc->sc_msi_addr32 &
	    FO_PCI_MSI_32_BIT_ADDR_MASK);
	FIRE_PCI_SET(sc, FO_PCI_MSI_64_BIT_ADDR, sc->sc_msi_addr64 &
	    FO_PCI_MSI_64_BIT_ADDR_MASK);

	/*
	 * Establish a handler for interesting PCIe messages and disable
	 * unintersting ones.
	 */
	mtx_lock(&sc->sc_msi_mtx);
	for (i = 0; i < sc->sc_msiq_count; i++) {
		if (isclr(sc->sc_msiq_bitmap, i) != 0) {
			j = i;
			break;
		}
	}
	if (i == sc->sc_msiq_count) {
		mtx_unlock(&sc->sc_msi_mtx);
		panic("%s: no spare event queue for PCIe messages", __func__);
	}
	setbit(sc->sc_msiq_bitmap, j);
	mtx_unlock(&sc->sc_msi_mtx);
	i = INTMAP_VEC(sc->sc_ign, j + sc->sc_msiq_ino_first);
	if (bus_set_resource(dev, SYS_RES_IRQ, 2, i, 1) != 0)
		panic("%s: failed to add interrupt for PCIe messages",
		    __func__);
	fire_set_intr(sc, 2, INTINO(i), fire_pcie, intr_vectors[i].iv_icarg);
	j += sc->sc_msiq_first;
	/*
	 * "Please note that setting the EQNUM field to a value larger than
	 * 35 will yield unpredictable results."
	 */
	if (j > 35)
		panic("%s: invalid queue for PCIe messages (%d)",
		    __func__, j);
	FIRE_PCI_SET(sc, FO_PCI_ERR_COR, FO_PCI_ERR_PME_V |
	    ((j << FO_PCI_ERR_PME_EQNUM_SHFT) & FO_PCI_ERR_PME_EQNUM_MASK));
	FIRE_PCI_SET(sc, FO_PCI_ERR_NONFATAL, FO_PCI_ERR_PME_V |
	    ((j << FO_PCI_ERR_PME_EQNUM_SHFT) & FO_PCI_ERR_PME_EQNUM_MASK));
	FIRE_PCI_SET(sc, FO_PCI_ERR_FATAL, FO_PCI_ERR_PME_V |
	    ((j << FO_PCI_ERR_PME_EQNUM_SHFT) & FO_PCI_ERR_PME_EQNUM_MASK));
	FIRE_PCI_SET(sc, FO_PCI_PM_PME, 0);
	FIRE_PCI_SET(sc, FO_PCI_PME_TO_ACK, 0);
	FIRE_PCI_WRITE_8(sc, FO_PCI_EQ_CTRL_SET_BASE + (j << 3),
	    FO_PCI_EQ_CTRL_SET_EN);

#define	TC_COUNTER_MAX_MASK	0xffffffff

	/*
	 * Setup JBC/UBC performance counter 0 in bus cycle counting
	 * mode as timecounter.
	 */
	if (device_get_unit(dev) == 0) {
		FIRE_CTRL_SET(sc, FO_XBC_PRF_CNT0, 0);
		FIRE_CTRL_SET(sc, FO_XBC_PRF_CNT1, 0);
		FIRE_CTRL_SET(sc, FO_XBC_PRF_CNT_SEL,
		    (FO_XBC_PRF_CNT_NONE << FO_XBC_PRF_CNT_CNT1_SHFT) |
		    (FO_XBC_PRF_CNT_XB_CLK << FO_XBC_PRF_CNT_CNT0_SHFT));
		tc = malloc(sizeof(*tc), M_DEVBUF, M_NOWAIT | M_ZERO);
		if (tc == NULL)
			panic("%s: could not malloc timecounter", __func__);
		tc->tc_get_timecount = fire_get_timecount;
		tc->tc_counter_mask = TC_COUNTER_MAX_MASK;
		if (OF_getprop(OF_peer(0), "clock-frequency", &prop,
		    sizeof(prop)) == -1)
			panic("%s: could not determine clock frequency",
			    __func__);
		tc->tc_frequency = prop;
		tc->tc_name = strdup(device_get_nameunit(dev), M_DEVBUF);
		tc->tc_priv = sc;
		/*
		 * Due to initial problems with the JBus-driven performance
		 * counters not advancing which might be firmware dependent
		 * ensure that it actually works.
		 */
		if (fire_get_timecount(tc) - fire_get_timecount(tc) != 0)
			tc->tc_quality = FIRE_PERF_CNT_QLTY;
		else
			tc->tc_quality = -FIRE_PERF_CNT_QLTY;
		tc_init(tc);
	}

	/*
	 * Set up the IOMMU.  Both Fire and Oberon have one per PBM, but
	 * neither has a streaming buffer.
	 */
	memcpy(&sc->sc_dma_methods, &iommu_dma_methods,
	    sizeof(sc->sc_dma_methods));
	sc->sc_is.is_flags = IOMMU_FIRE | IOMMU_PRESERVE_PROM;
	if (sc->sc_mode == FIRE_MODE_OBERON) {
		sc->sc_is.is_flags |= IOMMU_FLUSH_CACHE;
		sc->sc_is.is_pmaxaddr = IOMMU_MAXADDR(OBERON_IOMMU_BITS);
	} else {
		sc->sc_dma_methods.dm_dmamap_sync = fire_dmamap_sync;
		sc->sc_is.is_pmaxaddr = IOMMU_MAXADDR(FIRE_IOMMU_BITS);
	}
	sc->sc_is.is_sb[0] = sc->sc_is.is_sb[1] = 0;
	/* Punch in our copies. */
	sc->sc_is.is_bustag = rman_get_bustag(sc->sc_mem_res[FIRE_PCI]);
	sc->sc_is.is_bushandle = rman_get_bushandle(sc->sc_mem_res[FIRE_PCI]);
	sc->sc_is.is_iommu = FO_PCI_MMU;
	val = FIRE_PCI_READ_8(sc, FO_PCI_MMU + IMR_CTL);
	iommu_init(device_get_nameunit(dev), &sc->sc_is, 7, -1, 0);
#ifdef FIRE_DEBUG
	device_printf(dev, "FO_PCI_MMU + IMR_CTL 0x%016llx -> 0x%016llx\n",
	    (long long unsigned)val, (long long unsigned)sc->sc_is.is_cr);
#endif
	/* Create our DMA tag. */
	if (bus_dma_tag_create(bus_get_dma_tag(dev), 8, 0x100000000,
	    sc->sc_is.is_pmaxaddr, ~0, NULL, NULL, sc->sc_is.is_pmaxaddr,
	    0xff, 0xffffffff, 0, NULL, NULL, &dmat) != 0)
		panic("%s: could not create PCI DMA tag", __func__);
	dmat->dt_cookie = &sc->sc_is;
	dmat->dt_mt = &sc->sc_dma_methods;

	if (ofw_pci_attach_common(dev, dmat, FO_IO_SIZE, FO_MEM_SIZE) != 0)
		panic("%s: ofw_pci_attach_common() failed", __func__);

#define	FIRE_SYSCTL_ADD_UINT(name, arg, desc)				\
	SYSCTL_ADD_UINT(device_get_sysctl_ctx(dev),			\
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO,	\
	    (name), CTLFLAG_RD, (arg), 0, (desc))

	FIRE_SYSCTL_ADD_UINT("ilu_err", &sc->sc_stats_ilu_err,
	    "ILU unknown errors");
	FIRE_SYSCTL_ADD_UINT("jbc_ce_async", &sc->sc_stats_jbc_ce_async,
	    "JBC correctable errors");
	FIRE_SYSCTL_ADD_UINT("jbc_unsol_int", &sc->sc_stats_jbc_unsol_int,
	    "JBC unsolicited interrupt ACK/NACK errors");
	FIRE_SYSCTL_ADD_UINT("jbc_unsol_rd", &sc->sc_stats_jbc_unsol_rd,
	    "JBC unsolicited read response errors");
	FIRE_SYSCTL_ADD_UINT("mmu_err", &sc->sc_stats_mmu_err, "MMU errors");
	FIRE_SYSCTL_ADD_UINT("tlu_ce", &sc->sc_stats_tlu_ce,
	    "DLU/TLU correctable errors");
	FIRE_SYSCTL_ADD_UINT("tlu_oe_non_fatal",
	    &sc->sc_stats_tlu_oe_non_fatal,
	    "DLU/TLU other event non-fatal errors summary");
	FIRE_SYSCTL_ADD_UINT("tlu_oe_rx_err", &sc->sc_stats_tlu_oe_rx_err,
	    "DLU/TLU receive other event errors");
	FIRE_SYSCTL_ADD_UINT("tlu_oe_tx_err", &sc->sc_stats_tlu_oe_tx_err,
	    "DLU/TLU transmit other event errors");
	FIRE_SYSCTL_ADD_UINT("ubc_dmardue", &sc->sc_stats_ubc_dmardue,
	    "UBC DMARDUE erros");

#undef FIRE_SYSCTL_ADD_UINT

	device_add_child(dev, "pci", -1);
	return (bus_generic_attach(dev));
}

static void
fire_set_intr(struct fire_softc *sc, u_int index, u_int ino,
    driver_filter_t handler, void *arg)
{
	u_long vec;
	int rid;

	rid = index;
	sc->sc_irq_res[index] = bus_alloc_resource_any(sc->sc_dev,
	    SYS_RES_IRQ, &rid, RF_ACTIVE);
	if (sc->sc_irq_res[index] == NULL ||
	    INTINO(vec = rman_get_start(sc->sc_irq_res[index])) != ino ||
	    INTIGN(vec) != sc->sc_ign ||
	    intr_vectors[vec].iv_ic != &fire_ic ||
	    bus_setup_intr(sc->sc_dev, sc->sc_irq_res[index],
	    INTR_TYPE_MISC | INTR_BRIDGE, handler, NULL, arg,
	    &sc->sc_ihand[index]) != 0)
		panic("%s: failed to set up interrupt %d", __func__, index);
}

static int
fire_intr_register(struct fire_softc *sc, u_int ino)
{
	struct fire_icarg *fica;
	bus_addr_t intrclr, intrmap;
	int error;

	if (fire_get_intrmap(sc, ino, &intrmap, &intrclr) == 0)
		return (ENXIO);
	fica = malloc((ino >= FO_EQ_FIRST_INO && ino <= FO_EQ_LAST_INO) ?
	    sizeof(struct fire_msiqarg) : sizeof(struct fire_icarg), M_DEVBUF,
	    M_NOWAIT | M_ZERO);
	if (fica == NULL)
		return (ENOMEM);
	fica->fica_sc = sc;
	fica->fica_map = intrmap;
	fica->fica_clr = intrclr;
	error = (intr_controller_register(INTMAP_VEC(sc->sc_ign, ino),
	    &fire_ic, fica));
	if (error != 0)
		free(fica, M_DEVBUF);
	return (error);
}

static int
fire_get_intrmap(struct fire_softc *sc, u_int ino, bus_addr_t *intrmapptr,
    bus_addr_t *intrclrptr)
{

	if (ino > FO_MAX_INO) {
		device_printf(sc->sc_dev, "out of range INO %d requested\n",
		    ino);
		return (0);
	}

	ino <<= 3;
	if (intrmapptr != NULL)
		*intrmapptr = FO_PCI_INT_MAP_BASE + ino;
	if (intrclrptr != NULL)
		*intrclrptr = FO_PCI_INT_CLR_BASE + ino;
	return (1);
}

/*
 * Interrupt handlers
 */
static int
fire_dmc_pec(void *arg)
{
	struct fire_softc *sc;
	device_t dev;
	uint64_t cestat, dmcstat, ilustat, imustat, mcstat, mmustat, mmutfar;
	uint64_t mmutfsr, oestat, pecstat, uestat, val;
	u_int fatal, oenfatal;

	fatal = 0;
	sc = arg;
	dev = sc->sc_dev;
	mtx_lock_spin(&sc->sc_pcib_mtx);
	mcstat = FIRE_PCI_READ_8(sc, FO_PCI_MULTI_CORE_ERR_STAT);
	if ((mcstat & FO_PCI_MULTI_CORE_ERR_STAT_DMC) != 0) {
		dmcstat = FIRE_PCI_READ_8(sc, FO_PCI_DMC_CORE_BLOCK_ERR_STAT);
		if ((dmcstat & FO_PCI_DMC_CORE_BLOCK_INT_EN_IMU) != 0) {
			imustat = FIRE_PCI_READ_8(sc, FO_PCI_IMU_INT_STAT);
			device_printf(dev, "IMU error %#llx\n",
			    (unsigned long long)imustat);
			if ((imustat &
			    FO_PCI_IMU_ERR_INT_EQ_NOT_EN_P) != 0) {
				fatal = 1;
				val = FIRE_PCI_READ_8(sc,
				    FO_PCI_IMU_SCS_ERR_LOG);
				device_printf(dev, "SCS error log %#llx\n",
				    (unsigned long long)val);
			}
			if ((imustat & FO_PCI_IMU_ERR_INT_EQ_OVER_P) != 0) {
				fatal = 1;
				val = FIRE_PCI_READ_8(sc,
				    FO_PCI_IMU_EQS_ERR_LOG);
				device_printf(dev, "EQS error log %#llx\n",
				    (unsigned long long)val);
			}
			if ((imustat & (FO_PCI_IMU_ERR_INT_MSI_MAL_ERR_P |
			    FO_PCI_IMU_ERR_INT_MSI_PAR_ERR_P |
			    FO_PCI_IMU_ERR_INT_PMEACK_MES_NOT_EN_P |
			    FO_PCI_IMU_ERR_INT_PMPME_MES_NOT_EN_P |
			    FO_PCI_IMU_ERR_INT_FATAL_MES_NOT_EN_P |
			    FO_PCI_IMU_ERR_INT_NFATAL_MES_NOT_EN_P |
			    FO_PCI_IMU_ERR_INT_COR_MES_NOT_EN_P |
			    FO_PCI_IMU_ERR_INT_MSI_NOT_EN_P)) != 0) {
				fatal = 1;
				val = FIRE_PCI_READ_8(sc,
				    FO_PCI_IMU_RDS_ERR_LOG);
				device_printf(dev, "RDS error log %#llx\n",
				    (unsigned long long)val);
			}
		}
		if ((dmcstat & FO_PCI_DMC_CORE_BLOCK_INT_EN_MMU) != 0) {
			fatal = 1;
			mmustat = FIRE_PCI_READ_8(sc, FO_PCI_MMU_INT_STAT);
			mmutfar = FIRE_PCI_READ_8(sc,
			    FO_PCI_MMU_TRANS_FAULT_ADDR);
			mmutfsr = FIRE_PCI_READ_8(sc,
			    FO_PCI_MMU_TRANS_FAULT_STAT);
			if ((mmustat & (FO_PCI_MMU_ERR_INT_TBW_DPE_P |
			    FO_PCI_MMU_ERR_INT_TBW_ERR_P |
			    FO_PCI_MMU_ERR_INT_TBW_UDE_P |
			    FO_PCI_MMU_ERR_INT_TBW_DME_P |
			    FO_PCI_MMU_ERR_INT_TTC_CAE_P |
			    FIRE_PCI_MMU_ERR_INT_TTC_DPE_P |
			    OBERON_PCI_MMU_ERR_INT_TTC_DUE_P |
			    FO_PCI_MMU_ERR_INT_TRN_ERR_P)) != 0)
				fatal = 1;
			else {
				sc->sc_stats_mmu_err++;
				FIRE_PCI_WRITE_8(sc, FO_PCI_MMU_ERR_STAT_CLR,
				    mmustat);
			}
			device_printf(dev,
			    "MMU error %#llx: TFAR %#llx TFSR %#llx\n",
			    (unsigned long long)mmustat,
			    (unsigned long long)mmutfar,
			    (unsigned long long)mmutfsr);
		}
	}
	if ((mcstat & FO_PCI_MULTI_CORE_ERR_STAT_PEC) != 0) {
		pecstat = FIRE_PCI_READ_8(sc, FO_PCI_PEC_CORE_BLOCK_INT_STAT);
		if ((pecstat & FO_PCI_PEC_CORE_BLOCK_INT_STAT_UERR) != 0) {
			fatal = 1;
			uestat = FIRE_PCI_READ_8(sc,
			    FO_PCI_TLU_UERR_INT_STAT);
			device_printf(dev,
			    "DLU/TLU uncorrectable error %#llx\n",
			    (unsigned long long)uestat);
			if ((uestat & (FO_PCI_TLU_UERR_INT_UR_P |
			    OBERON_PCI_TLU_UERR_INT_POIS_P |
			    FO_PCI_TLU_UERR_INT_MFP_P |
			    FO_PCI_TLU_UERR_INT_ROF_P |
			    FO_PCI_TLU_UERR_INT_UC_P |
			    FIRE_PCI_TLU_UERR_INT_PP_P |
			    OBERON_PCI_TLU_UERR_INT_POIS_P)) != 0) {
				val = FIRE_PCI_READ_8(sc,
				    FO_PCI_TLU_RX_UERR_HDR1_LOG);
				device_printf(dev,
				    "receive header log %#llx\n",
				    (unsigned long long)val);
				val = FIRE_PCI_READ_8(sc,
				    FO_PCI_TLU_RX_UERR_HDR2_LOG);
				device_printf(dev,
				    "receive header log 2 %#llx\n",
				    (unsigned long long)val);
			}
			if ((uestat & FO_PCI_TLU_UERR_INT_CTO_P) != 0) {
				val = FIRE_PCI_READ_8(sc,
				    FO_PCI_TLU_TX_UERR_HDR1_LOG);
				device_printf(dev,
				    "transmit header log %#llx\n",
				    (unsigned long long)val);
				val = FIRE_PCI_READ_8(sc,
				    FO_PCI_TLU_TX_UERR_HDR2_LOG);
				device_printf(dev,
				    "transmit header log 2 %#llx\n",
				    (unsigned long long)val);
			}
			if ((uestat & FO_PCI_TLU_UERR_INT_DLP_P) != 0) {
				val = FIRE_PCI_READ_8(sc,
				    FO_PCI_LPU_LNK_LYR_INT_STAT);
				device_printf(dev,
				    "link layer interrupt and status %#llx\n",
				    (unsigned long long)val);
			}
			if ((uestat & FO_PCI_TLU_UERR_INT_TE_P) != 0) {
				val = FIRE_PCI_READ_8(sc,
				    FO_PCI_LPU_PHY_LYR_INT_STAT);
				device_printf(dev,
				    "phy layer interrupt and status %#llx\n",
				    (unsigned long long)val);
			}
		}
		if ((pecstat & FO_PCI_PEC_CORE_BLOCK_INT_STAT_CERR) != 0) {
			sc->sc_stats_tlu_ce++;
			cestat = FIRE_PCI_READ_8(sc,
			    FO_PCI_TLU_CERR_INT_STAT);
			device_printf(dev,
			    "DLU/TLU correctable error %#llx\n",
			    (unsigned long long)cestat);
			val = FIRE_PCI_READ_8(sc,
			    FO_PCI_LPU_LNK_LYR_INT_STAT);
			device_printf(dev,
			    "link layer interrupt and status %#llx\n",
			    (unsigned long long)val);
			if ((cestat & FO_PCI_TLU_CERR_INT_RE_P) != 0) {
				FIRE_PCI_WRITE_8(sc,
				    FO_PCI_LPU_LNK_LYR_INT_STAT, val);
				val = FIRE_PCI_READ_8(sc,
				    FO_PCI_LPU_PHY_LYR_INT_STAT);
				device_printf(dev,
				    "phy layer interrupt and status %#llx\n",
				    (unsigned long long)val);
			}
			FIRE_PCI_WRITE_8(sc, FO_PCI_TLU_CERR_STAT_CLR,
			    cestat);
		}
		if ((pecstat & FO_PCI_PEC_CORE_BLOCK_INT_STAT_OEVENT) != 0) {
			oenfatal = 0;
			oestat = FIRE_PCI_READ_8(sc,
			    FO_PCI_TLU_OEVENT_INT_STAT);
			device_printf(dev, "DLU/TLU other event %#llx\n",
			    (unsigned long long)oestat);
			if ((oestat & (FO_PCI_TLU_OEVENT_MFC_P |
			    FO_PCI_TLU_OEVENT_MRC_P |
			    FO_PCI_TLU_OEVENT_WUC_P |
			    FO_PCI_TLU_OEVENT_RUC_P |
			    FO_PCI_TLU_OEVENT_CRS_P)) != 0) {
				val = FIRE_PCI_READ_8(sc,
				    FO_PCI_TLU_RX_OEVENT_HDR1_LOG);
				device_printf(dev,
				    "receive header log %#llx\n",
				    (unsigned long long)val);
				val = FIRE_PCI_READ_8(sc,
				    FO_PCI_TLU_RX_OEVENT_HDR2_LOG);
				device_printf(dev,
				    "receive header log 2 %#llx\n",
				    (unsigned long long)val);
				if ((oestat & (FO_PCI_TLU_OEVENT_MFC_P |
				    FO_PCI_TLU_OEVENT_MRC_P |
				    FO_PCI_TLU_OEVENT_WUC_P |
				    FO_PCI_TLU_OEVENT_RUC_P)) != 0)
					fatal = 1;
				else {
					sc->sc_stats_tlu_oe_rx_err++;
					oenfatal = 1;
				}
			}
			if ((oestat & (FO_PCI_TLU_OEVENT_MFC_P |
			    FO_PCI_TLU_OEVENT_CTO_P |
			    FO_PCI_TLU_OEVENT_WUC_P |
			    FO_PCI_TLU_OEVENT_RUC_P)) != 0) {
				val = FIRE_PCI_READ_8(sc,
				    FO_PCI_TLU_TX_OEVENT_HDR1_LOG);
				device_printf(dev,
				    "transmit header log %#llx\n",
				    (unsigned long long)val);
				val = FIRE_PCI_READ_8(sc,
				    FO_PCI_TLU_TX_OEVENT_HDR2_LOG);
				device_printf(dev,
				    "transmit header log 2 %#llx\n",
				    (unsigned long long)val);
				if ((oestat & (FO_PCI_TLU_OEVENT_MFC_P |
				    FO_PCI_TLU_OEVENT_CTO_P |
				    FO_PCI_TLU_OEVENT_WUC_P |
				    FO_PCI_TLU_OEVENT_RUC_P)) != 0)
					fatal = 1;
				else {
					sc->sc_stats_tlu_oe_tx_err++;
					oenfatal = 1;
				}
			}
			if ((oestat & (FO_PCI_TLU_OEVENT_ERO_P |
			    FO_PCI_TLU_OEVENT_EMP_P |
			    FO_PCI_TLU_OEVENT_EPE_P |
			    FIRE_PCI_TLU_OEVENT_ERP_P |
			    OBERON_PCI_TLU_OEVENT_ERBU_P |
			    FIRE_PCI_TLU_OEVENT_EIP_P |
			    OBERON_PCI_TLU_OEVENT_EIUE_P)) != 0) {
				fatal = 1;
				val = FIRE_PCI_READ_8(sc,
				    FO_PCI_LPU_LNK_LYR_INT_STAT);
				device_printf(dev,
				    "link layer interrupt and status %#llx\n",
				    (unsigned long long)val);
			}
			if ((oestat & (FO_PCI_TLU_OEVENT_IIP_P |
			    FO_PCI_TLU_OEVENT_EDP_P |
			    FIRE_PCI_TLU_OEVENT_EHP_P |
			    OBERON_PCI_TLU_OEVENT_TLUEITMO_S |
			    FO_PCI_TLU_OEVENT_ERU_P)) != 0)
				fatal = 1;
			if ((oestat & (FO_PCI_TLU_OEVENT_NFP_P |
			    FO_PCI_TLU_OEVENT_LWC_P |
			    FO_PCI_TLU_OEVENT_LIN_P |
			    FO_PCI_TLU_OEVENT_LRS_P |
			    FO_PCI_TLU_OEVENT_LDN_P |
			    FO_PCI_TLU_OEVENT_LUP_P)) != 0)
				oenfatal = 1;
			if (oenfatal != 0) {
				sc->sc_stats_tlu_oe_non_fatal++;
				FIRE_PCI_WRITE_8(sc,
				    FO_PCI_TLU_OEVENT_STAT_CLR, oestat);
				if ((oestat & FO_PCI_TLU_OEVENT_LIN_P) != 0)
					FIRE_PCI_WRITE_8(sc,
					    FO_PCI_LPU_LNK_LYR_INT_STAT,
					    FIRE_PCI_READ_8(sc,
					    FO_PCI_LPU_LNK_LYR_INT_STAT));
			}
		}
		if ((pecstat & FO_PCI_PEC_CORE_BLOCK_INT_STAT_ILU) != 0) {
			ilustat = FIRE_PCI_READ_8(sc, FO_PCI_ILU_INT_STAT);
			device_printf(dev, "ILU error %#llx\n",
			    (unsigned long long)ilustat);
			if ((ilustat & (FIRE_PCI_ILU_ERR_INT_IHB_PE_P |
			    FIRE_PCI_ILU_ERR_INT_IHB_PE_P)) != 0)
			    fatal = 1;
			else {
				sc->sc_stats_ilu_err++;
				FIRE_PCI_WRITE_8(sc, FO_PCI_ILU_INT_STAT,
				    ilustat);
			}
		}
	}
	mtx_unlock_spin(&sc->sc_pcib_mtx);
	if (fatal != 0)
		panic("%s: fatal DMC/PEC error",
		    device_get_nameunit(sc->sc_dev));
	return (FILTER_HANDLED);
}

static int
fire_xcb(void *arg)
{
	struct fire_softc *sc;
	device_t dev;
	uint64_t errstat, intstat, val;
	u_int fatal;

	fatal = 0;
	sc = arg;
	dev = sc->sc_dev;
	mtx_lock_spin(&sc->sc_pcib_mtx);
	if (sc->sc_mode == FIRE_MODE_OBERON) {
		intstat = FIRE_CTRL_READ_8(sc, FO_XBC_INT_STAT);
		device_printf(dev, "UBC error: interrupt status %#llx\n",
		    (unsigned long long)intstat);
		if ((intstat & ~(OBERON_UBC_ERR_INT_DMARDUEB_P |
		    OBERON_UBC_ERR_INT_DMARDUEA_P)) != 0)
			fatal = 1;
		else
			sc->sc_stats_ubc_dmardue++;
		if (fatal != 0) {
			mtx_unlock_spin(&sc->sc_pcib_mtx);
			panic("%s: fatal UBC core block error",
			    device_get_nameunit(sc->sc_dev));
		} else {
			FIRE_CTRL_SET(sc, FO_XBC_ERR_STAT_CLR, ~0ULL);
			mtx_unlock_spin(&sc->sc_pcib_mtx);
		}
	} else {
		errstat = FIRE_CTRL_READ_8(sc, FIRE_JBC_CORE_BLOCK_ERR_STAT);
		if ((errstat & (FIRE_JBC_CORE_BLOCK_ERR_STAT_MERGE |
		    FIRE_JBC_CORE_BLOCK_ERR_STAT_JBCINT |
		    FIRE_JBC_CORE_BLOCK_ERR_STAT_DMCINT)) != 0) {
			intstat = FIRE_CTRL_READ_8(sc, FO_XBC_INT_STAT);
			device_printf(dev, "JBC interrupt status %#llx\n",
			    (unsigned long long)intstat);
			if ((intstat & FIRE_JBC_ERR_INT_EBUS_TO_P) != 0) {
				val = FIRE_CTRL_READ_8(sc,
				    FIRE_JBC_CSR_ERR_LOG);
				device_printf(dev, "CSR error log %#llx\n",
				    (unsigned long long)val);
			}
			if ((intstat & (FIRE_JBC_ERR_INT_UNSOL_RD_P |
			    FIRE_JBC_ERR_INT_UNSOL_INT_P)) != 0) {
				if ((intstat &
				    FIRE_JBC_ERR_INT_UNSOL_RD_P) != 0)
					sc->sc_stats_jbc_unsol_rd++;
				if ((intstat &
				    FIRE_JBC_ERR_INT_UNSOL_INT_P) != 0)
					sc->sc_stats_jbc_unsol_int++;
				val = FIRE_CTRL_READ_8(sc,
				    FIRE_DMCINT_IDC_ERR_LOG);
				device_printf(dev,
				    "DMCINT IDC error log %#llx\n",
				    (unsigned long long)val);
			}
			if ((intstat & (FIRE_JBC_ERR_INT_MB_PER_P |
			    FIRE_JBC_ERR_INT_MB_PEW_P)) != 0) {
				fatal = 1;
				val = FIRE_CTRL_READ_8(sc,
				    FIRE_MERGE_TRANS_ERR_LOG);
				device_printf(dev,
				    "merge transaction error log %#llx\n",
				    (unsigned long long)val);
			}
			if ((intstat & FIRE_JBC_ERR_INT_IJP_P) != 0) {
				fatal = 1;
				val = FIRE_CTRL_READ_8(sc,
				    FIRE_JBCINT_OTRANS_ERR_LOG);
				device_printf(dev,
				    "JBCINT out transaction error log "
				    "%#llx\n", (unsigned long long)val);
				val = FIRE_CTRL_READ_8(sc,
				    FIRE_JBCINT_OTRANS_ERR_LOG2);
				device_printf(dev,
				    "JBCINT out transaction error log 2 "
				    "%#llx\n", (unsigned long long)val);
			}
			if ((intstat & (FIRE_JBC_ERR_INT_UE_ASYN_P |
			    FIRE_JBC_ERR_INT_CE_ASYN_P |
			    FIRE_JBC_ERR_INT_JTE_P | FIRE_JBC_ERR_INT_JBE_P |
			    FIRE_JBC_ERR_INT_JUE_P |
			    FIRE_JBC_ERR_INT_ICISE_P |
			    FIRE_JBC_ERR_INT_WR_DPE_P |
			    FIRE_JBC_ERR_INT_RD_DPE_P |
			    FIRE_JBC_ERR_INT_ILL_BMW_P |
			    FIRE_JBC_ERR_INT_ILL_BMR_P |
			    FIRE_JBC_ERR_INT_BJC_P)) != 0) {
				if ((intstat & (FIRE_JBC_ERR_INT_UE_ASYN_P |
				    FIRE_JBC_ERR_INT_JTE_P |
				    FIRE_JBC_ERR_INT_JBE_P |
				    FIRE_JBC_ERR_INT_JUE_P |
				    FIRE_JBC_ERR_INT_ICISE_P |
				    FIRE_JBC_ERR_INT_WR_DPE_P |
				    FIRE_JBC_ERR_INT_RD_DPE_P |
				    FIRE_JBC_ERR_INT_ILL_BMW_P |
				    FIRE_JBC_ERR_INT_ILL_BMR_P |
				    FIRE_JBC_ERR_INT_BJC_P)) != 0)
					fatal = 1;
				else
					sc->sc_stats_jbc_ce_async++;
				val = FIRE_CTRL_READ_8(sc,
				    FIRE_JBCINT_ITRANS_ERR_LOG);
				device_printf(dev,
				    "JBCINT in transaction error log %#llx\n",
				    (unsigned long long)val);
				val = FIRE_CTRL_READ_8(sc,
				    FIRE_JBCINT_ITRANS_ERR_LOG2);
				device_printf(dev,
				    "JBCINT in transaction error log 2 "
				    "%#llx\n", (unsigned long long)val);
			}
			if ((intstat & (FIRE_JBC_ERR_INT_PIO_UNMAP_RD_P |
			    FIRE_JBC_ERR_INT_ILL_ACC_RD_P |
			    FIRE_JBC_ERR_INT_PIO_UNMAP_P |
			    FIRE_JBC_ERR_INT_PIO_DPE_P |
			    FIRE_JBC_ERR_INT_PIO_CPE_P |
			    FIRE_JBC_ERR_INT_ILL_ACC_P)) != 0) {
				fatal = 1;
				val = FIRE_CTRL_READ_8(sc,
				    FIRE_JBC_CSR_ERR_LOG);
				device_printf(dev,
				    "DMCINT ODCD error log %#llx\n",
				    (unsigned long long)val);
			}
			if ((intstat & (FIRE_JBC_ERR_INT_MB_PEA_P |
			    FIRE_JBC_ERR_INT_CPE_P | FIRE_JBC_ERR_INT_APE_P |
			    FIRE_JBC_ERR_INT_PIO_CPE_P |
			    FIRE_JBC_ERR_INT_JTCEEW_P |
			    FIRE_JBC_ERR_INT_JTCEEI_P |
			    FIRE_JBC_ERR_INT_JTCEER_P)) != 0) {
				fatal = 1;
				val = FIRE_CTRL_READ_8(sc,
				    FIRE_FATAL_ERR_LOG);
				device_printf(dev, "fatal error log %#llx\n",
				    (unsigned long long)val);
				val = FIRE_CTRL_READ_8(sc,
				    FIRE_FATAL_ERR_LOG2);
				device_printf(dev, "fatal error log 2 "
				    "%#llx\n", (unsigned long long)val);
			}
			if (fatal != 0) {
				mtx_unlock_spin(&sc->sc_pcib_mtx);
				panic("%s: fatal JBC core block error",
				    device_get_nameunit(sc->sc_dev));
			} else {
				FIRE_CTRL_SET(sc, FO_XBC_ERR_STAT_CLR, ~0ULL);
				mtx_unlock_spin(&sc->sc_pcib_mtx);
			}
		} else {
			mtx_unlock_spin(&sc->sc_pcib_mtx);
			panic("%s: unknown JCB core block error status %#llx",
			    device_get_nameunit(sc->sc_dev),
			    (unsigned long long)errstat);
		}
	}
	return (FILTER_HANDLED);
}

static int
fire_pcie(void *arg)
{
	struct fire_msiqarg *fmqa;
	struct fire_softc *sc;
	struct fo_msiq_record *qrec;
	device_t dev;
	uint64_t word0;
	u_int head, msg, msiq;

	fmqa = arg;
	sc = fmqa->fmqa_fica.fica_sc;
	dev = sc->sc_dev;
	msiq = fmqa->fmqa_msiq;
	mtx_lock_spin(&fmqa->fmqa_mtx);
	head = (FIRE_PCI_READ_8(sc, fmqa->fmqa_head) & FO_PCI_EQ_HD_MASK) >>
	    FO_PCI_EQ_HD_SHFT;
	qrec = &fmqa->fmqa_base[head];
	word0 = qrec->fomqr_word0;
	for (;;) {
		KASSERT((word0 & FO_MQR_WORD0_FMT_TYPE_MSG) != 0,
		    ("%s: received non-PCIe message in event queue %d "
		    "(word0 %#llx)", device_get_nameunit(dev), msiq,
		    (unsigned long long)word0));
		msg = (word0 & FO_MQR_WORD0_DATA0_MASK) >>
		    FO_MQR_WORD0_DATA0_SHFT;

#define	PCIE_MSG_CODE_ERR_COR		0x30
#define	PCIE_MSG_CODE_ERR_NONFATAL	0x31
#define	PCIE_MSG_CODE_ERR_FATAL		0x33

		if (msg == PCIE_MSG_CODE_ERR_COR)
			device_printf(dev, "correctable PCIe error\n");
		else if (msg == PCIE_MSG_CODE_ERR_NONFATAL ||
		    msg == PCIE_MSG_CODE_ERR_FATAL)
			panic("%s: %sfatal PCIe error",
			    device_get_nameunit(dev),
			    msg == PCIE_MSG_CODE_ERR_NONFATAL ? "non-" : "");
		else
			panic("%s: received unknown PCIe message %#x",
			    device_get_nameunit(dev), msg);
		qrec->fomqr_word0 &= ~FO_MQR_WORD0_FMT_TYPE_MASK;
		head = (head + 1) % sc->sc_msiq_size;
		qrec = &fmqa->fmqa_base[head];
		word0 = qrec->fomqr_word0;
		if (__predict_true((word0 & FO_MQR_WORD0_FMT_TYPE_MASK) == 0))
			break;
	}
	FIRE_PCI_WRITE_8(sc, fmqa->fmqa_head, (head & FO_PCI_EQ_HD_MASK) <<
	    FO_PCI_EQ_HD_SHFT);
	if ((FIRE_PCI_READ_8(sc, fmqa->fmqa_tail) &
	    FO_PCI_EQ_TL_OVERR) != 0) {
		device_printf(dev, "event queue %d overflow\n", msiq);
		msiq <<= 3;
		FIRE_PCI_WRITE_8(sc, FO_PCI_EQ_CTRL_CLR_BASE + msiq,
		    FIRE_PCI_READ_8(sc, FO_PCI_EQ_CTRL_CLR_BASE + msiq) |
		    FO_PCI_EQ_CTRL_CLR_COVERR);
	}
	mtx_unlock_spin(&fmqa->fmqa_mtx);
	return (FILTER_HANDLED);
}

static int
fire_maxslots(device_t dev)
{

	return (1);
}

static uint32_t
fire_read_config(device_t dev, u_int bus, u_int slot, u_int func, u_int reg,
    int width)
{

	return (ofw_pci_read_config_common(dev, PCIE_REGMAX, FO_CONF_OFF(bus,
	    slot, func, reg), bus, slot, func, reg, width));
}

static void
fire_write_config(device_t dev, u_int bus, u_int slot, u_int func, u_int reg,
    uint32_t val, int width)
{

	ofw_pci_write_config_common(dev, PCIE_REGMAX, FO_CONF_OFF(bus, slot,
	    func, reg), bus, slot, func, reg, val, width);
}

static int
fire_route_interrupt(device_t bridge, device_t dev, int pin)
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
fire_dmamap_sync(bus_dma_tag_t dt __unused, bus_dmamap_t map,
    bus_dmasync_op_t op)
{

	if ((map->dm_flags & DMF_LOADED) == 0)
		return;

	if ((op & BUS_DMASYNC_POSTREAD) != 0)
		ofw_pci_dmamap_sync_stst_order_common();
	else if ((op & BUS_DMASYNC_PREWRITE) != 0)
		membar(Sync);
}

static void
fire_intr_enable(void *arg)
{
	struct intr_vector *iv;
	struct fire_icarg *fica;
	struct fire_softc *sc;
	struct pcpu *pc;
	uint64_t mr;
	u_int ctrl, i;

	iv = arg;
	fica = iv->iv_icarg;
	sc = fica->fica_sc;
	mr = FO_PCI_IMAP_V;
	if (sc->sc_mode == FIRE_MODE_OBERON)
		mr |= (iv->iv_mid << OBERON_PCI_IMAP_T_DESTID_SHFT) &
		    OBERON_PCI_IMAP_T_DESTID_MASK;
	else
		mr |= (iv->iv_mid << FIRE_PCI_IMAP_T_JPID_SHFT) &
		    FIRE_PCI_IMAP_T_JPID_MASK;
	/*
	 * Given that all mondos for the same target are required to use the
	 * same interrupt controller we just use the CPU ID for indexing the
	 * latter.
	 */
	ctrl = 0;
	for (i = 0; i < mp_ncpus; ++i) {
		pc = pcpu_find(i);
		if (pc == NULL || iv->iv_mid != pc->pc_mid)
			continue;
		ctrl = pc->pc_cpuid % 4;
		break;
	}
	mr |= (1ULL << ctrl) << FO_PCI_IMAP_INT_CTRL_NUM_SHFT &
	    FO_PCI_IMAP_INT_CTRL_NUM_MASK;
	FIRE_PCI_WRITE_8(sc, fica->fica_map, mr);
}

static void
fire_intr_disable(void *arg)
{
	struct intr_vector *iv;
	struct fire_icarg *fica;
	struct fire_softc *sc;

	iv = arg;
	fica = iv->iv_icarg;
	sc = fica->fica_sc;
	FIRE_PCI_WRITE_8(sc, fica->fica_map,
	    FIRE_PCI_READ_8(sc, fica->fica_map) & ~FO_PCI_IMAP_V);
}

static void
fire_intr_assign(void *arg)
{
	struct intr_vector *iv;
	struct fire_icarg *fica;
	struct fire_softc *sc;
	uint64_t mr;

	iv = arg;
	fica = iv->iv_icarg;
	sc = fica->fica_sc;
	mr = FIRE_PCI_READ_8(sc, fica->fica_map);
	if ((mr & FO_PCI_IMAP_V) != 0) {
		FIRE_PCI_WRITE_8(sc, fica->fica_map, mr & ~FO_PCI_IMAP_V);
		FIRE_PCI_BARRIER(sc, fica->fica_map, 8,
		    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	}
	while (FIRE_PCI_READ_8(sc, fica->fica_clr) != INTCLR_IDLE)
		;
	if ((mr & FO_PCI_IMAP_V) != 0)
		fire_intr_enable(arg);
}

static void
fire_intr_clear(void *arg)
{
	struct intr_vector *iv;
	struct fire_icarg *fica;

	iv = arg;
	fica = iv->iv_icarg;
	FIRE_PCI_WRITE_8(fica->fica_sc, fica->fica_clr, INTCLR_IDLE);
}

/*
 * Given that the event queue implementation matches our current MD and MI
 * interrupt frameworks like square pegs fit into round holes we are generous
 * and use one event queue per MSI for now, which limits us to 35 MSIs/MSI-Xs
 * per Host-PCIe-bridge (we use one event queue for the PCIe error messages).
 * This seems tolerable as long as most devices just use one MSI/MSI-X anyway.
 * Adding knowledge about MSIs/MSI-Xs to the MD interrupt code should allow us
 * to decouple the 1:1 mapping at the cost of no longer being able to bind
 * MSIs/MSI-Xs to specific CPUs as we currently have no reliable way to
 * quiesce a device while we move its MSIs/MSI-Xs to another event queue.
 */

static int
fire_alloc_msi(device_t dev, device_t child, int count, int maxcount __unused,
    int *irqs)
{
	struct fire_softc *sc;
	u_int i, j, msiqrun;

	if (powerof2(count) == 0 || count > 32)
		return (EINVAL);

	sc = device_get_softc(dev);
	mtx_lock(&sc->sc_msi_mtx);
	msiqrun = 0;
	for (i = 0; i < sc->sc_msiq_count; i++) {
		for (j = i; j < i + count; j++) {
			if (isclr(sc->sc_msiq_bitmap, j) == 0)
				break;
		}
		if (j == i + count) {
			msiqrun = i;
			break;
		}
	}
	if (i == sc->sc_msiq_count) {
		mtx_unlock(&sc->sc_msi_mtx);
		return (ENXIO);
	}
	for (i = 0; i + count < sc->sc_msi_count; i += count) {
		for (j = i; j < i + count; j++)
			if (isclr(sc->sc_msi_bitmap, j) == 0)
				break;
		if (j == i + count) {
			for (j = 0; j < count; j++) {
				setbit(sc->sc_msiq_bitmap, msiqrun + j);
				setbit(sc->sc_msi_bitmap, i + j);
				sc->sc_msi_msiq_table[i + j] = msiqrun + j;
				irqs[j] = sc->sc_msi_first + i + j;
			}
			mtx_unlock(&sc->sc_msi_mtx);
			return (0);
		}
	}
	mtx_unlock(&sc->sc_msi_mtx);
	return (ENXIO);
}

static int
fire_release_msi(device_t dev, device_t child, int count, int *irqs)
{
	struct fire_softc *sc;
	u_int i;

	sc = device_get_softc(dev);
	mtx_lock(&sc->sc_msi_mtx);
	for (i = 0; i < count; i++) {
		clrbit(sc->sc_msiq_bitmap,
		    sc->sc_msi_msiq_table[irqs[i] - sc->sc_msi_first]);
		clrbit(sc->sc_msi_bitmap, irqs[i] - sc->sc_msi_first);
	}
	mtx_unlock(&sc->sc_msi_mtx);
	return (0);
}

static int
fire_alloc_msix(device_t dev, device_t child, int *irq)
{
	struct fire_softc *sc;
	int i, msiq;

	sc = device_get_softc(dev);
	if ((sc->sc_flags & FIRE_MSIX) == 0)
		return (ENXIO);
	mtx_lock(&sc->sc_msi_mtx);
	msiq = 0;
	for (i = 0; i < sc->sc_msiq_count; i++) {
		if (isclr(sc->sc_msiq_bitmap, i) != 0) {
			msiq = i;
			break;
		}
	}
	if (i == sc->sc_msiq_count) {
		mtx_unlock(&sc->sc_msi_mtx);
		return (ENXIO);
	}
	for (i = sc->sc_msi_count - 1; i >= 0; i--) {
		if (isclr(sc->sc_msi_bitmap, i) != 0) {
			setbit(sc->sc_msiq_bitmap, msiq);
			setbit(sc->sc_msi_bitmap, i);
			sc->sc_msi_msiq_table[i] = msiq;
			*irq = sc->sc_msi_first + i;
			mtx_unlock(&sc->sc_msi_mtx);
			return (0);
		}
	}
	mtx_unlock(&sc->sc_msi_mtx);
	return (ENXIO);
}

static int
fire_release_msix(device_t dev, device_t child, int irq)
{
	struct fire_softc *sc;

	sc = device_get_softc(dev);
	if ((sc->sc_flags & FIRE_MSIX) == 0)
		return (ENXIO);
	mtx_lock(&sc->sc_msi_mtx);
	clrbit(sc->sc_msiq_bitmap,
	    sc->sc_msi_msiq_table[irq - sc->sc_msi_first]);
	clrbit(sc->sc_msi_bitmap, irq - sc->sc_msi_first);
	mtx_unlock(&sc->sc_msi_mtx);
	return (0);
}

static int
fire_map_msi(device_t dev, device_t child, int irq, uint64_t *addr,
    uint32_t *data)
{
	struct fire_softc *sc;
	struct pci_devinfo *dinfo;

	sc = device_get_softc(dev);
	dinfo = device_get_ivars(child);
	if (dinfo->cfg.msi.msi_alloc > 0) {
		if ((irq & ~sc->sc_msi_data_mask) != 0) {
			device_printf(dev, "invalid MSI 0x%x\n", irq);
			return (EINVAL);
		}
	} else {
		if ((sc->sc_flags & FIRE_MSIX) == 0)
			return (ENXIO);
		if (fls(irq) > sc->sc_msix_data_width) {
			device_printf(dev, "invalid MSI-X 0x%x\n", irq);
			return (EINVAL);
		}
	}
	if (dinfo->cfg.msi.msi_alloc > 0 &&
	    (dinfo->cfg.msi.msi_ctrl & PCIM_MSICTRL_64BIT) == 0)
		*addr = sc->sc_msi_addr32;
	else
		*addr = sc->sc_msi_addr64;
	*data = irq;
	return (0);
}

static void
fire_msiq_handler(void *cookie)
{
	struct intr_vector *iv;
	struct fire_msiqarg *fmqa;

	iv = cookie;
	fmqa = iv->iv_icarg;
	/*
	 * Note that since fire_intr_clear() will clear the event queue
	 * interrupt after the handler associated with the MSI [sic] has
	 * been executed we have to protect the access to the event queue as
	 * otherwise nested event queue interrupts cause corruption of the
	 * event queue on MP machines.  Obviously especially when abandoning
	 * the 1:1 mapping it would be better to not clear the event queue
	 * interrupt after each handler invocation but only once when the
	 * outstanding MSIs have been processed but unfortunately that
	 * doesn't work well and leads to interrupt storms with controllers/
	 * drivers which don't mask interrupts while the handler is executed.
	 * Maybe delaying clearing the MSI until after the handler has been
	 * executed could be used to work around this but that's not the
	 * intended usage and might in turn cause lost MSIs.
	 */
	mtx_lock_spin(&fmqa->fmqa_mtx);
	fire_msiq_common(iv, fmqa);
	mtx_unlock_spin(&fmqa->fmqa_mtx);
}

static void
fire_msiq_filter(void *cookie)
{
	struct intr_vector *iv;
	struct fire_msiqarg *fmqa;

	iv = cookie;
	fmqa = iv->iv_icarg;
	/*
	 * For filters we don't use fire_intr_clear() since it would clear
	 * the event queue interrupt while we're still processing the event
	 * queue as filters and associated post-filter handler are executed
	 * directly, which in turn would lead to lost MSIs.  So we clear the
	 * event queue interrupt only once after processing the event queue.
	 * Given that this still guarantees the filters to not be executed
	 * concurrently and no other CPU can clear the event queue interrupt
	 * while the event queue is still processed, we don't even need to
	 * interlock the access to the event queue in this case.
	 */
	critical_enter();
	fire_msiq_common(iv, fmqa);
	FIRE_PCI_WRITE_8(fmqa->fmqa_fica.fica_sc, fmqa->fmqa_fica.fica_clr,
	    INTCLR_IDLE);
	critical_exit();
}

static inline void
fire_msiq_common(struct intr_vector *iv, struct fire_msiqarg *fmqa)
{
	struct fire_softc *sc;
	struct fo_msiq_record *qrec;
	device_t dev;
	uint64_t word0;
	u_int head, msi, msiq;

	sc = fmqa->fmqa_fica.fica_sc;
	dev = sc->sc_dev;
	msiq = fmqa->fmqa_msiq;
	head = (FIRE_PCI_READ_8(sc, fmqa->fmqa_head) & FO_PCI_EQ_HD_MASK) >>
	    FO_PCI_EQ_HD_SHFT;
	qrec = &fmqa->fmqa_base[head];
	word0 = qrec->fomqr_word0;
	for (;;) {
		if (__predict_false((word0 & FO_MQR_WORD0_FMT_TYPE_MASK) == 0))
			break;
		KASSERT((word0 & FO_MQR_WORD0_FMT_TYPE_MSI64) != 0 ||
		    (word0 & FO_MQR_WORD0_FMT_TYPE_MSI32) != 0,
		    ("%s: received non-MSI/MSI-X message in event queue %d "
		    "(word0 %#llx)", device_get_nameunit(dev), msiq,
		    (unsigned long long)word0));
		msi = (word0 & FO_MQR_WORD0_DATA0_MASK) >>
		    FO_MQR_WORD0_DATA0_SHFT;
		/*
		 * Sanity check the MSI/MSI-X as long as we use a 1:1 mapping.
		 */
		KASSERT(msi == fmqa->fmqa_msi,
		    ("%s: received non-matching MSI/MSI-X in event queue %d "
		    "(%d versus %d)", device_get_nameunit(dev), msiq, msi,
		    fmqa->fmqa_msi));
		FIRE_PCI_WRITE_8(sc, FO_PCI_MSI_CLR_BASE + (msi << 3),
		    FO_PCI_MSI_CLR_EQWR_N);
		if (__predict_false(intr_event_handle(iv->iv_event,
		    NULL) != 0))
			printf("stray MSI/MSI-X in event queue %d\n", msiq);
		qrec->fomqr_word0 &= ~FO_MQR_WORD0_FMT_TYPE_MASK;
		head = (head + 1) % sc->sc_msiq_size;
		qrec = &fmqa->fmqa_base[head];
		word0 = qrec->fomqr_word0;
	}
	FIRE_PCI_WRITE_8(sc, fmqa->fmqa_head, (head & FO_PCI_EQ_HD_MASK) <<
	    FO_PCI_EQ_HD_SHFT);
	if (__predict_false((FIRE_PCI_READ_8(sc, fmqa->fmqa_tail) &
	    FO_PCI_EQ_TL_OVERR) != 0)) {
		device_printf(dev, "event queue %d overflow\n", msiq);
		msiq <<= 3;
		FIRE_PCI_WRITE_8(sc, FO_PCI_EQ_CTRL_CLR_BASE + msiq,
		    FIRE_PCI_READ_8(sc, FO_PCI_EQ_CTRL_CLR_BASE + msiq) |
		    FO_PCI_EQ_CTRL_CLR_COVERR);
	}
}

static int
fire_setup_intr(device_t dev, device_t child, struct resource *ires,
    int flags, driver_filter_t *filt, driver_intr_t *intr, void *arg,
    void **cookiep)
{
	struct fire_softc *sc;
	struct fire_msiqarg *fmqa;
	u_long vec;
	int error;
	u_int msi, msiq;

	sc = device_get_softc(dev);
	/*
	 * XXX this assumes that a device only has one INTx, while in fact
	 * Cassini+ and Saturn can use all four the firmware has assigned
	 * to them, but so does pci(4).
	 */
	if (rman_get_rid(ires) != 0) {
		msi = rman_get_start(ires);
		msiq = sc->sc_msi_msiq_table[msi - sc->sc_msi_first];
		vec = INTMAP_VEC(sc->sc_ign, sc->sc_msiq_ino_first + msiq);
		msiq += sc->sc_msiq_first;
		if (intr_vectors[vec].iv_ic != &fire_ic) {
			device_printf(dev,
			    "invalid interrupt controller for vector 0x%lx\n",
			    vec);
			return (EINVAL);
		}
		/*
		 * The MD interrupt code needs the vector rather than the MSI.
		 */
		rman_set_start(ires, vec);
		rman_set_end(ires, vec);
		error = bus_generic_setup_intr(dev, child, ires, flags, filt,
		    intr, arg, cookiep);
		rman_set_start(ires, msi);
		rman_set_end(ires, msi);
		if (error != 0)
			return (error);
		fmqa = intr_vectors[vec].iv_icarg;
		/*
		 * XXX inject our event queue handler.
		 */
		if (filt != NULL) {
			intr_vectors[vec].iv_func = fire_msiq_filter;
			intr_vectors[vec].iv_ic = &fire_msiqc_filter;
			/*
			 * Ensure the event queue interrupt is cleared, it
			 * might have triggered before.  Given we supply NULL
			 * as ic_clear, inthand_add() won't do this for us.
			 */
			FIRE_PCI_WRITE_8(sc, fmqa->fmqa_fica.fica_clr,
			    INTCLR_IDLE);
		} else
			intr_vectors[vec].iv_func = fire_msiq_handler;
		/* Record the MSI/MSI-X as long as we we use a 1:1 mapping. */
		fmqa->fmqa_msi = msi;
		FIRE_PCI_WRITE_8(sc, FO_PCI_EQ_CTRL_SET_BASE + (msiq << 3),
		    FO_PCI_EQ_CTRL_SET_EN);
		msi <<= 3;
		FIRE_PCI_WRITE_8(sc, FO_PCI_MSI_MAP_BASE + msi,
		    (FIRE_PCI_READ_8(sc, FO_PCI_MSI_MAP_BASE + msi) &
		    ~FO_PCI_MSI_MAP_EQNUM_MASK) |
		    ((msiq << FO_PCI_MSI_MAP_EQNUM_SHFT) &
		    FO_PCI_MSI_MAP_EQNUM_MASK));
		FIRE_PCI_WRITE_8(sc, FO_PCI_MSI_CLR_BASE + msi,
		    FO_PCI_MSI_CLR_EQWR_N);
		FIRE_PCI_WRITE_8(sc, FO_PCI_MSI_MAP_BASE + msi,
		    FIRE_PCI_READ_8(sc, FO_PCI_MSI_MAP_BASE + msi) |
		    FO_PCI_MSI_MAP_V);
		return (error);
	}

	/*
	 * Make sure the vector is fully specified and we registered
	 * our interrupt controller for it.
	 */
	vec = rman_get_start(ires);
	if (INTIGN(vec) != sc->sc_ign) {
		device_printf(dev, "invalid interrupt vector 0x%lx\n", vec);
		return (EINVAL);
	}
	if (intr_vectors[vec].iv_ic != &fire_ic) {
		device_printf(dev,
		    "invalid interrupt controller for vector 0x%lx\n", vec);
		return (EINVAL);
	}
	return (bus_generic_setup_intr(dev, child, ires, flags, filt, intr,
	    arg, cookiep));
}

static int
fire_teardown_intr(device_t dev, device_t child, struct resource *ires,
    void *cookie)
{
	struct fire_softc *sc;
	u_long vec;
	int error;
	u_int msi, msiq;

	sc = device_get_softc(dev);
	if (rman_get_rid(ires) != 0) {
		msi = rman_get_start(ires);
		msiq = sc->sc_msi_msiq_table[msi - sc->sc_msi_first];
		vec = INTMAP_VEC(sc->sc_ign, msiq + sc->sc_msiq_ino_first);
		msiq += sc->sc_msiq_first;
		msi <<= 3;
		FIRE_PCI_WRITE_8(sc, FO_PCI_MSI_MAP_BASE + msi,
		    FIRE_PCI_READ_8(sc, FO_PCI_MSI_MAP_BASE + msi) &
		    ~FO_PCI_MSI_MAP_V);
		msiq <<= 3;
		FIRE_PCI_WRITE_8(sc, FO_PCI_EQ_CTRL_CLR_BASE + msiq,
		    FO_PCI_EQ_CTRL_CLR_COVERR | FO_PCI_EQ_CTRL_CLR_E2I |
		    FO_PCI_EQ_CTRL_CLR_DIS);
		FIRE_PCI_WRITE_8(sc, FO_PCI_EQ_TL_BASE + msiq,
		    (0 << FO_PCI_EQ_TL_SHFT) & FO_PCI_EQ_TL_MASK);
		FIRE_PCI_WRITE_8(sc, FO_PCI_EQ_HD_BASE + msiq,
		    (0 << FO_PCI_EQ_HD_SHFT) & FO_PCI_EQ_HD_MASK);
		intr_vectors[vec].iv_ic = &fire_ic;
		/*
		 * The MD interrupt code needs the vector rather than the MSI.
		 */
		rman_set_start(ires, vec);
		rman_set_end(ires, vec);
		error = bus_generic_teardown_intr(dev, child, ires, cookie);
		msi >>= 3;
		rman_set_start(ires, msi);
		rman_set_end(ires, msi);
		return (error);
	}
	return (bus_generic_teardown_intr(dev, child, ires, cookie));
}

static struct resource *
fire_alloc_resource(device_t bus, device_t child, int type, int *rid,
    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct fire_softc *sc;

	if (type == SYS_RES_IRQ && *rid == 0) {
		sc = device_get_softc(bus);
		start = end = INTMAP_VEC(sc->sc_ign, end);
	}
	return (ofw_pci_alloc_resource(bus, child, type, rid, start, end,
	    count, flags));
}

static u_int
fire_get_timecount(struct timecounter *tc)
{
	struct fire_softc *sc;

	sc = tc->tc_priv;
	return (FIRE_CTRL_READ_8(sc, FO_XBC_PRF_CNT0) & TC_COUNTER_MAX_MASK);
}
