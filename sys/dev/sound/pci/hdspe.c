/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012-2016 Ruslan Bukin <br@bsdpad.com>
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
 * RME HDSPe driver for FreeBSD.
 * Supported cards: AIO, RayDAT.
 */

#include <dev/sound/pcm/sound.h>
#include <dev/sound/pci/hdspe.h>
#include <dev/sound/chip.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <mixer_if.h>

SND_DECLARE_FILE("$FreeBSD$");

static struct hdspe_channel chan_map_aio[] = {
	{  0,  1,   "line", 1, 1 },
	{  6,  7,  "phone", 1, 0 },
	{  8,  9,    "aes", 1, 1 },
	{ 10, 11, "s/pdif", 1, 1 },
	{ 12, 16,   "adat", 1, 1 },

	/* Single or double speed. */
	{ 14, 18,   "adat", 1, 1 },

	/* Single speed only. */
	{ 13, 15,   "adat", 1, 1 },
	{ 17, 19,   "adat", 1, 1 },

	{  0,  0,     NULL, 0, 0 },
};

static struct hdspe_channel chan_map_rd[] = {
	{   0, 1,    "aes", 1, 1 },
	{   2, 3, "s/pdif", 1, 1 },
	{   4, 5,   "adat", 1, 1 },
	{   6, 7,   "adat", 1, 1 },
	{   8, 9,   "adat", 1, 1 },
	{ 10, 11,   "adat", 1, 1 },

	/* Single or double speed. */
	{ 12, 13,   "adat", 1, 1 },
	{ 14, 15,   "adat", 1, 1 },
	{ 16, 17,   "adat", 1, 1 },
	{ 18, 19,   "adat", 1, 1 },

	/* Single speed only. */
	{ 20, 21,   "adat", 1, 1 },
	{ 22, 23,   "adat", 1, 1 },
	{ 24, 25,   "adat", 1, 1 },
	{ 26, 27,   "adat", 1, 1 },
	{ 28, 29,   "adat", 1, 1 },
	{ 30, 31,   "adat", 1, 1 },
	{ 32, 33,   "adat", 1, 1 },
	{ 34, 35,   "adat", 1, 1 },

	{ 0,  0,      NULL, 0, 0 },
};

static void
hdspe_intr(void *p)
{
	struct sc_pcminfo *scp;
	struct sc_info *sc;
	device_t *devlist;
	int devcount;
	int status;
	int err;
	int i;

	sc = (struct sc_info *)p;

	snd_mtxlock(sc->lock);

	status = hdspe_read_1(sc, HDSPE_STATUS_REG);
	if (status & HDSPE_AUDIO_IRQ_PENDING) {
		if ((err = device_get_children(sc->dev, &devlist, &devcount)) != 0)
			return;

		for (i = 0; i < devcount; i++) {
			scp = device_get_ivars(devlist[i]);
			if (scp->ih != NULL)
				scp->ih(scp);
		}

		hdspe_write_1(sc, HDSPE_INTERRUPT_ACK, 0);
		free(devlist, M_TEMP);
	}

	snd_mtxunlock(sc->lock);
}

static void
hdspe_dmapsetmap(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	struct sc_info *sc;

	sc = (struct sc_info *)arg;

#if 0
	device_printf(sc->dev, "hdspe_dmapsetmap()\n");
#endif
}

