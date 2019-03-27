/*-
 * Copyright (c) 2016 Jared D. McNeill <jmcneill@invisible.ca>
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
 */

/*
 * Allwinner DMA controller
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/condvar.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/endian.h>

#include <machine/bus.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <arm/allwinner/a10_dmac.h>
#include <dev/extres/clk/clk.h>
#include <dev/extres/hwreset/hwreset.h>

#include "sunxi_dma_if.h"

#define	DMA_IRQ_EN_REG0		0x00
#define	DMA_IRQ_EN_REG1		0x04
#define	DMA_IRQ_EN_REG(ch)	(DMA_IRQ_EN_REG0 + ((ch) / 8) * 4)
#define	 DMA_PKG_IRQ_EN(ch)	(1 << (((ch) % 8) * 4 + 1))
#define	 DMA_PKG_IRQ_MASK	0x2222222222222222ULL
#define	DMA_IRQ_PEND_REG0	0x10
#define	DMA_IRQ_PEND_REG1	0x14
#define	DMA_IRQ_PEND_REG(ch)	(DMA_IRQ_PEND_REG0 + ((ch) / 8) * 4)
#define	DMA_STA_REG		0x30
#define	DMA_EN_REG(n)		(0x100 + (n) * 0x40 + 0x00)
#define	 DMA_EN			(1 << 0)
#define	DMA_PAU_REG(n)		(0x100 + (n) * 0x40 + 0x04)
#define	DMA_STAR_ADDR_REG(n)	(0x100 + (n) * 0x40 + 0x08)
#define	DMA_CFG_REG(n)		(0x100 + (n) * 0x40 + 0x0c)
#define	 DMA_DEST_DATA_WIDTH		(0x3 << 25)
#define	 DMA_DEST_DATA_WIDTH_SHIFT	25
#define	 DMA_DEST_BST_LEN		(0x3 << 22)
#define	 DMA_DEST_BST_LEN_SHIFT		22
#define	 DMA_DEST_ADDR_MODE		(0x1 << 21)
#define	 DMA_DEST_ADDR_MODE_SHIFT	21
#define	 DMA_DEST_DRQ_TYPE		(0x1f << 16)
#define	 DMA_DEST_DRQ_TYPE_SHIFT	16
#define	 DMA_SRC_DATA_WIDTH		(0x3 << 9)
#define	 DMA_SRC_DATA_WIDTH_SHIFT	9
#define	 DMA_SRC_BST_LEN		(0x3 << 6)
#define	 DMA_SRC_BST_LEN_SHIFT		6
#define	 DMA_SRC_ADDR_MODE		(0x1 << 5)
#define	 DMA_SRC_ADDR_MODE_SHIFT	5
#define	 DMA_SRC_DRQ_TYPE		(0x1f << 0)
#define	 DMA_SRC_DRQ_TYPE_SHIFT		0	
#define	 DMA_DATA_WIDTH_8BIT		0
#define	 DMA_DATA_WIDTH_16BIT		1
#define	 DMA_DATA_WIDTH_32BIT		2
#define	 DMA_DATA_WIDTH_64BIT		3
#define	 DMA_ADDR_MODE_LINEAR		0
#define	 DMA_ADDR_MODE_IO		1
#define	 DMA_BST_LEN_1			0
#define	 DMA_BST_LEN_4			1
#define	 DMA_BST_LEN_8			2
#define	 DMA_BST_LEN_16			3
#define	DMA_CUR_SRC_REG(n)	(0x100 + (n) * 0x40 + 0x10)
#define	DMA_CUR_DEST_REG(n)	(0x100 + (n) * 0x40 + 0x14)
#define	DMA_BCNT_LEFT_REG(n)	(0x100 + (n) * 0x40 + 0x18)
#define	DMA_PARA_REG(n)		(0x100 + (n) * 0x40 + 0x1c)
#define	 WAIT_CYC			(0xff << 0)
#define	 WAIT_CYC_SHIFT			0

struct a31dmac_desc {
	uint32_t		config;
	uint32_t		srcaddr;
	uint32_t		dstaddr;
	uint32_t		bcnt;
	uint32_t		para;
	uint32_t		next;
#define	DMA_NULL		0xfffff800
};
#define	DESC_ALIGN		4
#define	DESC_SIZE		sizeof(struct a31dmac_desc)

struct a31dmac_config {
	u_int			nchans;
};

static const struct a31dmac_config a31_config = { .nchans = 16 };
static const struct a31dmac_config h3_config = { .nchans = 12 };
static const struct a31dmac_config a83t_config = { .nchans = 8 };
static const struct a31dmac_config a64_config = { .nchans = 8 };

static struct ofw_compat_data compat_data[] = {
	{ "allwinner,sun6i-a31-dma",	(uintptr_t)&a31_config },
	{ "allwinner,sun8i-a83t-dma",	(uintptr_t)&a83t_config },
	{ "allwinner,sun8i-h3-dma",	(uintptr_t)&h3_config },
	{ "allwinner,sun50i-a64-dma",	(uintptr_t)&a64_config },
	{ NULL,				(uintptr_t)NULL }
};

struct a31dmac_softc;

struct a31dmac_channel {
	struct a31dmac_softc *		sc;
	uint8_t				index;
	void				(*callback)(void *);
	void *				callbackarg;

	bus_dmamap_t			dmamap;
	struct a31dmac_desc		*desc;
	bus_addr_t			physaddr;
};

struct a31dmac_softc {
	struct resource *		res[2];
	struct mtx			mtx;
	void *				ih;

	bus_dma_tag_t			dmat;

	u_int				nchans;
	struct a31dmac_channel *	chans;
};

static struct resource_spec a31dmac_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,	RF_ACTIVE },
	{ -1, 0 }
};

#define	DMA_READ(sc, reg)	bus_read_4((sc)->res[0], (reg))
#define	DMA_WRITE(sc, reg, val)	bus_write_4((sc)->res[0], (reg), (val))

static void a31dmac_intr(void *);
static void a31dmac_dmamap_cb(void *, bus_dma_segment_t *, int, int);

static int
a31dmac_probe(device_t dev)
{
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_search_compatible(dev, compat_data)->ocd_data)
		return (ENXIO);

	device_set_desc(dev, "Allwinner DMA controller");
	return (BUS_PROBE_DEFAULT);
}

static int
a31dmac_attach(device_t dev)
{
	struct a31dmac_softc *sc;
	struct a31dmac_config *conf;
	u_int index;
	hwreset_t rst;
	clk_t clk;
	int error;

	sc = device_get_softc(dev);
	conf = (void *)ofw_bus_search_compatible(dev, compat_data)->ocd_data;
	clk = NULL;
	rst = NULL;

	if (bus_alloc_resources(dev, a31dmac_spec, sc->res)) {
		device_printf(dev, "cannot allocate resources for device\n");
		return (ENXIO);
	}

	mtx_init(&sc->mtx, "a31 dmac", NULL, MTX_SPIN);

	/* Clock and reset setup */
	if (clk_get_by_ofw_index(dev, 0, 0, &clk) != 0) {
		device_printf(dev, "cannot get clock\n");
		goto fail;
	}
	if (clk_enable(clk) != 0) {
		device_printf(dev, "cannot enable clock\n");
		goto fail;
	}
	if (hwreset_get_by_ofw_idx(dev, 0, 0, &rst) != 0) {
		device_printf(dev, "cannot get hwreset\n");
		goto fail;
	}
	if (hwreset_deassert(rst) != 0) {
		device_printf(dev, "cannot de-assert reset\n");
		goto fail;
	}

	/* Descriptor DMA */
	error = bus_dma_tag_create(
		bus_get_dma_tag(dev),		/* Parent tag */
		DESC_ALIGN, 0,			/* alignment, boundary */
		BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
		BUS_SPACE_MAXADDR,		/* highaddr */
		NULL, NULL,			/* filter, filterarg */
		DESC_SIZE, 1,			/* maxsize, nsegs */
		DESC_SIZE,			/* maxsegsize */
		0,				/* flags */
		NULL, NULL,			/* lockfunc, lockarg */
		&sc->dmat);
	if (error != 0) {
		device_printf(dev, "cannot create dma tag\n");
		goto fail;
	}

	/* Disable all interrupts and clear pending status */
	DMA_WRITE(sc, DMA_IRQ_EN_REG0, 0);
	DMA_WRITE(sc, DMA_IRQ_EN_REG1, 0);
	DMA_WRITE(sc, DMA_IRQ_PEND_REG0, ~0);
	DMA_WRITE(sc, DMA_IRQ_PEND_REG1, ~0);

	/* Initialize channels */
	sc->nchans = conf->nchans;
	sc->chans = malloc(sizeof(*sc->chans) * sc->nchans, M_DEVBUF,
	    M_WAITOK | M_ZERO);

	for (index = 0; index < sc->nchans; index++) {
		sc->chans[index].sc = sc;
		sc->chans[index].index = index;
		sc->chans[index].callback = NULL;
		sc->chans[index].callbackarg = NULL;

		error = bus_dmamem_alloc(sc->dmat,
		    (void **)&sc->chans[index].desc,
		    BUS_DMA_WAITOK | BUS_DMA_COHERENT,
		    &sc->chans[index].dmamap);
		if (error != 0) {
			device_printf(dev, "cannot allocate dma mem\n");
			goto fail;
		}
		error = bus_dmamap_load(sc->dmat, sc->chans[index].dmamap, 
		    sc->chans[index].desc, sizeof(*sc->chans[index].desc),
		    a31dmac_dmamap_cb, &sc->chans[index], BUS_DMA_WAITOK);
		if (error != 0) {
			device_printf(dev, "cannot load dma map\n");
			goto fail;
		}

		DMA_WRITE(sc, DMA_EN_REG(index), 0);
	}

	error = bus_setup_intr(dev, sc->res[1], INTR_MPSAFE | INTR_TYPE_MISC,
	    NULL, a31dmac_intr, sc, &sc->ih);
	if (error != 0) {
		device_printf(dev, "could not setup interrupt handler\n");
		bus_release_resources(dev, a31dmac_spec, sc->res);
		mtx_destroy(&sc->mtx);
		return (ENXIO);
	}

	OF_device_register_xref(OF_xref_from_node(ofw_bus_get_node(dev)), dev);
	return (0);

