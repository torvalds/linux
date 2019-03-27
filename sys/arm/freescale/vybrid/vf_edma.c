/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
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
 * Vybrid Family Enhanced Direct Memory Access Controller (eDMA)
 * Chapter 21, Vybrid Reference Manual, Rev. 5, 07/2013
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
#include <sys/watchdog.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/intr.h>

#include <arm/freescale/vybrid/vf_edma.h>
#include <arm/freescale/vybrid/vf_dmamux.h>
#include <arm/freescale/vybrid/vf_common.h>

struct edma_channel {
	uint32_t	enabled;
	uint32_t	mux_num;
	uint32_t	mux_src;
	uint32_t	mux_chn;
	uint32_t	(*ih) (void *, int);
	void		*ih_user;
};

static struct edma_channel edma_map[EDMA_NUM_CHANNELS];

static struct resource_spec edma_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_MEMORY,	1,	RF_ACTIVE }, /* TCD */
	{ SYS_RES_IRQ,		0,	RF_ACTIVE }, /* Transfer complete */
	{ SYS_RES_IRQ,		1,	RF_ACTIVE }, /* Error Interrupt */
	{ -1, 0 }
};

static int
edma_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "fsl,mvf600-edma"))
		return (ENXIO);

	device_set_desc(dev, "Vybrid Family eDMA Controller");
	return (BUS_PROBE_DEFAULT);
}

static void
edma_transfer_complete_intr(void *arg)
{
	struct edma_channel *ch;
	struct edma_softc *sc;
	int interrupts;
	int i;

	sc = arg;

	interrupts = READ4(sc, DMA_INT);
	WRITE1(sc, DMA_CINT, CINT_CAIR);

	for (i = 0; i < EDMA_NUM_CHANNELS; i++) {
		if (interrupts & (0x1 << i)) {
			ch = &edma_map[i];
			if (ch->enabled == 1) {
				if (ch->ih != NULL) {
					ch->ih(ch->ih_user, i);
				}
			}
		}
	}
}

static void
edma_err_intr(void *arg)
{
	struct edma_softc *sc;
	int reg;

	sc = arg;

	reg = READ4(sc, DMA_ERR);

#if 0
	device_printf(sc->dev, "DMA_ERR 0x%08x, ES 0x%08x\n",
	    reg, READ4(sc, DMA_ES));
#endif

	WRITE1(sc, DMA_CERR, CERR_CAEI);
}

static int
channel_free(struct edma_softc *sc, int chnum)
{
	struct edma_channel *ch;

	ch = &edma_map[chnum];
	ch->enabled = 0;

	dmamux_configure(ch->mux_num, ch->mux_src, ch->mux_chn, 0);

	return (0);
}

static int
channel_configure(struct edma_softc *sc, int mux_grp, int mux_src)
{
	struct edma_channel *ch;
	int channel_first;
	int mux_num;
	int chnum;
	int i;

	if ((sc->device_id == 0 && mux_grp == 1) ||	\
	    (sc->device_id == 1 && mux_grp == 0)) {
		channel_first = NCHAN_PER_MUX;
		mux_num = (sc->device_id * 2) + 1;
	} else {
		channel_first = 0;
		mux_num = sc->device_id * 2;
	}

	/* Take first unused eDMA channel */
	ch = NULL;
	for (i = channel_first; i < (channel_first + NCHAN_PER_MUX); i++) {
		ch = &edma_map[i];
		if (ch->enabled == 0) {
			break;
		}
		ch = NULL;
	}

	if (ch == NULL) {
		/* Can't find free channel */
		return (-1);
	}

	chnum = i;

	ch->enabled = 1;
	ch->mux_num = mux_num;
	ch->mux_src = mux_src;
	ch->mux_chn = (chnum - channel_first);	/* 0 to 15 */

	dmamux_configure(ch->mux_num, ch->mux_src, ch->mux_chn, 1);

	return (chnum);
}

static int
dma_stop(struct edma_softc *sc, int chnum)
{
	int reg;

	reg = READ4(sc, DMA_ERQ);
	reg &= ~(0x1 << chnum);
	WRITE4(sc, DMA_ERQ, reg);

	return (0);
}