static int
hdspe_alloc_resources(struct sc_info *sc)
{

	/* Allocate resource. */
	sc->csid = PCIR_BAR(0);
	sc->cs = bus_alloc_resource_any(sc->dev, SYS_RES_MEMORY,
	    &sc->csid, RF_ACTIVE);

	if (!sc->cs) {
		device_printf(sc->dev, "Unable to map SYS_RES_MEMORY.\n");
		return (ENXIO);
	}

	sc->cst = rman_get_bustag(sc->cs);
	sc->csh = rman_get_bushandle(sc->cs);

	/* Allocate interrupt resource. */
	sc->irqid = 0;
	sc->irq = bus_alloc_resource_any(sc->dev, SYS_RES_IRQ, &sc->irqid,
	    RF_ACTIVE | RF_SHAREABLE);

	if (!sc->irq ||
	    bus_setup_intr(sc->dev, sc->irq, INTR_MPSAFE | INTR_TYPE_AV,
		NULL, hdspe_intr, sc, &sc->ih)) {
		device_printf(sc->dev, "Unable to alloc interrupt resource.\n");
		return (ENXIO);
	}

	/* Allocate DMA resources. */
	if (bus_dma_tag_create(/*parent*/bus_get_dma_tag(sc->dev),
		/*alignment*/4,
		/*boundary*/0,
		/*lowaddr*/BUS_SPACE_MAXADDR_32BIT,
		/*highaddr*/BUS_SPACE_MAXADDR,
		/*filter*/NULL,
		/*filterarg*/NULL,
		/*maxsize*/2 * HDSPE_DMASEGSIZE,
		/*nsegments*/2,
		/*maxsegsz*/HDSPE_DMASEGSIZE,
		/*flags*/0,
		/*lockfunc*/busdma_lock_mutex,
		/*lockarg*/&Giant,
		/*dmatag*/&sc->dmat) != 0) {
		device_printf(sc->dev, "Unable to create dma tag.\n");
		return (ENXIO);
	}

	sc->bufsize = HDSPE_DMASEGSIZE;

	/* pbuf (play buffer). */
	if (bus_dmamem_alloc(sc->dmat, (void **)&sc->pbuf,
		BUS_DMA_NOWAIT, &sc->pmap)) {
		device_printf(sc->dev, "Can't alloc pbuf.\n");
		return (ENXIO);
	}

	if (bus_dmamap_load(sc->dmat, sc->pmap, sc->pbuf, sc->bufsize,
		hdspe_dmapsetmap, sc, 0)) {
		device_printf(sc->dev, "Can't load pbuf.\n");
		return (ENXIO);
	}

	/* rbuf (rec buffer). */
	if (bus_dmamem_alloc(sc->dmat, (void **)&sc->rbuf,
		BUS_DMA_NOWAIT, &sc->rmap)) {
		device_printf(sc->dev, "Can't alloc rbuf.\n");
		return (ENXIO);
	}

	if (bus_dmamap_load(sc->dmat, sc->rmap, sc->rbuf, sc->bufsize,
		hdspe_dmapsetmap, sc, 0)) {
		device_printf(sc->dev, "Can't load rbuf.\n");
		return (ENXIO);
	}

	bzero(sc->pbuf, sc->bufsize);
	bzero(sc->rbuf, sc->bufsize);

	return (0);
}

static void
hdspe_map_dmabuf(struct sc_info *sc)
{
	uint32_t paddr, raddr;
	int i;

	paddr = vtophys(sc->pbuf);
	raddr = vtophys(sc->rbuf);

	for (i = 0; i < HDSPE_MAX_SLOTS * 16; i++) {
		hdspe_write_4(sc, HDSPE_PAGE_ADDR_BUF_OUT + 4 * i,
                    paddr + i * 4096);
		hdspe_write_4(sc, HDSPE_PAGE_ADDR_BUF_IN + 4 * i,
                    raddr + i * 4096);
	}
}

static int
hdspe_probe(device_t dev)
{
	uint32_t rev;

	if (pci_get_vendor(dev) == PCI_VENDOR_XILINX &&
	    pci_get_device(dev) == PCI_DEVICE_XILINX_HDSPE) {
		rev = pci_get_revid(dev);
		switch (rev) {
		case PCI_REVISION_AIO:
			device_set_desc(dev, "RME HDSPe AIO");
			return (0);
		case PCI_REVISION_RAYDAT:
			device_set_desc(dev, "RME HDSPe RayDAT");
			return (0);
		}
	}

	return (ENXIO);
}