fail:
	for (index = 0; index < sc->nchans; index++)
		if (sc->chans[index].desc != NULL) {
			bus_dmamap_unload(sc->dmat, sc->chans[index].dmamap);
			bus_dmamem_free(sc->dmat, sc->chans[index].desc,
			    sc->chans[index].dmamap);
		}
	if (sc->chans != NULL)
		free(sc->chans, M_DEVBUF);
	if (sc->ih != NULL)
		bus_teardown_intr(dev, sc->res[1], sc->ih);
	if (rst != NULL)
		hwreset_release(rst);
	if (clk != NULL)
		clk_release(clk);
	bus_release_resources(dev, a31dmac_spec, sc->res);

	return (ENXIO);
}

static void
a31dmac_dmamap_cb(void *priv, bus_dma_segment_t *segs, int nsegs, int error)
{
	struct a31dmac_channel *ch;

	if (error != 0)
		return;

	ch = priv;
	ch->physaddr = segs[0].ds_addr;
}

static void
a31dmac_intr(void *priv)
{
	struct a31dmac_softc *sc;
	uint32_t pend0, pend1, bit;
	uint64_t pend, mask;
	u_int index;

	sc = priv;
	pend0 = DMA_READ(sc, DMA_IRQ_PEND_REG0);
	pend1 = sc->nchans > 8 ? DMA_READ(sc, DMA_IRQ_PEND_REG1) : 0;
	if (pend0 == 0 && pend1 == 0)
		return;

	if (pend0 != 0)
		DMA_WRITE(sc, DMA_IRQ_PEND_REG0, pend0);
	if (pend1 != 0)
		DMA_WRITE(sc, DMA_IRQ_PEND_REG1, pend1);

	pend = pend0 | ((uint64_t)pend1 << 32);

	while ((bit = ffsll(pend & DMA_PKG_IRQ_MASK)) != 0) {
		mask = (1U << (bit - 1));
		pend &= ~mask;
		index = (bit - 1) / 4;

		if (index >= sc->nchans)
			continue;
		if (sc->chans[index].callback == NULL)
			continue;
		sc->chans[index].callback(sc->chans[index].callbackarg);
	}
}

