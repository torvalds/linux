/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 2010 Texas Instruments Incorporated - http://www.ti.com/
 * Copyright (c) 2012 Damjan Marion <dmarion@Freebsd.org>
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
 * 3. Neither the name of authors nor the names of its contributors may be
 *    used to endorse or promote products derived from this software without
 *    specific prior written permission.
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/endian.h>
#include <sys/mbuf.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <sys/sockio.h>
#include <sys/bus.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <arm/ti/ti_scm.h>
#include <arm/ti/ti_prcm.h>

#include <arm/ti/ti_edma3.h>

#define TI_EDMA3_NUM_TCS		3
#define TI_EDMA3_NUM_IRQS		3
#define TI_EDMA3_NUM_DMA_CHS		64
#define TI_EDMA3_NUM_QDMA_CHS		8

#define TI_EDMA3CC_PID			0x000
#define TI_EDMA3CC_DCHMAP(p)		(0x100 + ((p)*4))
#define TI_EDMA3CC_DMAQNUM(n)		(0x240 + ((n)*4))
#define TI_EDMA3CC_QDMAQNUM		0x260
#define TI_EDMA3CC_EMCR			0x308
#define TI_EDMA3CC_EMCRH		0x30C
#define TI_EDMA3CC_QEMCR		0x314
#define TI_EDMA3CC_CCERR		0x318
#define TI_EDMA3CC_CCERRCLR		0x31C
#define TI_EDMA3CC_DRAE(p)		(0x340 + ((p)*8))
#define TI_EDMA3CC_DRAEH(p)		(0x344 + ((p)*8))
#define TI_EDMA3CC_QRAE(p)		(0x380 + ((p)*4))
#define TI_EDMA3CC_S_ESR(p)		(0x2010 + ((p)*0x200))
#define TI_EDMA3CC_S_ESRH(p)		(0x2014 + ((p)*0x200))
#define TI_EDMA3CC_S_SECR(p)		(0x2040 + ((p)*0x200))
#define TI_EDMA3CC_S_SECRH(p)		(0x2044 + ((p)*0x200))
#define TI_EDMA3CC_S_EESR(p)		(0x2030 + ((p)*0x200))
#define TI_EDMA3CC_S_EESRH(p)		(0x2034 + ((p)*0x200))
#define TI_EDMA3CC_S_IESR(p)		(0x2060 + ((p)*0x200))
#define TI_EDMA3CC_S_IESRH(p)		(0x2064 + ((p)*0x200))
#define TI_EDMA3CC_S_IPR(p)		(0x2068 + ((p)*0x200))
#define TI_EDMA3CC_S_IPRH(p)		(0x206C + ((p)*0x200))
#define TI_EDMA3CC_S_QEESR(p)		(0x208C + ((p)*0x200))

#define TI_EDMA3CC_PARAM_OFFSET		0x4000
#define TI_EDMA3CC_OPT(p)		(TI_EDMA3CC_PARAM_OFFSET + 0x0 + ((p)*0x20))

#define TI_EDMA3CC_DMAQNUM_SET(c,q)	((0x7 & (q)) << (((c) % 8) * 4))
#define TI_EDMA3CC_DMAQNUM_CLR(c)	(~(0x7 << (((c) % 8) * 4)))
#define TI_EDMA3CC_QDMAQNUM_SET(c,q)	((0x7 & (q)) << ((c) * 4))
#define TI_EDMA3CC_QDMAQNUM_CLR(c)	(~(0x7 << ((c) * 4)))

#define TI_EDMA3CC_OPT_TCC_CLR		(~(0x3F000))
#define TI_EDMA3CC_OPT_TCC_SET(p)	(((0x3F000 >> 12) & (p)) << 12)

struct ti_edma3_softc {
	device_t		sc_dev;
	/* 
	 * We use one-element array in case if we need to add 
	 * mem resources for transfer control windows
	 */
	struct resource *	mem_res[1];
	struct resource *	irq_res[TI_EDMA3_NUM_IRQS];
	void			*ih_cookie[TI_EDMA3_NUM_IRQS];
};

static struct ti_edma3_softc *ti_edma3_sc = NULL;

