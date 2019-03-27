/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Oleksandr Tymoshenko <gonzo@freebsd.org>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/sx.h>
#include <sys/rman.h>
#include <machine/bus.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <arm/broadcom/bcm2835/bcm2835_mbox.h>
#include <arm/broadcom/bcm2835/bcm2835_mbox_prop.h>
#include <arm/broadcom/bcm2835/bcm2835_vcbus.h>

#include "mbox_if.h"

#define	REG_READ	0x00
#define	REG_POL		0x10
#define	REG_SENDER	0x14
#define	REG_STATUS	0x18
#define		STATUS_FULL	0x80000000
#define		STATUS_EMPTY	0x40000000
#define	REG_CONFIG	0x1C
#define		CONFIG_DATA_IRQ	0x00000001
#define	REG_WRITE	0x20 /* This is Mailbox 1 address */

#define	MBOX_MSG(chan, data)	(((data) & ~0xf) | ((chan) & 0xf))
#define	MBOX_CHAN(msg)		((msg) & 0xf)
#define	MBOX_DATA(msg)		((msg) & ~0xf)

#define	MBOX_LOCK(sc)	do {	\
	mtx_lock(&(sc)->lock);	\
} while(0)

#define	MBOX_UNLOCK(sc)	do {		\
	mtx_unlock(&(sc)->lock);	\
} while(0)

#ifdef  DEBUG
#define dprintf(fmt, args...) printf(fmt, ##args)
#else
#define dprintf(fmt, args...)
#endif

struct bcm_mbox_softc {
	struct mtx		lock;
	struct resource *	mem_res;
	struct resource *	irq_res;
	void*			intr_hl;
	bus_space_tag_t		bst;
	bus_space_handle_t	bsh;
	int			msg[BCM2835_MBOX_CHANS];
	int			have_message[BCM2835_MBOX_CHANS];
	struct sx		property_chan_lock;
};

#define	mbox_read_4(sc, reg)		\
    bus_space_read_4((sc)->bst, (sc)->bsh, reg)
#define	mbox_write_4(sc, reg, val)		\
    bus_space_write_4((sc)->bst, (sc)->bsh, reg, val)

static struct ofw_compat_data compat_data[] = {
	{"broadcom,bcm2835-mbox",	1},
	{"brcm,bcm2835-mbox",		1},
	{NULL,				0}
};

static int
bcm_mbox_read_msg(struct bcm_mbox_softc *sc, int *ochan)
{
#ifdef DEBUG
	uint32_t data;
#endif
	uint32_t msg;
	int chan;

	msg = mbox_read_4(sc, REG_READ);
	dprintf("bcm_mbox_intr: raw data %08x\n", msg);
	chan = MBOX_CHAN(msg);
#ifdef DEBUG
	data = MBOX_DATA(msg);
#endif
	if (sc->msg[chan]) {
		printf("bcm_mbox_intr: channel %d oveflow\n", chan);
		return (1);
	}
	dprintf("bcm_mbox_intr: chan %d, data %08x\n", chan, data);
	sc->msg[chan] = msg;

	if (ochan != NULL)
		*ochan = chan;

	return (0);
}

static void
bcm_mbox_intr(void *arg)
{
	struct bcm_mbox_softc *sc = arg;
	int chan;

	MBOX_LOCK(sc);
	while (!(mbox_read_4(sc, REG_STATUS) & STATUS_EMPTY))
		if (bcm_mbox_read_msg(sc, &chan) == 0) {
			sc->have_message[chan] = 1;
			wakeup(&sc->have_message[chan]);
		}
	MBOX_UNLOCK(sc);
}

static int
bcm_mbox_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "BCM2835 VideoCore Mailbox");

	return (BUS_PROBE_DEFAULT);
}