static int
a31dmac_set_config(device_t dev, void *priv, const struct sunxi_dma_config *cfg)
{
	struct a31dmac_channel *ch;
	uint32_t config, para;
	unsigned int dst_dw, dst_bl, dst_wc, dst_am;
	unsigned int src_dw, src_bl, src_wc, src_am;

	ch = priv;

	switch (cfg->dst_width) {
	case 8:
		dst_dw = DMA_DATA_WIDTH_8BIT;
		break;
	case 16:
		dst_dw = DMA_DATA_WIDTH_16BIT;
		break;
	case 32:
		dst_dw = DMA_DATA_WIDTH_32BIT;
		break;
	case 64:
		dst_dw = DMA_DATA_WIDTH_64BIT;
		break;
	default:
		return (EINVAL);
	}
	switch (cfg->dst_burst_len) {
	case 1:
		dst_bl = DMA_BST_LEN_1;
		break;
	case 4:
		dst_bl = DMA_BST_LEN_4;
		break;
	case 8:
		dst_bl = DMA_BST_LEN_8;
		break;
	case 16:
		dst_bl = DMA_BST_LEN_16;
		break;
	default:
		return (EINVAL);
	}
	switch (cfg->src_width) {
	case 8:
		src_dw = DMA_DATA_WIDTH_8BIT;
		break;
	case 16:
		src_dw = DMA_DATA_WIDTH_16BIT;
		break;
	case 32:
		src_dw = DMA_DATA_WIDTH_32BIT;
		break;
	case 64:
		src_dw = DMA_DATA_WIDTH_64BIT;
	default:
		return (EINVAL);
	}
	switch (cfg->src_burst_len) {
	case 1:
		src_bl = DMA_BST_LEN_1;
		break;
	case 4:
		src_bl = DMA_BST_LEN_4;
		break;
	case 8:
		src_bl = DMA_BST_LEN_8;
		break;
	case 16:
		src_bl = DMA_BST_LEN_16;
		break;
	default:
		return (EINVAL);
	}
	dst_am = cfg->dst_noincr ? DMA_ADDR_MODE_IO : DMA_ADDR_MODE_LINEAR;
	src_am = cfg->src_noincr ? DMA_ADDR_MODE_IO : DMA_ADDR_MODE_LINEAR;
	dst_wc = cfg->dst_wait_cyc;
	src_wc = cfg->src_wait_cyc;
	if (dst_wc != src_wc)
		return (EINVAL);

	config = (dst_dw << DMA_DEST_DATA_WIDTH_SHIFT) |
		 (dst_bl << DMA_DEST_BST_LEN_SHIFT) |
		 (dst_am << DMA_DEST_ADDR_MODE_SHIFT) |
		 (cfg->dst_drqtype << DMA_DEST_DRQ_TYPE_SHIFT) |
		 (src_dw << DMA_SRC_DATA_WIDTH_SHIFT) |
		 (src_bl << DMA_SRC_BST_LEN_SHIFT) |
		 (src_am << DMA_SRC_ADDR_MODE_SHIFT) |
		 (cfg->src_drqtype << DMA_SRC_DRQ_TYPE_SHIFT);
	para = (dst_wc << WAIT_CYC_SHIFT);

	ch->desc->config = htole32(config);
	ch->desc->para = htole32(para);

	return (0);
}

