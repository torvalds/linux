/*-
 * Copyright (c) 2014 Ruslan Bukin <br@bsdpad.com>
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
 */

/*
 * Samsung Exynos 5 Interrupt Combiner
 * Chapter 7, Exynos 5 Dual User's Manual Public Rev 1.00
 */
#ifdef USB_GLOBAL_INCLUDE_FILE
#include USB_GLOBAL_INCLUDE_FILE
#else
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/rman.h>
#include <sys/timeet.h>
#include <sys/timetc.h>
#include <sys/watchdog.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/intr.h>
#endif

#include <arm/samsung/exynos/exynos5_common.h>
#include <arm/samsung/exynos/exynos5_combiner.h>

#define NGRP		32

#define	IESR(n)	(0x10 * n + 0x0)	/* Interrupt enable set */
#define	IECR(n)	(0x10 * n + 0x4)	/* Interrupt enable clear */
#define	ISTR(n)	(0x10 * n + 0x8)	/* Interrupt status */
#define	IMSR(n)	(0x10 * n + 0xC)	/* Interrupt masked status */
#define	CIPSR	0x100			/* Combined interrupt pending */

struct combiner_softc {
	struct resource		*res[1 + NGRP];
	bus_space_tag_t		bst;
	bus_space_handle_t	bsh;
	void			*ih[NGRP];
	device_t		dev;
};

struct combiner_softc *combiner_sc;

static struct resource_spec combiner_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,	RF_ACTIVE },
	{ SYS_RES_IRQ,		1,	RF_ACTIVE },
	{ SYS_RES_IRQ,		2,	RF_ACTIVE },
	{ SYS_RES_IRQ,		3,	RF_ACTIVE },
	{ SYS_RES_IRQ,		4,	RF_ACTIVE },
	{ SYS_RES_IRQ,		5,	RF_ACTIVE },
	{ SYS_RES_IRQ,		6,	RF_ACTIVE },
	{ SYS_RES_IRQ,		7,	RF_ACTIVE },
	{ SYS_RES_IRQ,		8,	RF_ACTIVE },
	{ SYS_RES_IRQ,		9,	RF_ACTIVE },
	{ SYS_RES_IRQ,		10,	RF_ACTIVE },
	{ SYS_RES_IRQ,		11,	RF_ACTIVE },
	{ SYS_RES_IRQ,		12,	RF_ACTIVE },
	{ SYS_RES_IRQ,		13,	RF_ACTIVE },
	{ SYS_RES_IRQ,		14,	RF_ACTIVE },
	{ SYS_RES_IRQ,		15,	RF_ACTIVE },
	{ SYS_RES_IRQ,		16,	RF_ACTIVE },
	{ SYS_RES_IRQ,		17,	RF_ACTIVE },
	{ SYS_RES_IRQ,		18,	RF_ACTIVE },
	{ SYS_RES_IRQ,		19,	RF_ACTIVE },
	{ SYS_RES_IRQ,		20,	RF_ACTIVE },
	{ SYS_RES_IRQ,		21,	RF_ACTIVE },
	{ SYS_RES_IRQ,		22,	RF_ACTIVE },
	{ SYS_RES_IRQ,		23,	RF_ACTIVE },
	{ SYS_RES_IRQ,		24,	RF_ACTIVE },
	{ SYS_RES_IRQ,		25,	RF_ACTIVE },
	{ SYS_RES_IRQ,		26,	RF_ACTIVE },
	{ SYS_RES_IRQ,		27,	RF_ACTIVE },
	{ SYS_RES_IRQ,		28,	RF_ACTIVE },
	{ SYS_RES_IRQ,		29,	RF_ACTIVE },
	{ SYS_RES_IRQ,		30,	RF_ACTIVE },
	{ SYS_RES_IRQ,		31,	RF_ACTIVE },
	{ -1, 0 }
};

struct combiner_entry {
	int combiner_id;
	int bit;
	char *source_name;
};

