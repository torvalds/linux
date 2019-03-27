/*-
 * Copyright (c) 2017-2018 Ruslan Bukin <br@bsdpad.com>
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

/* ARM PrimeCell DMA Controller (PL330) driver. */

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
#include <sys/resource.h>
#include <sys/rman.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/pmap.h>

#include <machine/bus.h>

#ifdef FDT
#include <dev/fdt/fdt_common.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#endif

#include <dev/xdma/xdma.h>
#include <dev/xdma/controller/pl330.h>

#include "xdma_if.h"

#define PL330_DEBUG
#undef PL330_DEBUG

#ifdef PL330_DEBUG
#define dprintf(fmt, ...)  printf(fmt, ##__VA_ARGS__)
#else
#define dprintf(fmt, ...)
#endif

#define	READ4(_sc, _reg)	\
	bus_read_4(_sc->res[0], _reg)
#define	WRITE4(_sc, _reg, _val)	\
	bus_write_4(_sc->res[0], _reg, _val)

#define	PL330_NCHANNELS	32
#define	PL330_MAXLOAD	2048

struct pl330_channel {
	struct pl330_softc	*sc;
	xdma_channel_t		*xchan;
	int			used;
	int			index;
	uint8_t			*ibuf;
	bus_addr_t		ibuf_phys;
	uint32_t		enqueued;
	uint32_t		capacity;
};

struct pl330_fdt_data {
	uint32_t periph_id;
};

struct pl330_softc {
	device_t		dev;
	struct resource		*res[PL330_NCHANNELS + 1];
	void			*ih[PL330_NCHANNELS];
	struct pl330_channel	channels[PL330_NCHANNELS];
};

static struct resource_spec pl330_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,	RF_ACTIVE },
	{ SYS_RES_IRQ,		1,	RF_ACTIVE | RF_OPTIONAL },
	{ SYS_RES_IRQ,		2,	RF_ACTIVE | RF_OPTIONAL },
	{ SYS_RES_IRQ,		3,	RF_ACTIVE | RF_OPTIONAL },
	{ SYS_RES_IRQ,		4,	RF_ACTIVE | RF_OPTIONAL },
	{ SYS_RES_IRQ,		5,	RF_ACTIVE | RF_OPTIONAL },
	{ SYS_RES_IRQ,		6,	RF_ACTIVE | RF_OPTIONAL },
	{ SYS_RES_IRQ,		7,	RF_ACTIVE | RF_OPTIONAL },
	{ SYS_RES_IRQ,		8,	RF_ACTIVE | RF_OPTIONAL },
	{ SYS_RES_IRQ,		9,	RF_ACTIVE | RF_OPTIONAL },
	{ SYS_RES_IRQ,		10,	RF_ACTIVE | RF_OPTIONAL },
	{ SYS_RES_IRQ,		11,	RF_ACTIVE | RF_OPTIONAL },
	{ SYS_RES_IRQ,		12,	RF_ACTIVE | RF_OPTIONAL },
	{ SYS_RES_IRQ,		13,	RF_ACTIVE | RF_OPTIONAL },
	{ SYS_RES_IRQ,		14,	RF_ACTIVE | RF_OPTIONAL },
	{ SYS_RES_IRQ,		15,	RF_ACTIVE | RF_OPTIONAL },
	{ SYS_RES_IRQ,		16,	RF_ACTIVE | RF_OPTIONAL },
	{ SYS_RES_IRQ,		17,	RF_ACTIVE | RF_OPTIONAL },
	{ SYS_RES_IRQ,		18,	RF_ACTIVE | RF_OPTIONAL },
	{ SYS_RES_IRQ,		19,	RF_ACTIVE | RF_OPTIONAL },
	{ SYS_RES_IRQ,		20,	RF_ACTIVE | RF_OPTIONAL },
	{ SYS_RES_IRQ,		21,	RF_ACTIVE | RF_OPTIONAL },
	{ SYS_RES_IRQ,		22,	RF_ACTIVE | RF_OPTIONAL },
	{ SYS_RES_IRQ,		23,	RF_ACTIVE | RF_OPTIONAL },
	{ SYS_RES_IRQ,		24,	RF_ACTIVE | RF_OPTIONAL },
	{ SYS_RES_IRQ,		25,	RF_ACTIVE | RF_OPTIONAL },
	{ SYS_RES_IRQ,		26,	RF_ACTIVE | RF_OPTIONAL },
	{ SYS_RES_IRQ,		27,	RF_ACTIVE | RF_OPTIONAL },
	{ SYS_RES_IRQ,		28,	RF_ACTIVE | RF_OPTIONAL },
	{ SYS_RES_IRQ,		29,	RF_ACTIVE | RF_OPTIONAL },
	{ SYS_RES_IRQ,		30,	RF_ACTIVE | RF_OPTIONAL },
	{ SYS_RES_IRQ,		31,	RF_ACTIVE | RF_OPTIONAL },
	{ -1, 0 }
};