static int
bcm_mbox_attach(device_t dev)
{
	struct bcm_mbox_softc *sc = device_get_softc(dev);
	int i;
	int rid = 0;

	sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
	if (sc->mem_res == NULL) {
		device_printf(dev, "could not allocate memory resource\n");
		return (ENXIO);
	}

	sc->bst = rman_get_bustag(sc->mem_res);
	sc->bsh = rman_get_bushandle(sc->mem_res);

	rid = 0;
	sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid, RF_ACTIVE);
	if (sc->irq_res == NULL) {
		device_printf(dev, "could not allocate interrupt resource\n");
		return (ENXIO);
	}

	/* Setup and enable the timer */
	if (bus_setup_intr(dev, sc->irq_res, INTR_MPSAFE | INTR_TYPE_MISC, 
	    NULL, bcm_mbox_intr, sc, &sc->intr_hl) != 0) {
		bus_release_resource(dev, SYS_RES_IRQ, rid, sc->irq_res);
		device_printf(dev, "Unable to setup the clock irq handler.\n");
		return (ENXIO);
	}

	mtx_init(&sc->lock, "vcio mbox", NULL, MTX_DEF);
	for (i = 0; i < BCM2835_MBOX_CHANS; i++) {
		sc->msg[i] = 0;
		sc->have_message[i] = 0;
	}

	sx_init(&sc->property_chan_lock, "mboxprop");

	/* Read all pending messages */
	while ((mbox_read_4(sc, REG_STATUS) & STATUS_EMPTY) == 0)
		(void)mbox_read_4(sc, REG_READ);

	mbox_write_4(sc, REG_CONFIG, CONFIG_DATA_IRQ);

	return (0);
}

/* 
 * Mailbox API
 */
static int
bcm_mbox_write(device_t dev, int chan, uint32_t data)
{
	int limit = 1000;
	struct bcm_mbox_softc *sc = device_get_softc(dev);

	dprintf("bcm_mbox_write: chan %d, data %08x\n", chan, data);
	MBOX_LOCK(sc);
	sc->have_message[chan] = 0;
	while ((mbox_read_4(sc, REG_STATUS) & STATUS_FULL) && --limit)
		DELAY(5);
	if (limit == 0) {
		printf("bcm_mbox_write: STATUS_FULL stuck");
		MBOX_UNLOCK(sc);
		return (EAGAIN);
	}
	mbox_write_4(sc, REG_WRITE, MBOX_MSG(chan, data));
	MBOX_UNLOCK(sc);

	return (0);
}

static int
bcm_mbox_read(device_t dev, int chan, uint32_t *data)
{
	struct bcm_mbox_softc *sc = device_get_softc(dev);
	int err, read_chan;

	dprintf("bcm_mbox_read: chan %d\n", chan);

	err = 0;
	MBOX_LOCK(sc);
	if (!cold) {
		if (sc->have_message[chan] == 0) {
			if (mtx_sleep(&sc->have_message[chan], &sc->lock, 0,
			    "mbox", 10*hz) != 0) {
				device_printf(dev, "timeout waiting for message on chan %d\n", chan);
				err = ETIMEDOUT;
			}
		}
	} else {
		do {
			/* Wait for a message */
			while ((mbox_read_4(sc, REG_STATUS) & STATUS_EMPTY))
				;
			/* Read the message */
			if (bcm_mbox_read_msg(sc, &read_chan) != 0) {
				err = EINVAL;
				goto out;
			}
		} while (read_chan != chan);
	}
	/*
	 *  get data from intr handler, the same channel is never coming
	 *  because of holding sc lock.
	 */
	*data = MBOX_DATA(sc->msg[chan]);
	sc->msg[chan] = 0;
	sc->have_message[chan] = 0;
out:
	MBOX_UNLOCK(sc);
	dprintf("bcm_mbox_read: chan %d, data %08x\n", chan, *data);

	return (err);
}

static device_method_t bcm_mbox_methods[] = {
	DEVMETHOD(device_probe,		bcm_mbox_probe),
	DEVMETHOD(device_attach,	bcm_mbox_attach),

	DEVMETHOD(mbox_read,		bcm_mbox_read),
	DEVMETHOD(mbox_write,		bcm_mbox_write),

	DEVMETHOD_END
};

static driver_t bcm_mbox_driver = {
	"mbox",
	bcm_mbox_methods,
	sizeof(struct bcm_mbox_softc),
};

static devclass_t bcm_mbox_devclass;

DRIVER_MODULE(mbox, simplebus, bcm_mbox_driver, bcm_mbox_devclass, 0, 0);

static void
bcm2835_mbox_dma_cb(void *arg, bus_dma_segment_t *segs, int nseg, int err)
{
	bus_addr_t *addr;

	if (err)
		return;
	addr = (bus_addr_t *)arg;
	*addr = PHYS_TO_VCBUS(segs[0].ds_addr);
}

