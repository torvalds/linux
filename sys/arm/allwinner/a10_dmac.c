/*-
 * Copyright (c) 2014-2016 Jared D. McNeill <jmcneill@invisible.ca>
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
 * Allwinner A10/A20 DMA controller
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

#include <machine/bus.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <arm/allwinner/a10_dmac.h>
#include <dev/extres/clk/clk.h>

#include "sunxi_dma_if.h"

#define	NDMA_CHANNELS	8
#define	DDMA_CHANNELS	8

enum a10dmac_type {
	CH_NDMA,
	CH_DDMA
};

struct a10dmac_softc;

struct a10dmac_channel {
	struct a10dmac_softc *	ch_sc;
	uint8_t			ch_index;
	enum a10dmac_type	ch_type;
	void			(*ch_callback)(void *);
	void *			ch_callbackarg;
	uint32_t		ch_regoff;
};

struct a10dmac_softc {
	struct resource *	sc_res[2];
	struct mtx		sc_mtx;
	void *			sc_ih;

	struct a10dmac_channel	sc_ndma_channels[NDMA_CHANNELS];
	struct a10dmac_channel	sc_ddma_channels[DDMA_CHANNELS];
};

static struct resource_spec a10dmac_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,	RF_ACTIVE },
	{ -1, 0 }
};

#define	DMA_READ(sc, reg)	bus_read_4((sc)->sc_res[0], (reg))
#define	DMA_WRITE(sc, reg, val)	bus_write_4((sc)->sc_res[0], (reg), (val))
#define	DMACH_READ(ch, reg)		\
    DMA_READ((ch)->ch_sc, (reg) + (ch)->ch_regoff)
#define	DMACH_WRITE(ch, reg, val)	\
    DMA_WRITE((ch)->ch_sc, (reg) + (ch)->ch_regoff, (val))

static void a10dmac_intr(void *);

static int
a10dmac_probe(device_t dev)
{
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "allwinner,sun4i-a10-dma"))
		return (ENXIO);

	device_set_desc(dev, "Allwinner DMA controller");
	return (BUS_PROBE_DEFAULT);
}

static int
a10dmac_attach(device_t dev)
{
	struct a10dmac_softc *sc;
	unsigned int index;
	clk_t clk;
	int error;

	sc = device_get_softc(dev);

	if (bus_alloc_resources(dev, a10dmac_spec, sc->sc_res)) {
		device_printf(dev, "cannot allocate resources for device\n");
		return (ENXIO);
	}

	mtx_init(&sc->sc_mtx, "a10 dmac", NULL, MTX_SPIN);

	/* Activate DMA controller clock */
	error = clk_get_by_ofw_index(dev, 0, 0, &clk);
	if (error != 0) {
		device_printf(dev, "cannot get clock\n");
		return (error);
	}
	error = clk_enable(clk);
	if (error != 0) {
		device_printf(dev, "cannot enable clock\n");
		return (error);
	}

	/* Disable all interrupts and clear pending status */
	DMA_WRITE(sc, AWIN_DMA_IRQ_EN_REG, 0);
	DMA_WRITE(sc, AWIN_DMA_IRQ_PEND_STA_REG, ~0);

	/* Initialize channels */
	for (index = 0; index < NDMA_CHANNELS; index++) {
		sc->sc_ndma_channels[index].ch_sc = sc;
		sc->sc_ndma_channels[index].ch_index = index;
		sc->sc_ndma_channels[index].ch_type = CH_NDMA;
		sc->sc_ndma_channels[index].ch_callback = NULL;
		sc->sc_ndma_channels[index].ch_callbackarg = NULL;
		sc->sc_ndma_channels[index].ch_regoff = AWIN_NDMA_REG(index);
		DMACH_WRITE(&sc->sc_ndma_channels[index], AWIN_NDMA_CTL_REG, 0);
	}
	for (index = 0; index < DDMA_CHANNELS; index++) {
		sc->sc_ddma_channels[index].ch_sc = sc;
		sc->sc_ddma_channels[index].ch_index = index;
		sc->sc_ddma_channels[index].ch_type = CH_DDMA;
		sc->sc_ddma_channels[index].ch_callback = NULL;
		sc->sc_ddma_channels[index].ch_callbackarg = NULL;
		sc->sc_ddma_channels[index].ch_regoff = AWIN_DDMA_REG(index);
		DMACH_WRITE(&sc->sc_ddma_channels[index], AWIN_DDMA_CTL_REG, 0);
	}

	error = bus_setup_intr(dev, sc->sc_res[1], INTR_MPSAFE | INTR_TYPE_MISC,
	    NULL, a10dmac_intr, sc, &sc->sc_ih);
	if (error != 0) {
		device_printf(dev, "could not setup interrupt handler\n");
		bus_release_resources(dev, a10dmac_spec, sc->sc_res);
		mtx_destroy(&sc->sc_mtx);
		return (ENXIO);
	}

	OF_device_register_xref(OF_xref_from_node(ofw_bus_get_node(dev)), dev);
	return (0);
}