#define	HWTYPE_NONE	0
#define	HWTYPE_STD	1

static struct ofw_compat_data compat_data[] = {
	{ "arm,pl330",		HWTYPE_STD },
	{ NULL,			HWTYPE_NONE },
};

static void
pl330_intr(void *arg)
{
	xdma_transfer_status_t status;
	struct xdma_transfer_status st;
	struct pl330_channel *chan;
	struct xdma_channel *xchan;
	struct pl330_softc *sc;
	uint32_t pending;
	int i;
	int c;

	sc = arg;

	pending = READ4(sc, INTMIS);

	dprintf("%s: 0x%x, LC0 %x, SAR %x DAR %x\n",
	    __func__, pending, READ4(sc, LC0(0)),
	    READ4(sc, SAR(0)), READ4(sc, DAR(0)));

	WRITE4(sc, INTCLR, pending);

	for (c = 0; c < PL330_NCHANNELS; c++) {
		if ((pending & (1 << c)) == 0) {
			continue;
		}
		chan = &sc->channels[c];
		xchan = chan->xchan;
		st.error = 0;
		st.transferred = 0;
		for (i = 0; i < chan->enqueued; i++) {
			xchan_seg_done(xchan, &st);
		}

		/* Accept new requests. */
		chan->capacity = PL330_MAXLOAD;

		/* Finish operation */
		status.error = 0;
		status.transferred = 0;
		xdma_callback(chan->xchan, &status);
	}
}

static uint32_t
emit_mov(uint8_t *buf, uint32_t reg, uint32_t val)
{

	buf[0] = DMAMOV;
	buf[1] = reg;
	buf[2] = val;
	buf[3] = val >> 8;
	buf[4] = val >> 16;
	buf[5] = val >> 24;

	return (6);
}

static uint32_t
emit_lp(uint8_t *buf, uint8_t idx, uint32_t iter)
{

	if (idx > 1)
		return (0); /* We have two loops only. */

	buf[0] = DMALP;
	buf[0] |= (idx << 1);
	buf[1] = (iter - 1) & 0xff;

	return (2);
}

static uint32_t
emit_lpend(uint8_t *buf, uint8_t idx,
    uint8_t burst, uint8_t jump_addr_relative)
{

	buf[0] = DMALPEND;
	buf[0] |= DMALPEND_NF;
	buf[0] |= (idx << 2);
	if (burst)
		buf[0] |= (1 << 1) | (1 << 0);
	else
		buf[0] |= (0 << 1) | (1 << 0);
	buf[1] = jump_addr_relative;

	return (2);
}

static uint32_t
emit_ld(uint8_t *buf, uint8_t burst)
{

	buf[0] = DMALD;
	if (burst)
		buf[0] |= (1 << 1) | (1 << 0);
	else
		buf[0] |= (0 << 1) | (1 << 0);

	return (1);
}

static uint32_t
emit_st(uint8_t *buf, uint8_t burst)
{

	buf[0] = DMAST;
	if (burst)
		buf[0] |= (1 << 1) | (1 << 0);
	else
		buf[0] |= (0 << 1) | (1 << 0);

	return (1);
}

static uint32_t
emit_end(uint8_t *buf)
{

	buf[0] = DMAEND;

	return (1);
}

static uint32_t
emit_sev(uint8_t *buf, uint32_t ev)
{

	buf[0] = DMASEV;
	buf[1] = (ev << 3);

	return (2);
}

static uint32_t
emit_wfp(uint8_t *buf, uint32_t p_id)
{

	buf[0] = DMAWFP;
	buf[0] |= (1 << 0);
	buf[1] = (p_id << 3);

	return (2);
}

static uint32_t
emit_go(uint8_t *buf, uint32_t chan_id,
    uint32_t addr, uint8_t non_secure)
{

	buf[0] = DMAGO;
	buf[0] |= (non_secure << 1);

	buf[1] = chan_id;
	buf[2] = addr;
	buf[3] = addr >> 8;
	buf[4] = addr >> 16;
	buf[5] = addr >> 24;

	return (6);
}

static int
pl330_probe(device_t dev)
{
	int hwtype;

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	hwtype = ofw_bus_search_compatible(dev, compat_data)->ocd_data;
	if (hwtype == HWTYPE_NONE)
		return (ENXIO);

	device_set_desc(dev, "ARM PrimeCell DMA Controller (PL330)");

	return (BUS_PROBE_DEFAULT);
}