static void *
bcm2835_mbox_init_dma(device_t dev, size_t len, bus_dma_tag_t *tag,
    bus_dmamap_t *map, bus_addr_t *phys)
{
	void *buf;
	int err;

	err = bus_dma_tag_create(bus_get_dma_tag(dev), 16, 0,
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL,
	    len, 1, len, 0, NULL, NULL, tag);
	if (err != 0) {
		device_printf(dev, "can't create DMA tag\n");
		return (NULL);
	}

	err = bus_dmamem_alloc(*tag, &buf, 0, map);
	if (err != 0) {
		bus_dma_tag_destroy(*tag);
		device_printf(dev, "can't allocate dmamem\n");
		return (NULL);
	}

	err = bus_dmamap_load(*tag, *map, buf, len, bcm2835_mbox_dma_cb,
	    phys, 0);
	if (err != 0) {
		bus_dmamem_free(*tag, buf, *map);
		bus_dma_tag_destroy(*tag);
		device_printf(dev, "can't load DMA map\n");
		return (NULL);
	}

	return (buf);
}

static int
bcm2835_mbox_err(device_t dev, bus_addr_t msg_phys, uint32_t resp_phys,
	struct bcm2835_mbox_hdr *msg, size_t len)
{
	int idx;
	struct bcm2835_mbox_tag_hdr *tag;
	uint8_t *last;

	if ((uint32_t)msg_phys != resp_phys) {
		device_printf(dev, "response channel mismatch\n");
		return (EIO);
	}
	if (msg->code != BCM2835_MBOX_CODE_RESP_SUCCESS) {
		device_printf(dev, "mbox response error\n");
		return (EIO);
	}

	/* Loop until the end tag. */
	tag = (struct bcm2835_mbox_tag_hdr *)(msg + 1);
	last = (uint8_t *)msg + len;
	for (idx = 0; tag->tag != 0; idx++) {
		if ((tag->val_len & BCM2835_MBOX_TAG_VAL_LEN_RESPONSE) == 0) {
			device_printf(dev, "tag %d response error\n", idx);
			return (EIO);
		}
		/* Clear the response bit. */
		tag->val_len &= ~BCM2835_MBOX_TAG_VAL_LEN_RESPONSE;

		/* Next tag. */
		tag = (struct bcm2835_mbox_tag_hdr *)((uint8_t *)tag +
		    sizeof(*tag) + tag->val_buf_size);

		if ((uint8_t *)tag > last) {
			device_printf(dev, "mbox buffer size error\n");
			return (EIO);
		}
	}

	return (0);
}

int
bcm2835_mbox_property(void *msg, size_t msg_size)
{
	struct bcm_mbox_softc *sc;
	struct msg_set_power_state *buf;
	bus_dma_tag_t msg_tag;
	bus_dmamap_t msg_map;
	bus_addr_t msg_phys;
	uint32_t reg;
	device_t mbox;
	int err;

	/* get mbox device */
	mbox = devclass_get_device(devclass_find("mbox"), 0);
	if (mbox == NULL)
		return (ENXIO);

	sc = device_get_softc(mbox);
	sx_xlock(&sc->property_chan_lock);

	/* Allocate memory for the message */
	buf = bcm2835_mbox_init_dma(mbox, msg_size, &msg_tag, &msg_map,
	    &msg_phys);
	if (buf == NULL) {
		err = ENOMEM;
		goto out;
	}

	memcpy(buf, msg, msg_size);

	bus_dmamap_sync(msg_tag, msg_map,
	    BUS_DMASYNC_PREWRITE);

	MBOX_WRITE(mbox, BCM2835_MBOX_CHAN_PROP, (uint32_t)msg_phys);
	MBOX_READ(mbox, BCM2835_MBOX_CHAN_PROP, &reg);

	bus_dmamap_sync(msg_tag, msg_map,
	    BUS_DMASYNC_PREREAD);

	memcpy(msg, buf, msg_size);

	err = bcm2835_mbox_err(mbox, msg_phys, reg,
	    (struct bcm2835_mbox_hdr *)msg, msg_size);

	bus_dmamap_unload(msg_tag, msg_map);
	bus_dmamem_free(msg_tag, buf, msg_map);
	bus_dma_tag_destroy(msg_tag);
out:
	sx_xunlock(&sc->property_chan_lock);
	return (err);
}

int
bcm2835_mbox_set_power_state(uint32_t device_id, boolean_t on)
{
	struct msg_set_power_state msg;
	int err;

	memset(&msg, 0, sizeof(msg));
	msg.hdr.buf_size = sizeof(msg);
	msg.hdr.code = BCM2835_MBOX_CODE_REQ;
	msg.tag_hdr.tag = BCM2835_MBOX_TAG_SET_POWER_STATE;
	msg.tag_hdr.val_buf_size = sizeof(msg.body);
	msg.tag_hdr.val_len = sizeof(msg.body.req);
	msg.body.req.device_id = device_id;
	msg.body.req.state = (on ? BCM2835_MBOX_POWER_ON : 0) |
	    BCM2835_MBOX_POWER_WAIT;
	msg.end_tag = 0;

	err = bcm2835_mbox_property(&msg, sizeof(msg));

	return (err);
}