static void
a10dmac_intr(void *priv)
{
	struct a10dmac_softc *sc = priv;
	uint32_t sta, bit, mask;
	uint8_t index;

	sta = DMA_READ(sc, AWIN_DMA_IRQ_PEND_STA_REG);
	DMA_WRITE(sc, AWIN_DMA_IRQ_PEND_STA_REG, sta);

	while ((bit = ffs(sta & AWIN_DMA_IRQ_END_MASK)) != 0) {
		mask = (1U << (bit - 1));
		sta &= ~mask;
		/*
		 * Map status bit to channel number. The status register is
		 * encoded with two bits of status per channel (lowest bit
		 * is half transfer pending, highest bit is end transfer
		 * pending). The 8 normal DMA channel status are in the lower
		 * 16 bits and the 8 dedicated DMA channel status are in
		 * the upper 16 bits. The output is a channel number from 0-7.
		 */
		index = ((bit - 1) / 2) & 7;
		if (mask & AWIN_DMA_IRQ_NDMA) {
			if (sc->sc_ndma_channels[index].ch_callback == NULL)
				continue;
			sc->sc_ndma_channels[index].ch_callback(
			    sc->sc_ndma_channels[index].ch_callbackarg);
		} else {
			if (sc->sc_ddma_channels[index].ch_callback == NULL)
				continue;
			sc->sc_ddma_channels[index].ch_callback(
			    sc->sc_ddma_channels[index].ch_callbackarg);
		}
	}
}

static uint32_t
a10dmac_read_ctl(struct a10dmac_channel *ch)
{
	if (ch->ch_type == CH_NDMA) {
		return (DMACH_READ(ch, AWIN_NDMA_CTL_REG));
	} else {
		return (DMACH_READ(ch, AWIN_DDMA_CTL_REG));
	}
}

static void
a10dmac_write_ctl(struct a10dmac_channel *ch, uint32_t val)
{
	if (ch->ch_type == CH_NDMA) {
		DMACH_WRITE(ch, AWIN_NDMA_CTL_REG, val);
	} else {
		DMACH_WRITE(ch, AWIN_DDMA_CTL_REG, val);
	}
}

