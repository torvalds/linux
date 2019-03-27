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
 * Vybrid Family 12-bit Analog to Digital Converter (ADC)
 * Chapter 37, Vybrid Reference Manual, Rev. 5, 07/2013
 */

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

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/intr.h>

#include <arm/freescale/vybrid/vf_common.h>
#include <arm/freescale/vybrid/vf_adc.h>

#define	ADC_HC0		0x00		/* Ctrl reg for hardware triggers */
#define	ADC_HC1		0x04		/* Ctrl reg for hardware triggers */
#define	 HC_AIEN	(1 << 7)	/* Conversion Complete Int Control */
#define	 HC_ADCH_M	0x1f		/* Input Channel Select Mask */
#define	 HC_ADCH_S	0		/* Input Channel Select Shift */
#define	ADC_HS		0x08		/* Status register for HW triggers */
#define	 HS_COCO0	(1 << 0)	/* Conversion Complete Flag */
#define	 HS_COCO1	(1 << 1)	/* Conversion Complete Flag */
#define	ADC_R0		0x0C		/* Data result reg for HW triggers */
#define	ADC_R1		0x10		/* Data result reg for HW triggers */
#define	ADC_CFG		0x14		/* Configuration register */
#define	 CFG_OVWREN	(1 << 16)	/* Data Overwrite Enable */
#define	 CFG_AVGS_M	0x3		/* Hardware Average select Mask */
#define	 CFG_AVGS_S	14		/* Hardware Average select Shift */
#define	 CFG_ADTRG	(1 << 13)	/* Conversion Trigger Select */
#define	 CFG_REFSEL_M	0x3		/* Voltage Reference Select Mask */
#define	 CFG_REFSEL_S	11		/* Voltage Reference Select Shift */
#define	 CFG_ADHSC	(1 << 10)	/* High Speed Configuration */
#define	 CFG_ADSTS_M	0x3		/* Defines the sample time duration */
#define	 CFG_ADSTS_S	8		/* Defines the sample time duration */
#define	 CFG_ADLPC	(1 << 7)	/* Low-Power Configuration */
#define	 CFG_ADIV_M	0x3		/* Clock Divide Select */
#define	 CFG_ADIV_S	5		/* Clock Divide Select */
#define	 CFG_ADLSMP	(1 << 4)	/* Long Sample Time Configuration */
#define	 CFG_MODE_M	0x3		/* Conversion Mode Selection Mask */
#define	 CFG_MODE_S	2		/* Conversion Mode Selection Shift */
#define	 CFG_MODE_12	0x2		/* 12-bit mode */
#define	 CFG_ADICLK_M	0x3		/* Input Clock Select Mask */
#define	 CFG_ADICLK_S	0		/* Input Clock Select Shift */
#define	ADC_GC		0x18		/* General control register */
#define	 GC_CAL		(1 << 7)	/* Calibration */
#define	 GC_ADCO	(1 << 6)	/* Continuous Conversion Enable */
#define	 GC_AVGE	(1 << 5)	/* Hardware average enable */
#define	 GC_ACFE	(1 << 4)	/* Compare Function Enable */
#define	 GC_ACFGT	(1 << 3)	/* Compare Function Greater Than En */
#define	 GC_ACREN	(1 << 2)	/* Compare Function Range En */
#define	 GC_DMAEN	(1 << 1)	/* DMA Enable */
#define	 GC_ADACKEN	(1 << 0)	/* Asynchronous clock output enable */
#define	ADC_GS		0x1C		/* General status register */
#define	 GS_AWKST	(1 << 2)	/* Asynchronous wakeup int status */
#define	 GS_CALF	(1 << 1)	/* Calibration Failed Flag */
#define	 GS_ADACT	(1 << 0)	/* Conversion Active */
#define	ADC_CV		0x20		/* Compare value register */
#define	 CV_CV2_M	0xfff		/* Compare Value 2 Mask */
#define	 CV_CV2_S	16		/* Compare Value 2 Shift */
#define	 CV_CV1_M	0xfff		/* Compare Value 1 Mask */
#define	 CV_CV1_S	0		/* Compare Value 1 Shift */
#define	ADC_OFS		0x24		/* Offset correction value register */
#define	 OFS_SIGN	12		/* Sign bit */
#define	 OFS_M		0xfff		/* Offset value Mask */
#define	 OFS_S		0		/* Offset value Shift */
#define	ADC_CAL		0x28		/* Calibration value register */
#define	 CAL_CODE_M	0xf		/* Calibration Result Value Mask */
#define	 CAL_CODE_S	0		/* Calibration Result Value Shift */
#define	ADC_PCTL	0x30		/* Pin control register */