static int
hdspe_init(struct sc_info *sc)
{
	long long period;

	/* Set defaults. */
	sc->ctrl_register |= HDSPM_CLOCK_MODE_MASTER;

	/* Set latency. */
	sc->period = 32;
	sc->ctrl_register = hdspe_encode_latency(7);

	/* Set rate. */
	sc->speed = HDSPE_SPEED_DEFAULT;
	sc->ctrl_register &= ~HDSPE_FREQ_MASK;
	sc->ctrl_register |= HDSPE_FREQ_MASK_DEFAULT;
	hdspe_write_4(sc, HDSPE_CONTROL_REG, sc->ctrl_register);

	switch (sc->type) {
	case RAYDAT:
	case AIO:
		period = HDSPE_FREQ_AIO;
		break;
	default:
		return (ENXIO);
	}

	/* Set DDS value. */
	period /= sc->speed;
	hdspe_write_4(sc, HDSPE_FREQ_REG, period);

	/* Other settings. */
	sc->settings_register = 0;
	hdspe_write_4(sc, HDSPE_SETTINGS_REG, sc->settings_register);

	return (0);
}

static int
hdspe_attach(device_t dev)
{
	struct hdspe_channel *chan_map;
	struct sc_pcminfo *scp;
	struct sc_info *sc;
	uint32_t rev;
	int i, err;

#if 0
	device_printf(dev, "hdspe_attach()\n");
#endif

	sc = device_get_softc(dev);
	sc->lock = snd_mtxcreate(device_get_nameunit(dev),
	    "snd_hdspe softc");
	sc->dev = dev;

	pci_enable_busmaster(dev);
	rev = pci_get_revid(dev);
	switch (rev) {
	case PCI_REVISION_AIO:
		sc->type = AIO;
		chan_map = chan_map_aio;
		break;
	case PCI_REVISION_RAYDAT:
		sc->type = RAYDAT;
		chan_map = chan_map_rd;
		break;
	default:
		return (ENXIO);
	}

	/* Allocate resources. */
	err = hdspe_alloc_resources(sc);
	if (err) {
		device_printf(dev, "Unable to allocate system resources.\n");
		return (ENXIO);
	}

	if (hdspe_init(sc) != 0)
		return (ENXIO);

	for (i = 0; i < HDSPE_MAX_CHANS && chan_map[i].descr != NULL; i++) {
		scp = malloc(sizeof(struct sc_pcminfo), M_DEVBUF, M_NOWAIT | M_ZERO);
		scp->hc = &chan_map[i];
		scp->sc = sc;
		scp->dev = device_add_child(dev, "pcm", -1);
		device_set_ivars(scp->dev, scp);
	}

	hdspe_map_dmabuf(sc);

	return (bus_generic_attach(dev));
}

static void
hdspe_dmafree(struct sc_info *sc)
{

	bus_dmamap_unload(sc->dmat, sc->rmap);
	bus_dmamap_unload(sc->dmat, sc->pmap);
	bus_dmamem_free(sc->dmat, sc->rbuf, sc->rmap);
	bus_dmamem_free(sc->dmat, sc->pbuf, sc->pmap);
	sc->rbuf = sc->pbuf = NULL;
}

static int
hdspe_detach(device_t dev)
{
	struct sc_info *sc;
	int err;

	sc = device_get_softc(dev);
	if (sc == NULL) {
		device_printf(dev,"Can't detach: softc is null.\n");
		return (0);
	}

	err = device_delete_children(dev);
	if (err)
		return (err);

	hdspe_dmafree(sc);

	if (sc->ih)
		bus_teardown_intr(dev, sc->irq, sc->ih);
	if (sc->dmat)
		bus_dma_tag_destroy(sc->dmat);
	if (sc->irq)
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->irq);
	if (sc->cs)
		bus_release_resource(dev, SYS_RES_MEMORY, PCIR_BAR(0), sc->cs);
	if (sc->lock)
		snd_mtxfree(sc->lock);

	return (0);
}

static device_method_t hdspe_methods[] = {
	DEVMETHOD(device_probe,     hdspe_probe),
	DEVMETHOD(device_attach,    hdspe_attach),
	DEVMETHOD(device_detach,    hdspe_detach),
	{ 0, 0 }
};

static driver_t hdspe_driver = {
	"hdspe",
	hdspe_methods,
	PCM_SOFTC_SIZE,
};

static devclass_t hdspe_devclass;

DRIVER_MODULE(snd_hdspe, pci, hdspe_driver, hdspe_devclass, 0, 0);