static int
a10dmac_set_config(device_t dev, void *priv, const struct sunxi_dma_config *cfg)
{
	struct a10dmac_channel *ch = priv;
	uint32_t val;
	unsigned int dst_dw, dst_bl, dst_bs, dst_wc, dst_am;
	unsigned int src_dw, src_bl, src_bs, src_wc, src_am;

	switch (cfg->dst_width) {
	case 8:
		dst_dw = AWIN_DMA_CTL_DATA_WIDTH_8;
		break;
	case 16:
		dst_dw = AWIN_DMA_CTL_DATA_WIDTH_16;
		break;
	case 32:
		dst_dw = AWIN_DMA_CTL_DATA_WIDTH_32;
		break;
	default:
		return (EINVAL);
	}
	switch (cfg->dst_burst_len) {
	case 1:
		dst_bl = AWIN_DMA_CTL_BURST_LEN_1;
		break;
	case 4:
		dst_bl = AWIN_DMA_CTL_BURST_LEN_4;
		break;
	case 8:
		dst_bl = AWIN_DMA_CTL_BURST_LEN_8;
		break;
	default:
		return (EINVAL);
	}
	switch (cfg->src_width) {
	case 8:
		src_dw = AWIN_DMA_CTL_DATA_WIDTH_8;
		break;
	case 16:
		src_dw = AWIN_DMA_CTL_DATA_WIDTH_16;
		break;
	case 32:
		src_dw = AWIN_DMA_CTL_DATA_WIDTH_32;
		break;
	default:
		return (EINVAL);
	}
	switch (cfg->src_burst_len) {
	case 1:
		src_bl = AWIN_DMA_CTL_BURST_LEN_1;
		break;
	case 4:
		src_bl = AWIN_DMA_CTL_BURST_LEN_4;
		break;
	case 8:
		src_bl = AWIN_DMA_CTL_BURST_LEN_8;
		break;
	default:
		return (EINVAL);
	}

	val = (dst_dw << AWIN_DMA_CTL_DST_DATA_WIDTH_SHIFT) |
	      (dst_bl << AWIN_DMA_CTL_DST_BURST_LEN_SHIFT) |
	      (cfg->dst_drqtype << AWIN_DMA_CTL_DST_DRQ_TYPE_SHIFT) |
	      (src_dw << AWIN_DMA_CTL_SRC_DATA_WIDTH_SHIFT) |
	      (src_bl << AWIN_DMA_CTL_SRC_BURST_LEN_SHIFT) |
	      (cfg->src_drqtype << AWIN_DMA_CTL_SRC_DRQ_TYPE_SHIFT);

	if (ch->ch_type == CH_NDMA) {
		if (cfg->dst_noincr)
			val |= AWIN_NDMA_CTL_DST_ADDR_NOINCR;
		if (cfg->src_noincr)
			val |= AWIN_NDMA_CTL_SRC_ADDR_NOINCR;

		DMACH_WRITE(ch, AWIN_NDMA_CTL_REG, val);
	} else {
		dst_am = cfg->dst_noincr ? AWIN_DDMA_CTL_DMA_ADDR_IO :
		    AWIN_DDMA_CTL_DMA_ADDR_LINEAR;
		src_am = cfg->src_noincr ? AWIN_DDMA_CTL_DMA_ADDR_IO :
		    AWIN_DDMA_CTL_DMA_ADDR_LINEAR;

		val |= (dst_am << AWIN_DDMA_CTL_DST_ADDR_MODE_SHIFT);
		val |= (src_am << AWIN_DDMA_CTL_SRC_ADDR_MODE_SHIFT);

		DMACH_WRITE(ch, AWIN_DDMA_CTL_REG, val);

		dst_bs = cfg->dst_blksize - 1;
		dst_wc = cfg->dst_wait_cyc - 1;
		src_bs = cfg->src_blksize - 1;
		src_wc = cfg->src_wait_cyc - 1;

		DMACH_WRITE(ch, AWIN_DDMA_PARA_REG,
		    (dst_bs << AWIN_DDMA_PARA_DST_DATA_BLK_SIZ_SHIFT) |
		    (dst_wc << AWIN_DDMA_PARA_DST_WAIT_CYC_SHIFT) |
		    (src_bs << AWIN_DDMA_PARA_SRC_DATA_BLK_SIZ_SHIFT) |
		    (src_wc << AWIN_DDMA_PARA_SRC_WAIT_CYC_SHIFT));
	}

	return (0);
}

static void *
a10dmac_alloc(device_t dev, bool dedicated, void (*cb)(void *), void *cbarg)
{
	struct a10dmac_softc *sc = device_get_softc(dev);
	struct a10dmac_channel *ch_list;
	struct a10dmac_channel *ch = NULL;
	uint32_t irqen;
	uint8_t ch_count, index;

	if (dedicated) {
		ch_list = sc->sc_ddma_channels;
		ch_count = DDMA_CHANNELS;
	} else {
		ch_list = sc->sc_ndma_channels;
		ch_count = NDMA_CHANNELS;
	}

	mtx_lock_spin(&sc->sc_mtx);
	for (index = 0; index < ch_count; index++) {
		if (ch_list[index].ch_callback == NULL) {
			ch = &ch_list[index];
			ch->ch_callback = cb;
			ch->ch_callbackarg = cbarg;

			irqen = DMA_READ(sc, AWIN_DMA_IRQ_EN_REG);
			if (ch->ch_type == CH_NDMA)
				irqen |= AWIN_DMA_IRQ_NDMA_END(index);
			else
				irqen |= AWIN_DMA_IRQ_DDMA_END(index);
			DMA_WRITE(sc, AWIN_DMA_IRQ_EN_REG, irqen);

			break;
		}
	}
	mtx_unlock_spin(&sc->sc_mtx);

	return (ch);
}