static struct resource_spec ti_edma3_mem_spec[] = {
	{ SYS_RES_MEMORY,   0,  RF_ACTIVE },
	{ -1,               0,  0 }
};
static struct resource_spec ti_edma3_irq_spec[] = {
	{ SYS_RES_IRQ,      0,  RF_ACTIVE },
	{ SYS_RES_IRQ,      1,  RF_ACTIVE },
	{ SYS_RES_IRQ,      2,  RF_ACTIVE },
	{ -1,               0,  0 }
};

/* Read/Write macros */
#define ti_edma3_cc_rd_4(reg)		bus_read_4(ti_edma3_sc->mem_res[0], reg)
#define ti_edma3_cc_wr_4(reg, val)	bus_write_4(ti_edma3_sc->mem_res[0], reg, val)

static void ti_edma3_intr_comp(void *arg);
static void ti_edma3_intr_mperr(void *arg);
static void ti_edma3_intr_err(void *arg);

static struct {
	driver_intr_t *handler;
	char * description;
} ti_edma3_intrs[TI_EDMA3_NUM_IRQS] = {
	{ ti_edma3_intr_comp,	"EDMA Completion Interrupt" },
	{ ti_edma3_intr_mperr,	"EDMA Memory Protection Error Interrupt" },
	{ ti_edma3_intr_err,	"EDMA Error Interrupt" },
};

static int
ti_edma3_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "ti,edma3"))
		return (ENXIO);

	device_set_desc(dev, "TI EDMA Controller");
	return (0);
}

static int
ti_edma3_attach(device_t dev)
{
	struct ti_edma3_softc *sc = device_get_softc(dev);
	uint32_t reg;
	int err;
	int i;

	if (ti_edma3_sc)
		return (ENXIO);

	ti_edma3_sc = sc;
	sc->sc_dev = dev;

	/* Request the memory resources */
	err = bus_alloc_resources(dev, ti_edma3_mem_spec, sc->mem_res);
	if (err) {
		device_printf(dev, "Error: could not allocate mem resources\n");
		return (ENXIO);
	}

	/* Request the IRQ resources */
	err = bus_alloc_resources(dev, ti_edma3_irq_spec, sc->irq_res);
	if (err) {
		device_printf(dev, "Error: could not allocate irq resources\n");
		return (ENXIO);
	}

	/* Enable Channel Controller */
	ti_prcm_clk_enable(EDMA_TPCC_CLK);

	reg = ti_edma3_cc_rd_4(TI_EDMA3CC_PID);

	device_printf(dev, "EDMA revision %08x\n", reg);


	/* Attach interrupt handlers */
	for (i = 0; i < TI_EDMA3_NUM_IRQS; ++i) {
		err = bus_setup_intr(dev, sc->irq_res[i], INTR_TYPE_MISC |
		    INTR_MPSAFE, NULL, *ti_edma3_intrs[i].handler,
		    sc, &sc->ih_cookie[i]);
		if (err) {
			device_printf(dev, "could not setup %s\n",
			    ti_edma3_intrs[i].description);
			return (err);
		}
	}

	return (0);
}

static device_method_t ti_edma3_methods[] = {
	DEVMETHOD(device_probe, ti_edma3_probe),
	DEVMETHOD(device_attach, ti_edma3_attach),
	{0, 0},
};

static driver_t ti_edma3_driver = {
	"ti_edma3",
	ti_edma3_methods,
	sizeof(struct ti_edma3_softc),
};
static devclass_t ti_edma3_devclass;

DRIVER_MODULE(ti_edma3, simplebus, ti_edma3_driver, ti_edma3_devclass, 0, 0);
MODULE_DEPEND(ti_edma3, ti_prcm, 1, 1, 1);

static void
ti_edma3_intr_comp(void *arg)
{
	printf("%s: unimplemented\n", __func__);
}

static void
ti_edma3_intr_mperr(void *arg)
{
	printf("%s: unimplemented\n", __func__);
}

static void
ti_edma3_intr_err(void *arg)
{
	printf("%s: unimplemented\n", __func__);
}