struct adc_softc {
	struct resource		*res[2];
	bus_space_tag_t		bst;
	bus_space_handle_t	bsh;
	void			*ih;
};

struct adc_softc *adc_sc;

static struct resource_spec adc_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,	RF_ACTIVE },
	{ -1, 0 }
};

static int
adc_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "fsl,mvf600-adc"))
		return (ENXIO);

	device_set_desc(dev, "Vybrid Family "
	    "12-bit Analog to Digital Converter");
	return (BUS_PROBE_DEFAULT);
}

static void
adc_intr(void *arg)
{
	struct adc_softc *sc;

	sc = arg;

	/* Conversation complete */
}

uint32_t
adc_read(void)
{
	struct adc_softc *sc;

	sc = adc_sc;
	if (sc == NULL)
		return (0);

	return (READ4(sc, ADC_R0));
}

uint32_t
adc_enable(int channel)
{
	struct adc_softc *sc;
	int reg;

	sc = adc_sc;
	if (sc == NULL)
		return (1);

	reg = READ4(sc, ADC_HC0);
	reg &= ~(HC_ADCH_M << HC_ADCH_S);
	reg |= (channel << HC_ADCH_S);
	WRITE4(sc, ADC_HC0, reg);

	return (0);
}

static int
adc_attach(device_t dev)
{
	struct adc_softc *sc;
	int err;
	int reg;

	sc = device_get_softc(dev);

	if (bus_alloc_resources(dev, adc_spec, sc->res)) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	/* Memory interface */
	sc->bst = rman_get_bustag(sc->res[0]);
	sc->bsh = rman_get_bushandle(sc->res[0]);

	adc_sc = sc;

	/* Setup interrupt handler */
	err = bus_setup_intr(dev, sc->res[1], INTR_TYPE_BIO | INTR_MPSAFE,
	    NULL, adc_intr, sc, &sc->ih);
	if (err) {
		device_printf(dev, "Unable to alloc interrupt resource.\n");
		return (ENXIO);
	}

	/* Configure 12-bit mode */
	reg = READ4(sc, ADC_CFG);
	reg &= ~(CFG_MODE_M << CFG_MODE_S);
	reg |= (CFG_MODE_12 << CFG_MODE_S); /* 12bit */
	WRITE4(sc, ADC_CFG, reg);

	/* Configure for continuous conversion */
	reg = READ4(sc, ADC_GC);
	reg |= (GC_ADCO | GC_AVGE);
	WRITE4(sc, ADC_GC, reg);

	/* Disable interrupts */
	reg = READ4(sc, ADC_HC0);
	reg &= HC_AIEN;
	WRITE4(sc, ADC_HC0, reg);

	return (0);
}

static device_method_t adc_methods[] = {
	DEVMETHOD(device_probe,		adc_probe),
	DEVMETHOD(device_attach,	adc_attach),
	{ 0, 0 }
};

static driver_t adc_driver = {
	"adc",
	adc_methods,
	sizeof(struct adc_softc),
};

static devclass_t adc_devclass;

DRIVER_MODULE(adc, simplebus, adc_driver, adc_devclass, 0, 0);