int
bcm2835_mbox_get_clock_rate(uint32_t clock_id, uint32_t *hz)
{
	struct msg_get_clock_rate msg;
	int err;

	memset(&msg, 0, sizeof(msg));
	msg.hdr.buf_size = sizeof(msg);
	msg.hdr.code = BCM2835_MBOX_CODE_REQ;
	msg.tag_hdr.tag = BCM2835_MBOX_TAG_GET_CLOCK_RATE;
	msg.tag_hdr.val_buf_size = sizeof(msg.body);
	msg.tag_hdr.val_len = sizeof(msg.body.req);
	msg.body.req.clock_id = clock_id;
	msg.end_tag = 0;

	err = bcm2835_mbox_property(&msg, sizeof(msg));
	*hz = msg.body.resp.rate_hz;

	return (err);
}

int
bcm2835_mbox_fb_get_w_h(struct bcm2835_fb_config *fb)
{
	int err;
	struct msg_fb_get_w_h msg;

	memset(&msg, 0, sizeof(msg));
	msg.hdr.buf_size = sizeof(msg);
	msg.hdr.code = BCM2835_MBOX_CODE_REQ;
	BCM2835_MBOX_INIT_TAG(&msg.physical_w_h, GET_PHYSICAL_W_H);
	msg.physical_w_h.tag_hdr.val_len = 0;
	msg.end_tag = 0;

	err = bcm2835_mbox_property(&msg, sizeof(msg));
	if (err == 0) {
		fb->xres = msg.physical_w_h.body.resp.width;
		fb->yres = msg.physical_w_h.body.resp.height;
	}

	return (err);
}

int
bcm2835_mbox_fb_init(struct bcm2835_fb_config *fb)
{
	int err;
	struct msg_fb_setup msg;

	memset(&msg, 0, sizeof(msg));
	msg.hdr.buf_size = sizeof(msg);
	msg.hdr.code = BCM2835_MBOX_CODE_REQ;
	BCM2835_MBOX_INIT_TAG(&msg.physical_w_h, SET_PHYSICAL_W_H);
	msg.physical_w_h.body.req.width = fb->xres;
	msg.physical_w_h.body.req.height = fb->yres;
	BCM2835_MBOX_INIT_TAG(&msg.virtual_w_h, SET_VIRTUAL_W_H);
	msg.virtual_w_h.body.req.width = fb->vxres;
	msg.virtual_w_h.body.req.height = fb->vyres;
	BCM2835_MBOX_INIT_TAG(&msg.offset, SET_VIRTUAL_OFFSET);
	msg.offset.body.req.x = fb->xoffset;
	msg.offset.body.req.y = fb->yoffset;
	BCM2835_MBOX_INIT_TAG(&msg.depth, SET_DEPTH);
	msg.depth.body.req.bpp = fb->bpp;
	BCM2835_MBOX_INIT_TAG(&msg.alpha, SET_ALPHA_MODE);
	msg.alpha.body.req.alpha = BCM2835_MBOX_ALPHA_MODE_IGNORED;
	BCM2835_MBOX_INIT_TAG(&msg.buffer, ALLOCATE_BUFFER);
	msg.buffer.body.req.alignment = PAGE_SIZE;
	BCM2835_MBOX_INIT_TAG(&msg.pitch, GET_PITCH);
	msg.end_tag = 0;

	err = bcm2835_mbox_property(&msg, sizeof(msg));
	if (err == 0) {
		fb->xres = msg.physical_w_h.body.resp.width;
		fb->yres = msg.physical_w_h.body.resp.height;
		fb->vxres = msg.virtual_w_h.body.resp.width;
		fb->vyres = msg.virtual_w_h.body.resp.height;
		fb->xoffset = msg.offset.body.resp.x;
		fb->yoffset = msg.offset.body.resp.y;
		fb->pitch = msg.pitch.body.resp.pitch;
		fb->base = VCBUS_TO_PHYS(msg.buffer.body.resp.fb_address);
		fb->size = msg.buffer.body.resp.fb_size;
	}

	return (err);
}