void
ti_edma3_init(unsigned int eqn)
{
	uint32_t reg;
	int i;

	/* on AM335x Event queue 0 is always mapped to Transfer Controller 0,
	 * event queue 1 to TC2, etc. So we are asking PRCM to power on specific
	 * TC based on what event queue we need to initialize */
	ti_prcm_clk_enable(EDMA_TPTC0_CLK + eqn);

	/* Clear Event Missed Regs */
	ti_edma3_cc_wr_4(TI_EDMA3CC_EMCR, 0xFFFFFFFF);
	ti_edma3_cc_wr_4(TI_EDMA3CC_EMCRH, 0xFFFFFFFF);
	ti_edma3_cc_wr_4(TI_EDMA3CC_QEMCR, 0xFFFFFFFF);

	/* Clear Error Reg */
	ti_edma3_cc_wr_4(TI_EDMA3CC_CCERRCLR, 0xFFFFFFFF);

	/* Enable DMA channels 0-63 */
	ti_edma3_cc_wr_4(TI_EDMA3CC_DRAE(0), 0xFFFFFFFF);
	ti_edma3_cc_wr_4(TI_EDMA3CC_DRAEH(0), 0xFFFFFFFF);

	for (i = 0; i < 64; i++) {
		ti_edma3_cc_wr_4(TI_EDMA3CC_DCHMAP(i), i<<5);
	}

	/* Initialize the DMA Queue Number Registers */
	for (i = 0; i < TI_EDMA3_NUM_DMA_CHS; i++) {
		reg = ti_edma3_cc_rd_4(TI_EDMA3CC_DMAQNUM(i>>3));
		reg &= TI_EDMA3CC_DMAQNUM_CLR(i);
		reg |= TI_EDMA3CC_DMAQNUM_SET(i, eqn);
		ti_edma3_cc_wr_4(TI_EDMA3CC_DMAQNUM(i>>3), reg);
	}

	/* Enable the QDMA Region access for all channels */
	ti_edma3_cc_wr_4(TI_EDMA3CC_QRAE(0), (1 << TI_EDMA3_NUM_QDMA_CHS) - 1);

	/*Initialize QDMA Queue Number Registers */
	for (i = 0; i < TI_EDMA3_NUM_QDMA_CHS; i++) {
		reg = ti_edma3_cc_rd_4(TI_EDMA3CC_QDMAQNUM);
		reg &= TI_EDMA3CC_QDMAQNUM_CLR(i);
		reg |= TI_EDMA3CC_QDMAQNUM_SET(i, eqn);
		ti_edma3_cc_wr_4(TI_EDMA3CC_QDMAQNUM, reg);
	}
}

#ifdef notyet
int
ti_edma3_enable_event_intr(unsigned int ch)
{
	uint32_t reg;

	if (ch >= TI_EDMA3_NUM_DMA_CHS)
		return (EINVAL);

	if (ch < 32) {
		ti_edma3_cc_wr_4(TI_EDMA3CC_S_IESR(0), 1 << ch);
	} else {
		ti_edma3_cc_wr_4(TI_EDMA3CC_S_IESRH(0), 1 << (ch - 32));
	}
	return 0;
}
#endif

int
ti_edma3_request_dma_ch(unsigned int ch, unsigned int tccn, unsigned int eqn)
{
	uint32_t reg;

	if (ch >= TI_EDMA3_NUM_DMA_CHS)
		return (EINVAL);

	/* Enable the DMA channel in the DRAE/DRAEH registers */
	if (ch < 32) {
		reg = ti_edma3_cc_rd_4(TI_EDMA3CC_DRAE(0));
		reg |= (0x01 << ch);
		ti_edma3_cc_wr_4(TI_EDMA3CC_DRAE(0), reg);
	} else {
		reg = ti_edma3_cc_rd_4(TI_EDMA3CC_DRAEH(0));
		reg |= (0x01 << (ch - 32));
		ti_edma3_cc_wr_4(TI_EDMA3CC_DRAEH(0), reg);
	}

	/* Associate DMA Channel to Event Queue */
	reg = ti_edma3_cc_rd_4(TI_EDMA3CC_DMAQNUM(ch >> 3));
	reg &= TI_EDMA3CC_DMAQNUM_CLR(ch);
	reg |= TI_EDMA3CC_DMAQNUM_SET((ch), eqn);
	ti_edma3_cc_wr_4(TI_EDMA3CC_DMAQNUM(ch >> 3), reg);

	/* Set TCC in corresponding PaRAM Entry */
	reg = ti_edma3_cc_rd_4(TI_EDMA3CC_OPT(ch));
	reg &= TI_EDMA3CC_OPT_TCC_CLR;
	reg |= TI_EDMA3CC_OPT_TCC_SET(ch);
	ti_edma3_cc_wr_4(TI_EDMA3CC_OPT(ch), reg);

	return 0;
}