static struct combiner_entry interrupt_table[] = {
	{ 63, 1, "EINT[15]" },
	{ 63, 0, "EINT[14]" },
	{ 62, 1, "EINT[13]" },
	{ 62, 0, "EINT[12]" },
	{ 61, 1, "EINT[11]" },
	{ 61, 0, "EINT[10]" },
	{ 60, 1, "EINT[9]" },
	{ 60, 0, "EINT[8]" },
	{ 59, 1, "EINT[7]" },
	{ 59, 0, "EINT[6]" },
	{ 58, 1, "EINT[5]" },
	{ 58, 0, "EINT[4]" },
	{ 57, 3, "MCT_G3" },
	{ 57, 2, "MCT_G2" },
	{ 57, 1, "EINT[3]" },
	{ 57, 0, "EINT[2]" },
	{ 56, 6, "SYSMMU_G2D[1]" },
	{ 56, 5, "SYSMMU_G2D[0]" },
	{ 56, 2, "SYSMMU_FIMC_LITE1[1]" },
	{ 56, 1, "SYSMMU_FIMC_LITE1[0]" },
	{ 56, 0, "EINT[1]" },
	{ 55, 4, "MCT_G1" },
	{ 55, 3, "MCT_G0" },
	{ 55, 0, "EINT[0]" },
	{ 54, 7, "CPU_nCNTVIRQ[1]" },
	{ 54, 6, "CPU_nCTIIRQ[1]" },
	{ 54, 5, "CPU_nCNTPSIRQ[1]" },
	{ 54, 4, "CPU_nPMUIRQ[1]" },
	{ 54, 3, "CPU_nCNTPNSIRQ[1]" },
	{ 54, 2, "CPU_PARITYFAILSCU[1]" },
	{ 54, 1, "CPU_nCNTHPIRQ[1]" },
	{ 54, 0, "PARITYFAIL[1]" },
	{ 53, 1, "CPU_nIRQ[1]" },
	{ 52, 0, "CPU_nIRQ[0]" },
	{ 51, 7, "CPU_nRAMERRIRQ" },
	{ 51, 6, "CPU_nAXIERRIRQ" },
	{ 51, 4, "INT_COMB_ISP_GIC" },
	{ 51, 3, "INT_COMB_IOP_GIC" },
	{ 51, 2, "CCI_nERRORIRQ" },
	{ 51, 1, "INT_COMB_ARMISP_GIC" },
	{ 51, 0, "INT_COMB_ARMIOP_GIC" },
	{ 50, 7, "DISP1[3]" },
	{ 50, 6, "DISP1[2]" },
	{ 50, 5, "DISP1[1]" },
	{ 50, 4, "DISP1[0]" },
	{ 49, 3, "SSCM_PULSE_IRQ_C2CIF[1]" },
	{ 49, 2, "SSCM_PULSE_IRQ_C2CIF[0]" },
	{ 49, 1, "SSCM_IRQ_C2CIF[1]" },
	{ 49, 0, "SSCM_IRQ_C2CIF[0]" },
	{ 48, 3, "PEREV_M1_CDREX" },
	{ 48, 2, "PEREV_M0_CDREX" },
	{ 48, 1, "PEREV_A1_CDREX" },
	{ 48, 0, "PEREV_A0_CDREX" },
	{ 47, 3, "MDMA0_ABORT" },
	/* 46 is fully reserved */
	{ 45, 1, "MDMA1_ABORT" },
	/* 44 is fully reserved */
	{ 43, 7, "SYSMMU_DRCISP[1]" },
	{ 43, 6, "SYSMMU_DRCISP[0]" },
	{ 43, 1, "SYSMMU_ODC[1]" },
	{ 43, 0, "SYSMMU_ODC[0]" },
	{ 42, 7, "SYSMMU_ISP[1]" },
	{ 42, 6, "SYSMMU_ISP[0]" },
	{ 42, 5, "SYSMMU_DIS0[1]" },
	{ 42, 4, "SYSMMU_DIS0[0]" },
	{ 42, 3, "DP1" },
	{ 41, 5, "SYSMMU_DIS1[1]" },
	{ 41, 4, "SYSMMU_DIS1[0]" },
	{ 40, 6, "SYSMMU_MFCL[1]" },
	{ 40, 5, "SYSMMU_MFCL[0]" },
	{ 39, 5, "SYSMMU_TV_M0[1]" },
	{ 39, 4, "SYSMMU_TV_M0[0]" },
	{ 39, 3, "SYSMMU_MDMA1[1]" },
	{ 39, 2, "SYSMMU_MDMA1[0]" },
	{ 39, 1, "SYSMMU_MDMA0[1]" },
	{ 39, 0, "SYSMMU_MDMA0[0]" },
	{ 38, 7, "SYSMMU_SSS[1]" },
	{ 38, 6, "SYSMMU_SSS[0]" },
	{ 38, 5, "SYSMMU_RTIC[1]" },
	{ 38, 4, "SYSMMU_RTIC[0]" },
	{ 38, 3, "SYSMMU_MFCR[1]" },
	{ 38, 2, "SYSMMU_MFCR[0]" },
	{ 38, 1, "SYSMMU_ARM[1]" },
	{ 38, 0, "SYSMMU_ARM[0]" },
	{ 37, 7, "SYSMMU_3DNR[1]" },
	{ 37, 6, "SYSMMU_3DNR[0]" },
	{ 37, 5, "SYSMMU_MCUISP[1]" },
	{ 37, 4, "SYSMMU_MCUISP[0]" },
	{ 37, 3, "SYSMMU_SCALERCISP[1]" },
	{ 37, 2, "SYSMMU_SCALERCISP[0]" },
	{ 37, 1, "SYSMMU_FDISP[1]" },
	{ 37, 0, "SYSMMU_FDISP[0]" },
	{ 36, 7, "MCUIOP_CTIIRQ" },
	{ 36, 6, "MCUIOP_PMUIRQ" },
	{ 36, 5, "MCUISP_CTIIRQ" },
	{ 36, 4, "MCUISP_PMUIRQ" },
	{ 36, 3, "SYSMMU_JPEGX[1]" },
	{ 36, 2, "SYSMMU_JPEGX[0]" },
	{ 36, 1, "SYSMMU_ROTATOR[1]" },
	{ 36, 0, "SYSMMU_ROTATOR[0]" },
	{ 35, 7, "SYSMMU_SCALERPISP[1]" },
	{ 35, 6, "SYSMMU_SCALERPISP[0]" },
	{ 35, 5, "SYSMMU_FIMC_LITE0[1]" },
	{ 35, 4, "SYSMMU_FIMC_LITE0[0]" },
	{ 35, 3, "SYSMMU_DISP1_M0[1]" },
	{ 35, 2, "SYSMMU_DISP1_M0[0]" },
	{ 35, 1, "SYSMMU_FIMC_LITE2[1]" },
	{ 35, 0, "SYSMMU_FIMC_LITE2[0]" },
	{ 34, 7, "SYSMMU_GSCL3[1]" },
	{ 34, 6, "SYSMMU_GSCL3[0]" },
	{ 34, 5, "SYSMMU_GSCL2[1]" },
	{ 34, 4, "SYSMMU_GSCL2[0]" },
	{ 34, 3, "SYSMMU_GSCL1[1]" },
	{ 34, 2, "SYSMMU_GSCL1[0]" },
	{ 34, 1, "SYSMMU_GSCL0[1]" },
	{ 34, 0, "SYSMMU_GSCL0[0]" },
	{ 33, 7, "CPU_nCNTVIRQ[0]" },
	{ 33, 6, "CPU_nCNTPSIRQ[0]" },
	{ 33, 5, "CPU_nCNTPSNIRQ[0]" },
	{ 33, 4, "CPU_nCNTHPIRQ[0]" },
	{ 33, 3, "CPU_nCTIIRQ[0]" },
	{ 33, 2, "CPU_nPMUIRQ[0]" },
	{ 33, 1, "CPU_PARITYFAILSCU[0]" },
	{ 33, 0, "CPU_PARITYFAIL0" },
	{ 32, 7, "TZASC_XR1BXW" },
	{ 32, 6, "TZASC_XR1BXR" },
	{ 32, 5, "TZASC_XLBXW" },
	{ 32, 4, "TZASC_XLBXR" },
	{ 32, 3, "TZASC_DRBXW" },
	{ 32, 2, "TZASC_DRBXR" },
	{ 32, 1, "TZASC_CBXW" },
	{ 32, 0, "TZASC_CBXR" },