static int
dma_setup(struct edma_softc *sc, struct tcd_conf *tcd)
{
	struct edma_channel *ch;
	int chnum;
	int reg;

	chnum = tcd->channel;

	ch = &edma_map[chnum];
	ch->ih = tcd->ih;
	ch->ih_user = tcd->ih_user;

	TCD_WRITE4(sc, DMA_TCDn_SADDR(chnum), tcd->saddr);
	TCD_WRITE4(sc, DMA_TCDn_DADDR(chnum), tcd->daddr);

	reg = (tcd->smod << TCD_ATTR_SMOD_SHIFT);
	reg |= (tcd->dmod << TCD_ATTR_DMOD_SHIFT);
	reg |= (tcd->ssize << TCD_ATTR_SSIZE_SHIFT);
	reg |= (tcd->dsize << TCD_ATTR_DSIZE_SHIFT);
	TCD_WRITE2(sc, DMA_TCDn_ATTR(chnum), reg);

	TCD_WRITE2(sc, DMA_TCDn_SOFF(chnum), tcd->soff);
	TCD_WRITE2(sc, DMA_TCDn_DOFF(chnum), tcd->doff);
	TCD_WRITE4(sc, DMA_TCDn_SLAST(chnum), tcd->slast);
	TCD_WRITE4(sc, DMA_TCDn_DLASTSGA(chnum), tcd->dlast_sga);
	TCD_WRITE4(sc, DMA_TCDn_NBYTES_MLOFFYES(chnum), tcd->nbytes);

	reg = tcd->nmajor; /* Current Major Iteration Count */
	TCD_WRITE2(sc, DMA_TCDn_CITER_ELINKNO(chnum), reg);
	TCD_WRITE2(sc, DMA_TCDn_BITER_ELINKNO(chnum), reg);

	reg = (TCD_CSR_INTMAJOR);
	if(tcd->majorelink == 1) {
		reg |= TCD_CSR_MAJORELINK;
		reg |= (tcd->majorelinkch << TCD_CSR_MAJORELINKCH_SHIFT);
	}
	TCD_WRITE2(sc, DMA_TCDn_CSR(chnum), reg);

	/* Enable requests */
	reg = READ4(sc, DMA_ERQ);
	reg |= (0x1 << chnum);
	WRITE4(sc, DMA_ERQ, reg);

	/* Enable error interrupts */
	reg = READ4(sc, DMA_EEI);
	reg |= (0x1 << chnum);
	WRITE4(sc, DMA_EEI, reg);

	return (0);
}

static int
dma_request(struct edma_softc *sc, int chnum)
{
	int reg;

	/* Start */
	reg = TCD_READ2(sc, DMA_TCDn_CSR(chnum));
	reg |= TCD_CSR_START;
	TCD_WRITE2(sc, DMA_TCDn_CSR(chnum), reg);

	return (0);
}

static int
edma_attach(device_t dev)
{
	struct edma_softc *sc;
	phandle_t node;
	int dts_value;
	int len;

	sc = device_get_softc(dev);
	sc->dev = dev;

	if ((node = ofw_bus_get_node(sc->dev)) == -1)
		return (ENXIO);

	if ((len = OF_getproplen(node, "device-id")) <= 0)
		return (ENXIO);

	OF_getencprop(node, "device-id", &dts_value, len);
	sc->device_id = dts_value;

	sc->dma_stop = dma_stop;
	sc->dma_setup = dma_setup;
	sc->dma_request = dma_request;
	sc->channel_configure = channel_configure;
	sc->channel_free = channel_free;

	if (bus_alloc_resources(dev, edma_spec, sc->res)) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	/* Memory interface */
	sc->bst = rman_get_bustag(sc->res[0]);
	sc->bsh = rman_get_bushandle(sc->res[0]);
	sc->bst_tcd = rman_get_bustag(sc->res[1]);
	sc->bsh_tcd = rman_get_bushandle(sc->res[1]);

	/* Setup interrupt handlers */
	if (bus_setup_intr(dev, sc->res[2], INTR_TYPE_BIO | INTR_MPSAFE,
		NULL, edma_transfer_complete_intr, sc, &sc->tc_ih)) {
		device_printf(dev, "Unable to alloc DMA intr resource.\n");
		return (ENXIO);
	}

	if (bus_setup_intr(dev, sc->res[3], INTR_TYPE_BIO | INTR_MPSAFE,
		NULL, edma_err_intr, sc, &sc->err_ih)) {
		device_printf(dev, "Unable to alloc DMA Err intr resource.\n");
		return (ENXIO);
	}

	return (0);
}

static device_method_t edma_methods[] = {
	DEVMETHOD(device_probe,		edma_probe),
	DEVMETHOD(device_attach,	edma_attach),
	{ 0, 0 }
};

static driver_t edma_driver = {
	"edma",
	edma_methods,
	sizeof(struct edma_softc),
};

static devclass_t edma_devclass;

DRIVER_MODULE(edma, simplebus, edma_driver, edma_devclass, 0, 0);