static void
a10dmac_free(device_t dev, void *priv)
{
	struct a10dmac_channel *ch = priv;
	struct a10dmac_softc *sc = ch->ch_sc;
	uint32_t irqen, sta, cfg;

	mtx_lock_spin(&sc->sc_mtx);

	irqen = DMA_READ(sc, AWIN_DMA_IRQ_EN_REG);
	cfg = a10dmac_read_ctl(ch);
	if (ch->ch_type == CH_NDMA) {
		sta = AWIN_DMA_IRQ_NDMA_END(ch->ch_index);
		cfg &= ~AWIN_NDMA_CTL_DMA_LOADING;
	} else {
		sta = AWIN_DMA_IRQ_DDMA_END(ch->ch_index);
		cfg &= ~AWIN_DDMA_CTL_DMA_LOADING;
	}
	irqen &= ~sta;
	a10dmac_write_ctl(ch, cfg);
	DMA_WRITE(sc, AWIN_DMA_IRQ_EN_REG, irqen);
	DMA_WRITE(sc, AWIN_DMA_IRQ_PEND_STA_REG, sta);

	ch->ch_callback = NULL;
	ch->ch_callbackarg = NULL;

	mtx_unlock_spin(&sc->sc_mtx);
}

static int
a10dmac_transfer(device_t dev, void *priv, bus_addr_t src, bus_addr_t dst,
    size_t nbytes)
{
	struct a10dmac_channel *ch = priv;
	uint32_t cfg;

	cfg = a10dmac_read_ctl(ch);
	if (ch->ch_type == CH_NDMA) {
		if (cfg & AWIN_NDMA_CTL_DMA_LOADING)
			return (EBUSY);

		DMACH_WRITE(ch, AWIN_NDMA_SRC_ADDR_REG, src);
		DMACH_WRITE(ch, AWIN_NDMA_DEST_ADDR_REG, dst);
		DMACH_WRITE(ch, AWIN_NDMA_BC_REG, nbytes);

		cfg |= AWIN_NDMA_CTL_DMA_LOADING;
		a10dmac_write_ctl(ch, cfg);
	} else {
		if (cfg & AWIN_DDMA_CTL_DMA_LOADING)
			return (EBUSY);

		DMACH_WRITE(ch, AWIN_DDMA_SRC_START_ADDR_REG, src);
		DMACH_WRITE(ch, AWIN_DDMA_DEST_START_ADDR_REG, dst);
		DMACH_WRITE(ch, AWIN_DDMA_BC_REG, nbytes);

		cfg |= AWIN_DDMA_CTL_DMA_LOADING;
		a10dmac_write_ctl(ch, cfg);
	}

	return (0);
}

static void
a10dmac_halt(device_t dev, void *priv)
{
	struct a10dmac_channel *ch = priv;
	uint32_t cfg;

	cfg = a10dmac_read_ctl(ch);
	if (ch->ch_type == CH_NDMA) {
		cfg &= ~AWIN_NDMA_CTL_DMA_LOADING;
	} else {
		cfg &= ~AWIN_DDMA_CTL_DMA_LOADING;
	}
	a10dmac_write_ctl(ch, cfg);
}

static device_method_t a10dmac_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		a10dmac_probe),
	DEVMETHOD(device_attach,	a10dmac_attach),

	/* sunxi DMA interface */
	DEVMETHOD(sunxi_dma_alloc,	a10dmac_alloc),
	DEVMETHOD(sunxi_dma_free,	a10dmac_free),
	DEVMETHOD(sunxi_dma_set_config,	a10dmac_set_config),
	DEVMETHOD(sunxi_dma_transfer,	a10dmac_transfer),
	DEVMETHOD(sunxi_dma_halt,	a10dmac_halt),

	DEVMETHOD_END
};

static driver_t a10dmac_driver = {
	"a10dmac",
	a10dmac_methods,
	sizeof(struct a10dmac_softc)
};

static devclass_t a10dmac_devclass;

DRIVER_MODULE(a10dmac, simplebus, a10dmac_driver, a10dmac_devclass, 0, 0);