int
ti_edma3_request_qdma_ch(unsigned int ch, unsigned int tccn, unsigned int eqn)
{
	uint32_t reg;

	if (ch >= TI_EDMA3_NUM_DMA_CHS)
		return (EINVAL);

	/* Enable the QDMA channel in the QRAE registers */
	reg = ti_edma3_cc_rd_4(TI_EDMA3CC_QRAE(0));
	reg |= (0x01 << ch);
	ti_edma3_cc_wr_4(TI_EDMA3CC_QRAE(0), reg);

	/* Associate QDMA Channel to Event Queue */
	reg = ti_edma3_cc_rd_4(TI_EDMA3CC_QDMAQNUM);
	reg |= TI_EDMA3CC_QDMAQNUM_SET(ch, eqn);
	ti_edma3_cc_wr_4(TI_EDMA3CC_QDMAQNUM, reg);

	/* Set TCC in corresponding PaRAM Entry */
	reg = ti_edma3_cc_rd_4(TI_EDMA3CC_OPT(ch));
	reg &= TI_EDMA3CC_OPT_TCC_CLR;
	reg |= TI_EDMA3CC_OPT_TCC_SET(ch);
	ti_edma3_cc_wr_4(TI_EDMA3CC_OPT(ch), reg);

	return 0;
}

int
ti_edma3_enable_transfer_manual(unsigned int ch)
{
	if (ch >= TI_EDMA3_NUM_DMA_CHS)
		return (EINVAL);

	/* set corresponding bit in ESR/ESRH to set a event */
	if (ch < 32) {
		ti_edma3_cc_wr_4(TI_EDMA3CC_S_ESR(0), 1 <<  ch);
	} else {
		ti_edma3_cc_wr_4(TI_EDMA3CC_S_ESRH(0), 1 <<  (ch - 32));
	}

	return 0;
}

int
ti_edma3_enable_transfer_qdma(unsigned int ch)
{
	if (ch >= TI_EDMA3_NUM_QDMA_CHS)
		return (EINVAL);

	/* set corresponding bit in QEESR to enable QDMA event */
	ti_edma3_cc_wr_4(TI_EDMA3CC_S_QEESR(0), (1 << ch));

	return 0;
}

int
ti_edma3_enable_transfer_event(unsigned int ch)
{
	if (ch >= TI_EDMA3_NUM_DMA_CHS)
		return (EINVAL);

	/* Clear SECR(H) & EMCR(H) to clean any previous NULL request
	 * and set corresponding bit in EESR to enable DMA event */
	if(ch < 32) {
		ti_edma3_cc_wr_4(TI_EDMA3CC_S_SECR(0), (1 << ch));
		ti_edma3_cc_wr_4(TI_EDMA3CC_EMCR, (1 << ch));
		ti_edma3_cc_wr_4(TI_EDMA3CC_S_EESR(0), (1 << ch));
	} else {
		ti_edma3_cc_wr_4(TI_EDMA3CC_S_SECRH(0), 1 << (ch - 32));
		ti_edma3_cc_wr_4(TI_EDMA3CC_EMCRH, 1 << (ch - 32));
		ti_edma3_cc_wr_4(TI_EDMA3CC_S_EESRH(0), 1 << (ch - 32));
	}

	return 0;
}

void
ti_edma3_param_write(unsigned int ch, struct ti_edma3cc_param_set *prs)
{
	bus_write_region_4(ti_edma3_sc->mem_res[0], TI_EDMA3CC_OPT(ch),
	    (uint32_t *) prs, 8);
}

void
ti_edma3_param_read(unsigned int ch, struct ti_edma3cc_param_set *prs)
{
	bus_read_region_4(ti_edma3_sc->mem_res[0], TI_EDMA3CC_OPT(ch),
	    (uint32_t *) prs, 8);
}
