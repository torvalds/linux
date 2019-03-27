/*-
 * Copyright (c) 2016-2018 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract FA8750-10-C-0237
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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

/* Altera mSGDMA driver. */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_platform.h"
#include <sys/param.h>
#include <sys/endian.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/sglist.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/resource.h>
#include <sys/rman.h>

#include <machine/bus.h>
#include <machine/fdt.h>
#include <machine/cache.h>

#ifdef FDT
#include <dev/fdt/fdt_common.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#endif

#include <dev/xdma/xdma.h>
#include "xdma_if.h"

#include <dev/altera/msgdma/msgdma.h>

#define MSGDMA_DEBUG
#undef MSGDMA_DEBUG

#ifdef MSGDMA_DEBUG
#define dprintf(fmt, ...)  printf(fmt, ##__VA_ARGS__)
#else
#define dprintf(fmt, ...)
#endif

#define	MSGDMA_NCHANNELS	1

struct msgdma_channel {
	struct msgdma_softc	*sc;
	struct mtx		mtx;
	xdma_channel_t		*xchan;
	struct proc		*p;
	int			used;
	int			index;
	int			idx_head;
	int			idx_tail;

	struct msgdma_desc	**descs;
	bus_dma_segment_t	*descs_phys;
	uint32_t		descs_num;
	bus_dma_tag_t		dma_tag;
	bus_dmamap_t		*dma_map;
	uint32_t		map_descr;
	uint8_t			map_err;
	uint32_t		descs_used_count;
};

struct msgdma_softc {
	device_t		dev;
	struct resource		*res[3];
	bus_space_tag_t		bst;
	bus_space_handle_t	bsh;
	bus_space_tag_t		bst_d;
	bus_space_handle_t	bsh_d;
	void			*ih;
	struct msgdma_desc	desc;
	struct msgdma_channel	channels[MSGDMA_NCHANNELS];
};

static struct resource_spec msgdma_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_MEMORY,	1,	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,	RF_ACTIVE },
	{ -1, 0 }
};

#define	HWTYPE_NONE	0
#define	HWTYPE_STD	1

static struct ofw_compat_data compat_data[] = {
	{ "altr,msgdma-16.0",	HWTYPE_STD },
	{ "altr,msgdma-1.0",	HWTYPE_STD },
	{ NULL,			HWTYPE_NONE },
};

static int msgdma_probe(device_t dev);
static int msgdma_attach(device_t dev);
static int msgdma_detach(device_t dev);

static inline uint32_t
msgdma_next_desc(struct msgdma_channel *chan, uint32_t curidx)
{

	return ((curidx + 1) % chan->descs_num);
}