	{ -1, -1, NULL },
};

struct combined_intr {
	uint32_t	enabled;
	void		(*ih) (void *);
	void		*ih_user;
};

static struct combined_intr intr_map[32][8];

static void
combiner_intr(void *arg)
{
	struct combiner_softc *sc;
	void (*ih) (void *);
	void *ih_user;
	int enabled;
	int intrs;
	int shift;
	int cirq;
	int grp;
	int i,n;

	sc = arg;

	intrs = READ4(sc, CIPSR);
	for (grp = 0; grp < 32; grp++) {
		if (intrs & (1 << grp)) {
			n = (grp / 4);
			shift = (grp % 4) * 8;

			cirq = READ4(sc, ISTR(n));
			for (i = 0; i < 8; i++) {
				if (cirq & (1 << (i + shift))) {
					ih = intr_map[grp][i].ih;
					ih_user = intr_map[grp][i].ih_user;
					enabled = intr_map[grp][i].enabled;
					if (enabled && (ih != NULL)) {
						ih(ih_user);
					}
				}
			}
		}
	}
}

void
combiner_setup_intr(char *source_name, void (*ih)(void *), void *ih_user)
{
	struct combiner_entry *entry;
	struct combined_intr *cirq;
	struct combiner_softc *sc;
	int shift;
	int reg;
	int grp;
	int n;
	int i;

	sc = combiner_sc;

	if (sc == NULL) {
		device_printf(sc->dev, "Error: combiner is not attached\n");
		return;
	}

	entry = NULL;

	for (i = 0; i < NGRP && interrupt_table[i].bit != -1; i++) {
		if (strcmp(interrupt_table[i].source_name, source_name) == 0) {
			entry = &interrupt_table[i];
		}
	}

	if (entry == NULL) {
		device_printf(sc->dev, "Can't find interrupt name %s\n",
		    source_name);
		return;
	}

#if 0
	device_printf(sc->dev, "Setting up interrupt %s\n", source_name);
#endif

	grp = entry->combiner_id - 32;

	cirq = &intr_map[grp][entry->bit];
	cirq->enabled = 1;
	cirq->ih = ih;
	cirq->ih_user = ih_user;

	n = grp / 4;
	shift = (grp % 4) * 8 + entry->bit;

	reg = (1 << shift);
	WRITE4(sc, IESR(n), reg);
}