static int
pl330_attach(device_t dev)
{
	struct pl330_softc *sc;
	phandle_t xref, node;
	int err;
	int i;

	sc = device_get_softc(dev);
	sc->dev = dev;

	if (bus_alloc_resources(dev, pl330_spec, sc->res)) {
		device_printf(dev, "could not allocate resources for device\n");
		return (ENXIO);
	}

	/* Setup interrupt handler */
	for (i = 0; i < PL330_NCHANNELS; i++) {
		if (sc->res[i + 1] == NULL)
			break;
		err = bus_setup_intr(dev, sc->res[i + 1], INTR_TYPE_MISC | INTR_MPSAFE,
		    NULL, pl330_intr, sc, sc->ih[i]);
		if (err) {
			device_printf(dev, "Unable to alloc interrupt resource.\n");
			return (ENXIO);
		}
	}

	node = ofw_bus_get_node(dev);
	xref = OF_xref_from_node(node);
	OF_device_register_xref(xref, dev);

	return (0);
}

static int
pl330_detach(device_t dev)
{
	struct pl330_softc *sc;

	sc = device_get_softc(dev);

	return (0);
}

static int
pl330_channel_alloc(device_t dev, struct xdma_channel *xchan)
{
	struct pl330_channel *chan;
	struct pl330_softc *sc;
	int i;

	sc = device_get_softc(dev);

	for (i = 0; i < PL330_NCHANNELS; i++) {
		chan = &sc->channels[i];
		if (chan->used == 0) {
			chan->xchan = xchan;
			xchan->chan = (void *)chan;
			xchan->caps |= XCHAN_CAP_BUSDMA;
			chan->index = i;
			chan->sc = sc;
			chan->used = 1;

			chan->ibuf = (void *)kmem_alloc_contig(PAGE_SIZE * 8,
			    M_ZERO, 0, ~0, PAGE_SIZE, 0,
			    VM_MEMATTR_UNCACHEABLE);
			chan->ibuf_phys = vtophys(chan->ibuf);

			return (0);
		}
	}

	return (-1);
}

static int
pl330_channel_free(device_t dev, struct xdma_channel *xchan)
{
	struct pl330_channel *chan;
	struct pl330_softc *sc;

	sc = device_get_softc(dev);

	chan = (struct pl330_channel *)xchan->chan;
	chan->used = 0;

	return (0);
}

static int
pl330_channel_capacity(device_t dev, xdma_channel_t *xchan,
    uint32_t *capacity)
{
	struct pl330_channel *chan;

	chan = (struct pl330_channel *)xchan->chan;

	*capacity = chan->capacity;

	return (0);
}

static int
pl330_ccr_port_width(struct xdma_sglist *sg, uint32_t *addr)
{
	uint32_t reg;

	reg = 0;

	switch (sg->src_width) {
	case 1:
		reg |= CCR_SRC_BURST_SIZE_1;
		break;
	case 2:
		reg |= CCR_SRC_BURST_SIZE_2;
		break;
	case 4:
		reg |= CCR_SRC_BURST_SIZE_4;
		break;
	default:
		return (-1);
	}

	switch (sg->dst_width) {
	case 1:
		reg |= CCR_DST_BURST_SIZE_1;
		break;
	case 2:
		reg |= CCR_DST_BURST_SIZE_2;
		break;
	case 4:
		reg |= CCR_DST_BURST_SIZE_4;
		break;
	default:
		return (-1);
	}

	*addr |= reg;

	return (0);
}

static int
pl330_channel_submit_sg(device_t dev, struct xdma_channel *xchan,
    struct xdma_sglist *sg, uint32_t sg_n)
{
	struct pl330_fdt_data *data;
	xdma_controller_t *xdma;
	struct pl330_channel *chan;
	struct pl330_softc *sc;
	uint32_t src_addr_lo;
	uint32_t dst_addr_lo;
	uint32_t len;
	uint32_t reg;
	uint32_t offs;
	uint32_t cnt;
	uint8_t *ibuf;
	uint8_t dbuf[6];
	uint8_t offs0, offs1;
	int err;
	int i;

	sc = device_get_softc(dev);

	xdma = xchan->xdma;
	data = (struct pl330_fdt_data *)xdma->data;

	chan = (struct pl330_channel *)xchan->chan;
	ibuf = chan->ibuf;

	dprintf("%s: chan->index %d\n", __func__, chan->index);

	offs = 0;