static void
msgdma_intr(void *arg)
{
	xdma_transfer_status_t status;
	struct xdma_transfer_status st;
	struct msgdma_desc *desc;
	struct msgdma_channel *chan;
	struct xdma_channel *xchan;
	struct msgdma_softc *sc;
	uint32_t tot_copied;

	sc = arg;
	chan = &sc->channels[0];
	xchan = chan->xchan;

	dprintf("%s(%d): status 0x%08x next_descr 0x%08x, control 0x%08x\n",
	    __func__, device_get_unit(sc->dev),
		READ4_DESC(sc, PF_STATUS),
		READ4_DESC(sc, PF_NEXT_LO),
		READ4_DESC(sc, PF_CONTROL));

	tot_copied = 0;

	while (chan->idx_tail != chan->idx_head) {
		dprintf("%s: idx_tail %d idx_head %d\n", __func__,
		    chan->idx_tail, chan->idx_head);
		bus_dmamap_sync(chan->dma_tag, chan->dma_map[chan->idx_tail],
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

		desc = chan->descs[chan->idx_tail];
		if ((le32toh(desc->control) & CONTROL_OWN) != 0) {
			break;
		}

		tot_copied += le32toh(desc->transferred);
		st.error = 0;
		st.transferred = le32toh(desc->transferred);
		xchan_seg_done(xchan, &st);

		chan->idx_tail = msgdma_next_desc(chan, chan->idx_tail);
		atomic_subtract_int(&chan->descs_used_count, 1);
	}

	WRITE4_DESC(sc, PF_STATUS, PF_STATUS_IRQ);

	/* Finish operation */
	status.error = 0;
	status.transferred = tot_copied;
	xdma_callback(chan->xchan, &status);
}

static int
msgdma_reset(struct msgdma_softc *sc)
{
	int timeout;

	dprintf("%s: read status: %x\n", __func__, READ4(sc, 0x00));
	dprintf("%s: read control: %x\n", __func__, READ4(sc, 0x04));
	dprintf("%s: read 1: %x\n", __func__, READ4(sc, 0x08));
	dprintf("%s: read 2: %x\n", __func__, READ4(sc, 0x0C));

	WRITE4(sc, DMA_CONTROL, CONTROL_RESET);

	timeout = 100;
	do {
		if ((READ4(sc, DMA_STATUS) & STATUS_RESETTING) == 0)
			break;
	} while (timeout--);

	dprintf("timeout %d\n", timeout);

	if (timeout == 0)
		return (-1);

	dprintf("%s: read control after reset: %x\n",
	    __func__, READ4(sc, DMA_CONTROL));

	return (0);
}

static int
msgdma_probe(device_t dev)
{
	int hwtype;

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	hwtype = ofw_bus_search_compatible(dev, compat_data)->ocd_data;
	if (hwtype == HWTYPE_NONE)
		return (ENXIO);

	device_set_desc(dev, "Altera mSGDMA");

	return (BUS_PROBE_DEFAULT);
}

static int
msgdma_attach(device_t dev)
{
	struct msgdma_softc *sc;
	phandle_t xref, node;
	int err;

	sc = device_get_softc(dev);
	sc->dev = dev;

	if (bus_alloc_resources(dev, msgdma_spec, sc->res)) {
		device_printf(dev, "could not allocate resources for device\n");
		return (ENXIO);
	}

	/* CSR memory interface */
	sc->bst = rman_get_bustag(sc->res[0]);
	sc->bsh = rman_get_bushandle(sc->res[0]);

	/* Descriptor memory interface */
	sc->bst_d = rman_get_bustag(sc->res[1]);
	sc->bsh_d = rman_get_bushandle(sc->res[1]);

	/* Setup interrupt handler */
	err = bus_setup_intr(dev, sc->res[2], INTR_TYPE_MISC | INTR_MPSAFE,
	    NULL, msgdma_intr, sc, &sc->ih);
	if (err) {
		device_printf(dev, "Unable to alloc interrupt resource.\n");
		return (ENXIO);
	}

	node = ofw_bus_get_node(dev);
	xref = OF_xref_from_node(node);
	OF_device_register_xref(xref, dev);

	if (msgdma_reset(sc) != 0)
		return (-1);

	WRITE4(sc, DMA_CONTROL, CONTROL_GIEM);

	return (0);
}

static int
msgdma_detach(device_t dev)
{
	struct msgdma_softc *sc;

	sc = device_get_softc(dev);

	return (0);
}

static void
msgdma_dmamap_cb(void *arg, bus_dma_segment_t *segs, int nseg, int err)
{
	struct msgdma_channel *chan;

	chan = (struct msgdma_channel *)arg;
	KASSERT(chan != NULL, ("xchan is NULL"));

	if (err) {
		chan->map_err = 1;
		return;
	}

	chan->descs_phys[chan->map_descr].ds_addr = segs[0].ds_addr;
	chan->descs_phys[chan->map_descr].ds_len = segs[0].ds_len;

	dprintf("map desc %d: descs phys %lx len %ld\n",
	    chan->map_descr, segs[0].ds_addr, segs[0].ds_len);
}

static int
msgdma_desc_free(struct msgdma_softc *sc, struct msgdma_channel *chan)
{
	struct msgdma_desc *desc;
	int nsegments;
	int i;

	nsegments = chan->descs_num;

	for (i = 0; i < nsegments; i++) {
		desc = chan->descs[i];
		bus_dmamap_unload(chan->dma_tag, chan->dma_map[i]);
		bus_dmamem_free(chan->dma_tag, desc, chan->dma_map[i]);
	}

	bus_dma_tag_destroy(chan->dma_tag);
	free(chan->descs, M_DEVBUF);
	free(chan->dma_map, M_DEVBUF);
	free(chan->descs_phys, M_DEVBUF);

	return (0);
}

static int
msgdma_desc_alloc(struct msgdma_softc *sc, struct msgdma_channel *chan,
    uint32_t desc_size, uint32_t align)
{
	int nsegments;
	int err;
	int i;

	nsegments = chan->descs_num;

	dprintf("%s: nseg %d\n", __func__, nsegments);

	err = bus_dma_tag_create(
	    bus_get_dma_tag(sc->dev),
	    align, 0,			/* alignment, boundary */
	    BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    desc_size, 1,		/* maxsize, nsegments*/
	    desc_size, 0,		/* maxsegsize, flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &chan->dma_tag);
	if (err) {
		device_printf(sc->dev,
		    "%s: Can't create bus_dma tag.\n", __func__);
		return (-1);
	}

	/* Descriptors. */
	chan->descs = malloc(nsegments * sizeof(struct msgdma_desc *),
	    M_DEVBUF, (M_WAITOK | M_ZERO));
	if (chan->descs == NULL) {
		device_printf(sc->dev,
		    "%s: Can't allocate memory.\n", __func__);
		return (-1);
	}
	chan->dma_map = malloc(nsegments * sizeof(bus_dmamap_t),
	    M_DEVBUF, (M_WAITOK | M_ZERO));
	chan->descs_phys = malloc(nsegments * sizeof(bus_dma_segment_t),
	    M_DEVBUF, (M_WAITOK | M_ZERO));

	/* Allocate bus_dma memory for each descriptor. */
	for (i = 0; i < nsegments; i++) {
		err = bus_dmamem_alloc(chan->dma_tag, (void **)&chan->descs[i],
		    BUS_DMA_WAITOK | BUS_DMA_ZERO, &chan->dma_map[i]);
		if (err) {
			device_printf(sc->dev,
			    "%s: Can't allocate memory for descriptors.\n",
			    __func__);
			return (-1);
		}

		chan->map_err = 0;
		chan->map_descr = i;
		err = bus_dmamap_load(chan->dma_tag, chan->dma_map[i], chan->descs[i],
		    desc_size, msgdma_dmamap_cb, chan, BUS_DMA_WAITOK);
		if (err) {
			device_printf(sc->dev,
			    "%s: Can't load DMA map.\n", __func__);
			return (-1);
		}

		if (chan->map_err != 0) {
			device_printf(sc->dev,
			    "%s: Can't load DMA map.\n", __func__);
			return (-1);
		}
	}

	return (0);
}


static int
msgdma_channel_alloc(device_t dev, struct xdma_channel *xchan)
{
	struct msgdma_channel *chan;
	struct msgdma_softc *sc;
	int i;

	sc = device_get_softc(dev);

	for (i = 0; i < MSGDMA_NCHANNELS; i++) {
		chan = &sc->channels[i];
		if (chan->used == 0) {
			chan->xchan = xchan;
			xchan->chan = (void *)chan;
			xchan->caps |= XCHAN_CAP_BUSDMA;
			chan->index = i;
			chan->sc = sc;
			chan->used = 1;
			chan->idx_head = 0;
			chan->idx_tail = 0;
			chan->descs_used_count = 0;
			chan->descs_num = 1024;

			return (0);
		}
	}

	return (-1);
}

static int
msgdma_channel_free(device_t dev, struct xdma_channel *xchan)
{
	struct msgdma_channel *chan;
	struct msgdma_softc *sc;

	sc = device_get_softc(dev);

	chan = (struct msgdma_channel *)xchan->chan;

	msgdma_desc_free(sc, chan);

	chan->used = 0;

	return (0);
}

static int
msgdma_channel_capacity(device_t dev, xdma_channel_t *xchan,
    uint32_t *capacity)
{
	struct msgdma_channel *chan;
	uint32_t c;

	chan = (struct msgdma_channel *)xchan->chan;

	/* At least one descriptor must be left empty. */
	c = (chan->descs_num - chan->descs_used_count - 1);

	*capacity = c;

	return (0);
}

static int
msgdma_channel_submit_sg(device_t dev, struct xdma_channel *xchan,
    struct xdma_sglist *sg, uint32_t sg_n)
{
	struct msgdma_channel *chan;
	struct msgdma_desc *desc;
	struct msgdma_softc *sc;
	uint32_t src_addr_lo;
	uint32_t dst_addr_lo;
	uint32_t len;
	uint32_t tmp;
	int i;

	sc = device_get_softc(dev);

	chan = (struct msgdma_channel *)xchan->chan;

	for (i = 0; i < sg_n; i++) {
		src_addr_lo = (uint32_t)sg[i].src_addr;
		dst_addr_lo = (uint32_t)sg[i].dst_addr;
		len = (uint32_t)sg[i].len;

		dprintf("%s: src %x dst %x len %d\n", __func__,
		    src_addr_lo, dst_addr_lo, len);

		desc = chan->descs[chan->idx_head];
		desc->read_lo = htole32(src_addr_lo);
		desc->write_lo = htole32(dst_addr_lo);
		desc->length = htole32(len);
		desc->transferred = 0;
		desc->status = 0;
		desc->reserved = 0;
		desc->control = 0;

		if (sg[i].direction == XDMA_MEM_TO_DEV) {
			if (sg[i].first == 1) {
				desc->control |= htole32(CONTROL_GEN_SOP);
			}

			if (sg[i].last == 1) {
				desc->control |= htole32(CONTROL_GEN_EOP);
				desc->control |= htole32(CONTROL_TC_IRQ_EN |
				    CONTROL_ET_IRQ_EN | CONTROL_ERR_M);
			}
		} else {
			desc->control |= htole32(CONTROL_END_ON_EOP | (1 << 13));
			desc->control |= htole32(CONTROL_TC_IRQ_EN |
			    CONTROL_ET_IRQ_EN | CONTROL_ERR_M);
		}

		tmp = chan->idx_head;

		atomic_add_int(&chan->descs_used_count, 1);
		chan->idx_head = msgdma_next_desc(chan, chan->idx_head);

		desc->control |= htole32(CONTROL_OWN | CONTROL_GO);

		bus_dmamap_sync(chan->dma_tag, chan->dma_map[tmp],
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	}

	return (0);
}

static int
msgdma_channel_prep_sg(device_t dev, struct xdma_channel *xchan)
{
	struct msgdma_channel *chan;
	struct msgdma_desc *desc;
	struct msgdma_softc *sc;
	uint32_t addr;
	uint32_t reg;
	int ret;
	int i;

	sc = device_get_softc(dev);

	dprintf("%s(%d)\n", __func__, device_get_unit(dev));

	chan = (struct msgdma_channel *)xchan->chan;

	ret = msgdma_desc_alloc(sc, chan, sizeof(struct msgdma_desc), 16);
	if (ret != 0) {
		device_printf(sc->dev,
		    "%s: Can't allocate descriptors.\n", __func__);
		return (-1);
	}

	for (i = 0; i < chan->descs_num; i++) {
		desc = chan->descs[i];

		if (i == (chan->descs_num - 1)) {
			desc->next = htole32(chan->descs_phys[0].ds_addr);
		} else {
			desc->next = htole32(chan->descs_phys[i+1].ds_addr);
		}

		dprintf("%s(%d): desc %d vaddr %lx next paddr %x\n", __func__,
		    device_get_unit(dev), i, (uint64_t)desc, le32toh(desc->next));
	}

	addr = chan->descs_phys[0].ds_addr;
	WRITE4_DESC(sc, PF_NEXT_LO, addr);
	WRITE4_DESC(sc, PF_NEXT_HI, 0);
	WRITE4_DESC(sc, PF_POLL_FREQ, 1000);

	reg = (PF_CONTROL_GIEM | PF_CONTROL_DESC_POLL_EN);
	reg |= PF_CONTROL_RUN;
	WRITE4_DESC(sc, PF_CONTROL, reg);

	return (0);
}

static int
msgdma_channel_control(device_t dev, xdma_channel_t *xchan, int cmd)
{
	struct msgdma_channel *chan;
	struct msgdma_softc *sc;

	sc = device_get_softc(dev);

	chan = (struct msgdma_channel *)xchan->chan;

	switch (cmd) {
	case XDMA_CMD_BEGIN:
	case XDMA_CMD_TERMINATE:
	case XDMA_CMD_PAUSE:
		/* TODO: implement me */
		return (-1);
	}

	return (0);
}

#ifdef FDT
static int
msgdma_ofw_md_data(device_t dev, pcell_t *cells, int ncells, void **ptr)
{

	return (0);
}
#endif

static device_method_t msgdma_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,			msgdma_probe),
	DEVMETHOD(device_attach,		msgdma_attach),
	DEVMETHOD(device_detach,		msgdma_detach),

	/* xDMA Interface */
	DEVMETHOD(xdma_channel_alloc,		msgdma_channel_alloc),
	DEVMETHOD(xdma_channel_free,		msgdma_channel_free),
	DEVMETHOD(xdma_channel_control,		msgdma_channel_control),

	/* xDMA SG Interface */
	DEVMETHOD(xdma_channel_capacity,	msgdma_channel_capacity),
	DEVMETHOD(xdma_channel_prep_sg,		msgdma_channel_prep_sg),
	DEVMETHOD(xdma_channel_submit_sg,	msgdma_channel_submit_sg),

#ifdef FDT
	DEVMETHOD(xdma_ofw_md_data,		msgdma_ofw_md_data),
#endif

	DEVMETHOD_END
};

static driver_t msgdma_driver = {
	"msgdma",
	msgdma_methods,
	sizeof(struct msgdma_softc),
};

static devclass_t msgdma_devclass;

EARLY_DRIVER_MODULE(msgdma, simplebus, msgdma_driver, msgdma_devclass, 0, 0,
    BUS_PASS_INTERRUPT + BUS_PASS_ORDER_LATE);