static int
combiner_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "exynos,combiner"))
		return (ENXIO);

	device_set_desc(dev, "Samsung Exynos 5 Interrupt Combiner");
	return (BUS_PROBE_DEFAULT);
}

static int
combiner_attach(device_t dev)
{
	struct combiner_softc *sc;
	int err;
	int i;

	sc = device_get_softc(dev);
	sc->dev = dev;

	if (bus_alloc_resources(dev, combiner_spec, sc->res)) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	/* Memory interface */
	sc->bst = rman_get_bustag(sc->res[0]);
	sc->bsh = rman_get_bushandle(sc->res[0]);

	combiner_sc = sc;

        /* Setup interrupt handler */
	for (i = 0; i < NGRP; i++) {
		err = bus_setup_intr(dev, sc->res[1+i], INTR_TYPE_BIO | \
		    INTR_MPSAFE, NULL, combiner_intr, sc, &sc->ih[i]);
		if (err) {
			device_printf(dev, "Unable to alloc int resource.\n");
			return (ENXIO);
		}
	}

	return (0);
}

static device_method_t combiner_methods[] = {
	DEVMETHOD(device_probe,		combiner_probe),
	DEVMETHOD(device_attach,	combiner_attach),
	{ 0, 0 }
};

static driver_t combiner_driver = {
	"combiner",
	combiner_methods,
	sizeof(struct combiner_softc),
};

static devclass_t combiner_devclass;

DRIVER_MODULE(combiner, simplebus, combiner_driver, combiner_devclass, 0, 0);