	for (i = 0; i < sg_n; i++) {
		if (sg[i].direction == XDMA_DEV_TO_MEM)
			reg = CCR_DST_INC;
		else {
			reg = CCR_SRC_INC;
			reg |= (CCR_DST_PROT_PRIV);
		}

		err = pl330_ccr_port_width(&sg[i], &reg);
		if (err != 0)
			return (err);

		offs += emit_mov(&chan->ibuf[offs], R_CCR, reg);

		src_addr_lo = (uint32_t)sg[i].src_addr;
		dst_addr_lo = (uint32_t)sg[i].dst_addr;
		len = (uint32_t)sg[i].len;

		dprintf("%s: src %x dst %x len %d periph_id %d\n", __func__,
		    src_addr_lo, dst_addr_lo, len, data->periph_id);

		offs += emit_mov(&ibuf[offs], R_SAR, src_addr_lo);
		offs += emit_mov(&ibuf[offs], R_DAR, dst_addr_lo);

		if (sg[i].src_width != sg[i].dst_width)
			return (-1); /* Not supported. */

		cnt = (len / sg[i].src_width);
		if (cnt > 128) {
			offs += emit_lp(&ibuf[offs], 0, cnt / 128);
			offs0 = offs;
			offs += emit_lp(&ibuf[offs], 1, 128);
			offs1 = offs;
		} else {
			offs += emit_lp(&ibuf[offs], 0, cnt);
			offs0 = offs;
		}
		offs += emit_wfp(&ibuf[offs], data->periph_id);
		offs += emit_ld(&ibuf[offs], 1);
		offs += emit_st(&ibuf[offs], 1);

		if (cnt > 128)
			offs += emit_lpend(&ibuf[offs], 1, 1, (offs - offs1));

		offs += emit_lpend(&ibuf[offs], 0, 1, (offs - offs0));
	}

	offs += emit_sev(&ibuf[offs], chan->index);
	offs += emit_end(&ibuf[offs]);

	emit_go(dbuf, chan->index, chan->ibuf_phys, 0);

	reg = (dbuf[1] << 24) | (dbuf[0] << 16);
	WRITE4(sc, DBGINST0, reg);
	reg = (dbuf[5] << 24) | (dbuf[4] << 16) | (dbuf[3] << 8) | dbuf[2];
	WRITE4(sc, DBGINST1, reg);

	WRITE4(sc, INTCLR, 0xffffffff);
	WRITE4(sc, INTEN, (1 << chan->index));

	chan->enqueued = sg_n;
	chan->capacity = 0;

	/* Start operation */
	WRITE4(sc, DBGCMD, 0);

	return (0);
}

static int
pl330_channel_prep_sg(device_t dev, struct xdma_channel *xchan)
{
	struct pl330_channel *chan;
	struct pl330_softc *sc;

	sc = device_get_softc(dev);

	dprintf("%s(%d)\n", __func__, device_get_unit(dev));

	chan = (struct pl330_channel *)xchan->chan;
	chan->capacity = PL330_MAXLOAD;

	return (0);
}

static int
pl330_channel_control(device_t dev, xdma_channel_t *xchan, int cmd)
{
	struct pl330_channel *chan;
	struct pl330_softc *sc;

	sc = device_get_softc(dev);

	chan = (struct pl330_channel *)xchan->chan;

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
pl330_ofw_md_data(device_t dev, pcell_t *cells, int ncells, void **ptr)
{
	struct pl330_fdt_data *data;

	if (ncells != 1)
		return (-1);

	data = malloc(sizeof(struct pl330_fdt_data),
	    M_DEVBUF, (M_WAITOK | M_ZERO));
	data->periph_id = cells[0];

	*ptr = data;

	return (0);
}
#endif

static device_method_t pl330_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,			pl330_probe),
	DEVMETHOD(device_attach,		pl330_attach),
	DEVMETHOD(device_detach,		pl330_detach),

	/* xDMA Interface */
	DEVMETHOD(xdma_channel_alloc,		pl330_channel_alloc),
	DEVMETHOD(xdma_channel_free,		pl330_channel_free),
	DEVMETHOD(xdma_channel_control,		pl330_channel_control),

	/* xDMA SG Interface */
	DEVMETHOD(xdma_channel_capacity,	pl330_channel_capacity),
	DEVMETHOD(xdma_channel_prep_sg,		pl330_channel_prep_sg),
	DEVMETHOD(xdma_channel_submit_sg,	pl330_channel_submit_sg),

#ifdef FDT
	DEVMETHOD(xdma_ofw_md_data,		pl330_ofw_md_data),
#endif

	DEVMETHOD_END
};

static driver_t pl330_driver = {
	"pl330",
	pl330_methods,
	sizeof(struct pl330_softc),
};

static devclass_t pl330_devclass;

EARLY_DRIVER_MODULE(pl330, simplebus, pl330_driver, pl330_devclass, 0, 0,
    BUS_PASS_INTERRUPT + BUS_PASS_ORDER_LATE);