static void *
a31dmac_alloc(device_t dev, bool dedicated, void (*cb)(void *), void *cbarg)
{
	struct a31dmac_softc *sc;
	struct a31dmac_channel *ch;
	uint32_t irqen;
	u_int index;

	sc = device_get_softc(dev);
	ch = NULL;

	mtx_lock_spin(&sc->mtx);
	for (index = 0; index < sc->nchans; index++) {
		if (sc->chans[index].callback == NULL) {
			ch = &sc->chans[index];
			ch->callback = cb;
			ch->callbackarg = cbarg;

			irqen = DMA_READ(sc, DMA_IRQ_EN_REG(index));
			irqen |= DMA_PKG_IRQ_EN(index);
			DMA_WRITE(sc, DMA_IRQ_EN_REG(index), irqen);
			break;
		}
	}
	mtx_unlock_spin(&sc->mtx);

	return (ch);
}

static void
a31dmac_free(device_t dev, void *priv)
{
	struct a31dmac_channel *ch;
	struct a31dmac_softc *sc;
	uint32_t irqen;
	u_int index;

	ch = priv;
	sc = ch->sc;
	index = ch->index;

	mtx_lock_spin(&sc->mtx);

	irqen = DMA_READ(sc, DMA_IRQ_EN_REG(index));
	irqen &= ~DMA_PKG_IRQ_EN(index);
	DMA_WRITE(sc, DMA_IRQ_EN_REG(index), irqen);
	DMA_WRITE(sc, DMA_IRQ_PEND_REG(index), DMA_PKG_IRQ_EN(index));

	ch->callback = NULL;
	ch->callbackarg = NULL;

	mtx_unlock_spin(&sc->mtx);
}

static int
a31dmac_transfer(device_t dev, void *priv, bus_addr_t src, bus_addr_t dst,
    size_t nbytes)
{
	struct a31dmac_channel *ch;
	struct a31dmac_softc *sc;

	ch = priv;
	sc = ch->sc;

	ch->desc->srcaddr = htole32((uint32_t)src);
	ch->desc->dstaddr = htole32((uint32_t)dst);
	ch->desc->bcnt = htole32(nbytes);
	ch->desc->next = htole32(DMA_NULL);

	DMA_WRITE(sc, DMA_STAR_ADDR_REG(ch->index), (uint32_t)ch->physaddr);
	DMA_WRITE(sc, DMA_EN_REG(ch->index), DMA_EN);

	return (0);
}

static void
a31dmac_halt(device_t dev, void *priv)
{
	struct a31dmac_channel *ch;
	struct a31dmac_softc *sc;

	ch = priv;
	sc = ch->sc;

	DMA_WRITE(sc, DMA_EN_REG(ch->index), 0);
}

static device_method_t a31dmac_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		a31dmac_probe),
	DEVMETHOD(device_attach,	a31dmac_attach),

	/* sunxi DMA interface */
	DEVMETHOD(sunxi_dma_alloc,	a31dmac_alloc),
	DEVMETHOD(sunxi_dma_free,	a31dmac_free),
	DEVMETHOD(sunxi_dma_set_config,	a31dmac_set_config),
	DEVMETHOD(sunxi_dma_transfer,	a31dmac_transfer),
	DEVMETHOD(sunxi_dma_halt,	a31dmac_halt),

	DEVMETHOD_END
};

static driver_t a31dmac_driver = {
	"a31dmac",
	a31dmac_methods,
	sizeof(struct a31dmac_softc)
};

static devclass_t a31dmac_devclass;

DRIVER_MODULE(a31dmac, simplebus, a31dmac_driver, a31dmac_devclass, 0, 0);
