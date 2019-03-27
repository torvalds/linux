/*-
 * Copyright (c) 2014 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
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

/*
 * Synopsys DesignWare Mobile Storage Host Controller
 * Chapter 14, Altera Cyclone V Device Handbook (CV-5V2 2014.07.22)
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

#include <dev/mmc/bridge.h>
#include <dev/mmc/mmcbrvar.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/intr.h>

#ifdef EXT_RESOURCES
#include <dev/extres/clk/clk.h>
#endif

#include <dev/mmc/host/dwmmc_reg.h>
#include <dev/mmc/host/dwmmc_var.h>

#include "opt_mmccam.h"

#include "mmcbr_if.h"

#define dprintf(x, arg...)

#define	READ4(_sc, _reg) \
	bus_read_4((_sc)->res[0], _reg)
#define	WRITE4(_sc, _reg, _val) \
	bus_write_4((_sc)->res[0], _reg, _val)

#define	DIV_ROUND_UP(n, d)		howmany(n, d)

#define	DWMMC_LOCK(_sc)			mtx_lock(&(_sc)->sc_mtx)
#define	DWMMC_UNLOCK(_sc)		mtx_unlock(&(_sc)->sc_mtx)
#define	DWMMC_LOCK_INIT(_sc) \
	mtx_init(&_sc->sc_mtx, device_get_nameunit(_sc->dev), \
	    "dwmmc", MTX_DEF)
#define	DWMMC_LOCK_DESTROY(_sc)		mtx_destroy(&_sc->sc_mtx);
#define	DWMMC_ASSERT_LOCKED(_sc)	mtx_assert(&_sc->sc_mtx, MA_OWNED);
#define	DWMMC_ASSERT_UNLOCKED(_sc)	mtx_assert(&_sc->sc_mtx, MA_NOTOWNED);

#define	PENDING_CMD	0x01
#define	PENDING_STOP	0x02
#define	CARD_INIT_DONE	0x04

#define	DWMMC_DATA_ERR_FLAGS	(SDMMC_INTMASK_DRT | SDMMC_INTMASK_DCRC \
				|SDMMC_INTMASK_HTO | SDMMC_INTMASK_SBE \
				|SDMMC_INTMASK_EBE)
#define	DWMMC_CMD_ERR_FLAGS	(SDMMC_INTMASK_RTO | SDMMC_INTMASK_RCRC \
				|SDMMC_INTMASK_RE)
#define	DWMMC_ERR_FLAGS		(DWMMC_DATA_ERR_FLAGS | DWMMC_CMD_ERR_FLAGS \
				|SDMMC_INTMASK_HLE)

#define	DES0_DIC	(1 << 1)
#define	DES0_LD		(1 << 2)
#define	DES0_FS		(1 << 3)
#define	DES0_CH		(1 << 4)
#define	DES0_ER		(1 << 5)
#define	DES0_CES	(1 << 30)
#define	DES0_OWN	(1 << 31)

#define	DES1_BS1_MASK	0xfff
#define	DES1_BS1_SHIFT	0

struct idmac_desc {
	uint32_t	des0;	/* control */
	uint32_t	des1;	/* bufsize */
	uint32_t	des2;	/* buf1 phys addr */
	uint32_t	des3;	/* buf2 phys addr or next descr */
};

#define	DESC_MAX	256
#define	DESC_SIZE	(sizeof(struct idmac_desc) * DESC_MAX)
#define	DEF_MSIZE	0x2	/* Burst size of multiple transaction */

static void dwmmc_next_operation(struct dwmmc_softc *);
static int dwmmc_setup_bus(struct dwmmc_softc *, int);
static int dma_done(struct dwmmc_softc *, struct mmc_command *);
static int dma_stop(struct dwmmc_softc *);
static void pio_read(struct dwmmc_softc *, struct mmc_command *);
static void pio_write(struct dwmmc_softc *, struct mmc_command *);

static struct resource_spec dwmmc_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,	RF_ACTIVE },
	{ -1, 0 }
};

#define	HWTYPE_MASK		(0x0000ffff)
#define	HWFLAG_MASK		(0xffff << 16)

static void
dwmmc_get1paddr(void *arg, bus_dma_segment_t *segs, int nsegs, int error)
{

	if (error != 0)
		return;
	*(bus_addr_t *)arg = segs[0].ds_addr;
}

static void
dwmmc_ring_setup(void *arg, bus_dma_segment_t *segs, int nsegs, int error)
{
	struct dwmmc_softc *sc;
	int idx;

	if (error != 0)
		return;

	sc = arg;

	dprintf("nsegs %d seg0len %lu\n", nsegs, segs[0].ds_len);

	for (idx = 0; idx < nsegs; idx++) {
		sc->desc_ring[idx].des0 = (DES0_OWN | DES0_DIC | DES0_CH);
		sc->desc_ring[idx].des1 = segs[idx].ds_len;
		sc->desc_ring[idx].des2 = segs[idx].ds_addr;

		if (idx == 0)
			sc->desc_ring[idx].des0 |= DES0_FS;

		if (idx == (nsegs - 1)) {
			sc->desc_ring[idx].des0 &= ~(DES0_DIC | DES0_CH);
			sc->desc_ring[idx].des0 |= DES0_LD;
		}
	}
}

static int
dwmmc_ctrl_reset(struct dwmmc_softc *sc, int reset_bits)
{
	int reg;
	int i;

	reg = READ4(sc, SDMMC_CTRL);
	reg |= (reset_bits);
	WRITE4(sc, SDMMC_CTRL, reg);

	/* Wait reset done */
	for (i = 0; i < 100; i++) {
		if (!(READ4(sc, SDMMC_CTRL) & reset_bits))
			return (0);
		DELAY(10);
	}

	device_printf(sc->dev, "Reset failed\n");

	return (1);
}

static int
dma_setup(struct dwmmc_softc *sc)
{
	int error;
	int nidx;
	int idx;

	/*
	 * Set up TX descriptor ring, descriptors, and dma maps.
	 */
	error = bus_dma_tag_create(
	    bus_get_dma_tag(sc->dev),	/* Parent tag. */
	    4096, 0,			/* alignment, boundary */
	    BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    DESC_SIZE, 1, 		/* maxsize, nsegments */
	    DESC_SIZE,			/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->desc_tag);
	if (error != 0) {
		device_printf(sc->dev,
		    "could not create ring DMA tag.\n");
		return (1);
	}

	error = bus_dmamem_alloc(sc->desc_tag, (void**)&sc->desc_ring,
	    BUS_DMA_COHERENT | BUS_DMA_WAITOK | BUS_DMA_ZERO,
	    &sc->desc_map);
	if (error != 0) {
		device_printf(sc->dev,
		    "could not allocate descriptor ring.\n");
		return (1);
	}

	error = bus_dmamap_load(sc->desc_tag, sc->desc_map,
	    sc->desc_ring, DESC_SIZE, dwmmc_get1paddr,
	    &sc->desc_ring_paddr, 0);
	if (error != 0) {
		device_printf(sc->dev,
		    "could not load descriptor ring map.\n");
		return (1);
	}

	for (idx = 0; idx < sc->desc_count; idx++) {
		sc->desc_ring[idx].des0 = DES0_CH;
		sc->desc_ring[idx].des1 = 0;
		nidx = (idx + 1) % sc->desc_count;
		sc->desc_ring[idx].des3 = sc->desc_ring_paddr + \
		    (nidx * sizeof(struct idmac_desc));
	}

	error = bus_dma_tag_create(
	    bus_get_dma_tag(sc->dev),	/* Parent tag. */
	    4096, 0,			/* alignment, boundary */
	    BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    sc->desc_count * MMC_SECTOR_SIZE, /* maxsize */
	    sc->desc_count,		/* nsegments */
	    MMC_SECTOR_SIZE,		/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->buf_tag);
	if (error != 0) {
		device_printf(sc->dev,
		    "could not create ring DMA tag.\n");
		return (1);
	}

	error = bus_dmamap_create(sc->buf_tag, 0,
	    &sc->buf_map);
	if (error != 0) {
		device_printf(sc->dev,
		    "could not create TX buffer DMA map.\n");
		return (1);
	}

	return (0);
}

static void
dwmmc_cmd_done(struct dwmmc_softc *sc)
{
	struct mmc_command *cmd;

	cmd = sc->curcmd;
	if (cmd == NULL)
		return;

	if (cmd->flags & MMC_RSP_PRESENT) {
		if (cmd->flags & MMC_RSP_136) {
			cmd->resp[3] = READ4(sc, SDMMC_RESP0);
			cmd->resp[2] = READ4(sc, SDMMC_RESP1);
			cmd->resp[1] = READ4(sc, SDMMC_RESP2);
			cmd->resp[0] = READ4(sc, SDMMC_RESP3);
		} else {
			cmd->resp[3] = 0;
			cmd->resp[2] = 0;
			cmd->resp[1] = 0;
			cmd->resp[0] = READ4(sc, SDMMC_RESP0);
		}
	}
}

static void
dwmmc_tasklet(struct dwmmc_softc *sc)
{
	struct mmc_command *cmd;

	cmd = sc->curcmd;
	if (cmd == NULL)
		return;

	if (!sc->cmd_done)
		return;

	if (cmd->error != MMC_ERR_NONE || !cmd->data) {
		dwmmc_next_operation(sc);
	} else if (cmd->data && sc->dto_rcvd) {
		if ((cmd->opcode == MMC_WRITE_MULTIPLE_BLOCK ||
		     cmd->opcode == MMC_READ_MULTIPLE_BLOCK) &&
		     sc->use_auto_stop) {
			if (sc->acd_rcvd)
				dwmmc_next_operation(sc);
		} else {
			dwmmc_next_operation(sc);
		}
	}
}

static void
dwmmc_intr(void *arg)
{
	struct mmc_command *cmd;
	struct dwmmc_softc *sc;
	uint32_t reg;

	sc = arg;

	DWMMC_LOCK(sc);

	cmd = sc->curcmd;

	/* First handle SDMMC controller interrupts */
	reg = READ4(sc, SDMMC_MINTSTS);
	if (reg) {
		dprintf("%s 0x%08x\n", __func__, reg);

		if (reg & DWMMC_CMD_ERR_FLAGS) {
			dprintf("cmd err 0x%08x cmd 0x%08x\n",
				reg, cmd->opcode);
			cmd->error = MMC_ERR_TIMEOUT;
		}

		if (reg & DWMMC_DATA_ERR_FLAGS) {
			dprintf("data err 0x%08x cmd 0x%08x\n",
				reg, cmd->opcode);
			cmd->error = MMC_ERR_FAILED;
			if (!sc->use_pio) {
				dma_done(sc, cmd);
				dma_stop(sc);
			}
		}

		if (reg & SDMMC_INTMASK_CMD_DONE) {
			dwmmc_cmd_done(sc);
			sc->cmd_done = 1;
		}

		if (reg & SDMMC_INTMASK_ACD)
			sc->acd_rcvd = 1;

		if (reg & SDMMC_INTMASK_DTO)
			sc->dto_rcvd = 1;

		if (reg & SDMMC_INTMASK_CD) {
			/* XXX: Handle card detect */
		}
	}

	/* Ack interrupts */
	WRITE4(sc, SDMMC_RINTSTS, reg);

	if (sc->use_pio) {
		if (reg & (SDMMC_INTMASK_RXDR|SDMMC_INTMASK_DTO)) {
			pio_read(sc, cmd);
		}
		if (reg & (SDMMC_INTMASK_TXDR|SDMMC_INTMASK_DTO)) {
			pio_write(sc, cmd);
		}
	} else {
		/* Now handle DMA interrupts */
		reg = READ4(sc, SDMMC_IDSTS);
		if (reg) {
			dprintf("dma intr 0x%08x\n", reg);
			if (reg & (SDMMC_IDINTEN_TI | SDMMC_IDINTEN_RI)) {
				WRITE4(sc, SDMMC_IDSTS, (SDMMC_IDINTEN_TI |
							 SDMMC_IDINTEN_RI));
				WRITE4(sc, SDMMC_IDSTS, SDMMC_IDINTEN_NI);
				dma_done(sc, cmd);
			}
		}
	}

	dwmmc_tasklet(sc);

	DWMMC_UNLOCK(sc);
}

static int
parse_fdt(struct dwmmc_softc *sc)
{
	pcell_t dts_value[3];
	phandle_t node;
	uint32_t bus_hz = 0, bus_width;
	int len;
#ifdef EXT_RESOURCES
	int error;
#endif

	if ((node = ofw_bus_get_node(sc->dev)) == -1)
		return (ENXIO);

	/* bus-width */
	if (OF_getencprop(node, "bus-width", &bus_width, sizeof(uint32_t)) <= 0)
		bus_width = 4;
	if (bus_width >= 4)
		sc->host.caps |= MMC_CAP_4_BIT_DATA;
	if (bus_width >= 8)
		sc->host.caps |= MMC_CAP_8_BIT_DATA;

	/* max-frequency */
	if (OF_getencprop(node, "max-frequency", &sc->max_hz, sizeof(uint32_t)) <= 0)
		sc->max_hz = 200000000;

	/* fifo-depth */
	if ((len = OF_getproplen(node, "fifo-depth")) > 0) {
		OF_getencprop(node, "fifo-depth", dts_value, len);
		sc->fifo_depth = dts_value[0];
	}

	/* num-slots (Deprecated) */
	sc->num_slots = 1;
	if ((len = OF_getproplen(node, "num-slots")) > 0) {
		device_printf(sc->dev, "num-slots property is deprecated\n");
		OF_getencprop(node, "num-slots", dts_value, len);
		sc->num_slots = dts_value[0];
	}

	/* clock-frequency */
	if ((len = OF_getproplen(node, "clock-frequency")) > 0) {
		OF_getencprop(node, "clock-frequency", dts_value, len);
		bus_hz = dts_value[0];
	}

#ifdef EXT_RESOURCES
	/* BIU (Bus Interface Unit clock) is optional */
	error = clk_get_by_ofw_name(sc->dev, 0, "biu", &sc->biu);
	if (sc->biu) {
		error = clk_enable(sc->biu);
		if (error != 0) {
			device_printf(sc->dev, "cannot enable biu clock\n");
			goto fail;
		}
	}

	/*
	 * CIU (Controller Interface Unit clock) is mandatory
	 * if no clock-frequency property is given
	 */
	error = clk_get_by_ofw_name(sc->dev, 0, "ciu", &sc->ciu);
	if (sc->ciu) {
		error = clk_enable(sc->ciu);
		if (error != 0) {
			device_printf(sc->dev, "cannot enable ciu clock\n");
			goto fail;
		}
		if (bus_hz != 0) {
			error = clk_set_freq(sc->ciu, bus_hz, 0);
			if (error != 0)
				device_printf(sc->dev,
				    "cannot set ciu clock to %u\n", bus_hz);
		}
		clk_get_freq(sc->ciu, &sc->bus_hz);
	}
#endif /* EXT_RESOURCES */

	if (sc->bus_hz == 0) {
		device_printf(sc->dev, "No bus speed provided\n");
		goto fail;
	}

	return (0);

fail:
	return (ENXIO);
}

int
dwmmc_attach(device_t dev)
{
	struct dwmmc_softc *sc;
	int error;
	int slot;

	sc = device_get_softc(dev);

	sc->dev = dev;

	/* Why not to use Auto Stop? It save a hundred of irq per second */
	sc->use_auto_stop = 1;

	error = parse_fdt(sc);
	if (error != 0) {
		device_printf(dev, "Can't get FDT property.\n");
		return (ENXIO);
	}

	DWMMC_LOCK_INIT(sc);

	if (bus_alloc_resources(dev, dwmmc_spec, sc->res)) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	/* Setup interrupt handler. */
	error = bus_setup_intr(dev, sc->res[1], INTR_TYPE_NET | INTR_MPSAFE,
	    NULL, dwmmc_intr, sc, &sc->intr_cookie);
	if (error != 0) {
		device_printf(dev, "could not setup interrupt handler.\n");
		return (ENXIO);
	}

	device_printf(dev, "Hardware version ID is %04x\n",
		READ4(sc, SDMMC_VERID) & 0xffff);

	if (sc->desc_count == 0)
		sc->desc_count = DESC_MAX;

	/* XXX: we support operation for slot index 0 only */
	slot = 0;
	if (sc->pwren_inverted) {
		WRITE4(sc, SDMMC_PWREN, (0 << slot));
	} else {
		WRITE4(sc, SDMMC_PWREN, (1 << slot));
	}

	/* Reset all */
	if (dwmmc_ctrl_reset(sc, (SDMMC_CTRL_RESET |
				  SDMMC_CTRL_FIFO_RESET |
				  SDMMC_CTRL_DMA_RESET)))
		return (ENXIO);

	dwmmc_setup_bus(sc, sc->host.f_min);

	if (sc->fifo_depth == 0) {
		sc->fifo_depth = 1 +
		    ((READ4(sc, SDMMC_FIFOTH) >> SDMMC_FIFOTH_RXWMARK_S) & 0xfff);
		device_printf(dev, "No fifo-depth, using FIFOTH %x\n",
		    sc->fifo_depth);
	}

	if (!sc->use_pio) {
		if (dma_setup(sc))
			return (ENXIO);

		/* Install desc base */
		WRITE4(sc, SDMMC_DBADDR, sc->desc_ring_paddr);

		/* Enable DMA interrupts */
		WRITE4(sc, SDMMC_IDSTS, SDMMC_IDINTEN_MASK);
		WRITE4(sc, SDMMC_IDINTEN, (SDMMC_IDINTEN_NI |
					   SDMMC_IDINTEN_RI |
					   SDMMC_IDINTEN_TI));
	}

	/* Clear and disable interrups for a while */
	WRITE4(sc, SDMMC_RINTSTS, 0xffffffff);
	WRITE4(sc, SDMMC_INTMASK, 0);

	/* Maximum timeout */
	WRITE4(sc, SDMMC_TMOUT, 0xffffffff);

	/* Enable interrupts */
	WRITE4(sc, SDMMC_RINTSTS, 0xffffffff);
	WRITE4(sc, SDMMC_INTMASK, (SDMMC_INTMASK_CMD_DONE |
				   SDMMC_INTMASK_DTO |
				   SDMMC_INTMASK_ACD |
				   SDMMC_INTMASK_TXDR |
				   SDMMC_INTMASK_RXDR |
				   DWMMC_ERR_FLAGS |
				   SDMMC_INTMASK_CD));
	WRITE4(sc, SDMMC_CTRL, SDMMC_CTRL_INT_ENABLE);

	sc->host.f_min = 400000;
	sc->host.f_max = sc->max_hz;
	sc->host.host_ocr = MMC_OCR_320_330 | MMC_OCR_330_340;
	sc->host.caps |= MMC_CAP_HSPEED;
	sc->host.caps |= MMC_CAP_SIGNALING_330;

	device_add_child(dev, "mmc", -1);
	return (bus_generic_attach(dev));
}

static int
dwmmc_setup_bus(struct dwmmc_softc *sc, int freq)
{
	int tout;
	int div;

	if (freq == 0) {
		WRITE4(sc, SDMMC_CLKENA, 0);
		WRITE4(sc, SDMMC_CMD, (SDMMC_CMD_WAIT_PRVDATA |
			SDMMC_CMD_UPD_CLK_ONLY | SDMMC_CMD_START));

		tout = 1000;
		do {
			if (tout-- < 0) {
				device_printf(sc->dev, "Failed update clk\n");
				return (1);
			}
		} while (READ4(sc, SDMMC_CMD) & SDMMC_CMD_START);

		return (0);
	}

	WRITE4(sc, SDMMC_CLKENA, 0);
	WRITE4(sc, SDMMC_CLKSRC, 0);

	div = (sc->bus_hz != freq) ? DIV_ROUND_UP(sc->bus_hz, 2 * freq) : 0;

	WRITE4(sc, SDMMC_CLKDIV, div);
	WRITE4(sc, SDMMC_CMD, (SDMMC_CMD_WAIT_PRVDATA |
			SDMMC_CMD_UPD_CLK_ONLY | SDMMC_CMD_START));

	tout = 1000;
	do {
		if (tout-- < 0) {
			device_printf(sc->dev, "Failed to update clk");
			return (1);
		}
	} while (READ4(sc, SDMMC_CMD) & SDMMC_CMD_START);

	WRITE4(sc, SDMMC_CLKENA, (SDMMC_CLKENA_CCLK_EN | SDMMC_CLKENA_LP));
	WRITE4(sc, SDMMC_CMD, SDMMC_CMD_WAIT_PRVDATA |
			SDMMC_CMD_UPD_CLK_ONLY | SDMMC_CMD_START);

	tout = 1000;
	do {
		if (tout-- < 0) {
			device_printf(sc->dev, "Failed to enable clk\n");
			return (1);
		}
	} while (READ4(sc, SDMMC_CMD) & SDMMC_CMD_START);

	return (0);
}

static int
dwmmc_update_ios(device_t brdev, device_t reqdev)
{
	struct dwmmc_softc *sc;
	struct mmc_ios *ios;
	uint32_t reg;
	int ret = 0;

	sc = device_get_softc(brdev);
	ios = &sc->host.ios;

	dprintf("Setting up clk %u bus_width %d\n",
		ios->clock, ios->bus_width);

	if (ios->bus_width == bus_width_8)
		WRITE4(sc, SDMMC_CTYPE, SDMMC_CTYPE_8BIT);
	else if (ios->bus_width == bus_width_4)
		WRITE4(sc, SDMMC_CTYPE, SDMMC_CTYPE_4BIT);
	else
		WRITE4(sc, SDMMC_CTYPE, 0);

	if ((sc->hwtype & HWTYPE_MASK) == HWTYPE_EXYNOS) {
		/* XXX: take care about DDR or SDR use here */
		WRITE4(sc, SDMMC_CLKSEL, sc->sdr_timing);
	}

	/* Set DDR mode */
	reg = READ4(sc, SDMMC_UHS_REG);
	if (ios->timing == bus_timing_uhs_ddr50 ||
	    ios->timing == bus_timing_mmc_ddr52 ||
	    ios->timing == bus_timing_mmc_hs400)
		reg |= (SDMMC_UHS_REG_DDR);
	else
		reg &= ~(SDMMC_UHS_REG_DDR);
	WRITE4(sc, SDMMC_UHS_REG, reg);

	if (sc->update_ios)
		ret = sc->update_ios(sc, ios);

	dwmmc_setup_bus(sc, ios->clock);

	return (ret);
}

static int
dma_done(struct dwmmc_softc *sc, struct mmc_command *cmd)
{
	struct mmc_data *data;

	data = cmd->data;

	if (data->flags & MMC_DATA_WRITE)
		bus_dmamap_sync(sc->buf_tag, sc->buf_map,
			BUS_DMASYNC_POSTWRITE);
	else
		bus_dmamap_sync(sc->buf_tag, sc->buf_map,
			BUS_DMASYNC_POSTREAD);

	bus_dmamap_sync(sc->desc_tag, sc->desc_map,
	    BUS_DMASYNC_POSTWRITE);

	bus_dmamap_unload(sc->buf_tag, sc->buf_map);

	return (0);
}

static int
dma_stop(struct dwmmc_softc *sc)
{
	int reg;

	reg = READ4(sc, SDMMC_CTRL);
	reg &= ~(SDMMC_CTRL_USE_IDMAC);
	reg |= (SDMMC_CTRL_DMA_RESET);
	WRITE4(sc, SDMMC_CTRL, reg);

	reg = READ4(sc, SDMMC_BMOD);
	reg &= ~(SDMMC_BMOD_DE | SDMMC_BMOD_FB);
	reg |= (SDMMC_BMOD_SWR);
	WRITE4(sc, SDMMC_BMOD, reg);

	return (0);
}

static int
dma_prepare(struct dwmmc_softc *sc, struct mmc_command *cmd)
{
	struct mmc_data *data;
	int err;
	int reg;

	data = cmd->data;

	reg = READ4(sc, SDMMC_INTMASK);
	reg &= ~(SDMMC_INTMASK_TXDR | SDMMC_INTMASK_RXDR);
	WRITE4(sc, SDMMC_INTMASK, reg);

	err = bus_dmamap_load(sc->buf_tag, sc->buf_map,
		data->data, data->len, dwmmc_ring_setup,
		sc, BUS_DMA_NOWAIT);
	if (err != 0)
		panic("dmamap_load failed\n");

	/* Ensure the device can see the desc */
	bus_dmamap_sync(sc->desc_tag, sc->desc_map,
	    BUS_DMASYNC_PREWRITE);

	if (data->flags & MMC_DATA_WRITE)
		bus_dmamap_sync(sc->buf_tag, sc->buf_map,
			BUS_DMASYNC_PREWRITE);
	else
		bus_dmamap_sync(sc->buf_tag, sc->buf_map,
			BUS_DMASYNC_PREREAD);

	reg = (DEF_MSIZE << SDMMC_FIFOTH_MSIZE_S);
	reg |= ((sc->fifo_depth / 2) - 1) << SDMMC_FIFOTH_RXWMARK_S;
	reg |= (sc->fifo_depth / 2) << SDMMC_FIFOTH_TXWMARK_S;

	WRITE4(sc, SDMMC_FIFOTH, reg);
	wmb();

	reg = READ4(sc, SDMMC_CTRL);
	reg |= (SDMMC_CTRL_USE_IDMAC | SDMMC_CTRL_DMA_ENABLE);
	WRITE4(sc, SDMMC_CTRL, reg);
	wmb();

	reg = READ4(sc, SDMMC_BMOD);
	reg |= (SDMMC_BMOD_DE | SDMMC_BMOD_FB);
	WRITE4(sc, SDMMC_BMOD, reg);

	/* Start */
	WRITE4(sc, SDMMC_PLDMND, 1);

	return (0);
}

static int
pio_prepare(struct dwmmc_softc *sc, struct mmc_command *cmd)
{
	struct mmc_data *data;
	int reg;

	data = cmd->data;
	data->xfer_len = 0;

	reg = (DEF_MSIZE << SDMMC_FIFOTH_MSIZE_S);
	reg |= ((sc->fifo_depth / 2) - 1) << SDMMC_FIFOTH_RXWMARK_S;
	reg |= (sc->fifo_depth / 2) << SDMMC_FIFOTH_TXWMARK_S;

	WRITE4(sc, SDMMC_FIFOTH, reg);
	wmb();

	return (0);
}

static void
pio_read(struct dwmmc_softc *sc, struct mmc_command *cmd)
{
	struct mmc_data *data;
	uint32_t *p, status;

	if (cmd == NULL || cmd->data == NULL)
		return;

	data = cmd->data;
	if ((data->flags & MMC_DATA_READ) == 0)
		return;

	KASSERT((data->xfer_len & 3) == 0, ("xfer_len not aligned"));
	p = (uint32_t *)data->data + (data->xfer_len >> 2);

	while (data->xfer_len < data->len) {
		status = READ4(sc, SDMMC_STATUS);
		if (status & SDMMC_STATUS_FIFO_EMPTY)
			break;
		*p++ = READ4(sc, SDMMC_DATA);
		data->xfer_len += 4;
	}

	WRITE4(sc, SDMMC_RINTSTS, SDMMC_INTMASK_RXDR);
}

static void
pio_write(struct dwmmc_softc *sc, struct mmc_command *cmd)
{
	struct mmc_data *data;
	uint32_t *p, status;

	if (cmd == NULL || cmd->data == NULL)
		return;

	data = cmd->data;
	if ((data->flags & MMC_DATA_WRITE) == 0)
		return;

	KASSERT((data->xfer_len & 3) == 0, ("xfer_len not aligned"));
	p = (uint32_t *)data->data + (data->xfer_len >> 2);

	while (data->xfer_len < data->len) {
		status = READ4(sc, SDMMC_STATUS);
		if (status & SDMMC_STATUS_FIFO_FULL)
			break;
		WRITE4(sc, SDMMC_DATA, *p++);
		data->xfer_len += 4;
	}

	WRITE4(sc, SDMMC_RINTSTS, SDMMC_INTMASK_TXDR);
}

static void
dwmmc_start_cmd(struct dwmmc_softc *sc, struct mmc_command *cmd)
{
	struct mmc_data *data;
	uint32_t blksz;
	uint32_t cmdr;

	sc->curcmd = cmd;
	data = cmd->data;

	if ((sc->hwtype & HWTYPE_MASK) == HWTYPE_ROCKCHIP)
		dwmmc_setup_bus(sc, sc->host.ios.clock);

	/* XXX Upper layers don't always set this */
	cmd->mrq = sc->req;

	/* Begin setting up command register. */

	cmdr = cmd->opcode;

	dprintf("cmd->opcode 0x%08x\n", cmd->opcode);

	if (cmd->opcode == MMC_STOP_TRANSMISSION ||
	    cmd->opcode == MMC_GO_IDLE_STATE ||
	    cmd->opcode == MMC_GO_INACTIVE_STATE)
		cmdr |= SDMMC_CMD_STOP_ABORT;
	else if (cmd->opcode != MMC_SEND_STATUS && data)
		cmdr |= SDMMC_CMD_WAIT_PRVDATA;

	/* Set up response handling. */
	if (MMC_RSP(cmd->flags) != MMC_RSP_NONE) {
		cmdr |= SDMMC_CMD_RESP_EXP;
		if (cmd->flags & MMC_RSP_136)
			cmdr |= SDMMC_CMD_RESP_LONG;
	}

	if (cmd->flags & MMC_RSP_CRC)
		cmdr |= SDMMC_CMD_RESP_CRC;

	/*
	 * XXX: Not all platforms want this.
	 */
	cmdr |= SDMMC_CMD_USE_HOLD_REG;

	if ((sc->flags & CARD_INIT_DONE) == 0) {
		sc->flags |= (CARD_INIT_DONE);
		cmdr |= SDMMC_CMD_SEND_INIT;
	}

	if (data) {
		if ((cmd->opcode == MMC_WRITE_MULTIPLE_BLOCK ||
		     cmd->opcode == MMC_READ_MULTIPLE_BLOCK) &&
		     sc->use_auto_stop)
			cmdr |= SDMMC_CMD_SEND_ASTOP;

		cmdr |= SDMMC_CMD_DATA_EXP;
		if (data->flags & MMC_DATA_STREAM)
			cmdr |= SDMMC_CMD_MODE_STREAM;
		if (data->flags & MMC_DATA_WRITE)
			cmdr |= SDMMC_CMD_DATA_WRITE;

		WRITE4(sc, SDMMC_TMOUT, 0xffffffff);
		WRITE4(sc, SDMMC_BYTCNT, data->len);
		blksz = (data->len < MMC_SECTOR_SIZE) ? \
			 data->len : MMC_SECTOR_SIZE;
		WRITE4(sc, SDMMC_BLKSIZ, blksz);

		if (sc->use_pio) {
			pio_prepare(sc, cmd);
		} else {
			dma_prepare(sc, cmd);
		}
		wmb();
	}

	dprintf("cmdr 0x%08x\n", cmdr);

	WRITE4(sc, SDMMC_CMDARG, cmd->arg);
	wmb();
	WRITE4(sc, SDMMC_CMD, cmdr | SDMMC_CMD_START);
};

static void
dwmmc_next_operation(struct dwmmc_softc *sc)
{
	struct mmc_request *req;

	req = sc->req;
	if (req == NULL)
		return;

	sc->acd_rcvd = 0;
	sc->dto_rcvd = 0;
	sc->cmd_done = 0;

	/*
	 * XXX: Wait until card is still busy.
	 * We do need this to prevent data timeouts,
	 * mostly caused by multi-block write command
	 * followed by single-read.
	 */
	while(READ4(sc, SDMMC_STATUS) & (SDMMC_STATUS_DATA_BUSY))
		continue;

	if (sc->flags & PENDING_CMD) {
		sc->flags &= ~PENDING_CMD;
		dwmmc_start_cmd(sc, req->cmd);
		return;
	} else if (sc->flags & PENDING_STOP && !sc->use_auto_stop) {
		sc->flags &= ~PENDING_STOP;
		dwmmc_start_cmd(sc, req->stop);
		return;
	}

	sc->req = NULL;
	sc->curcmd = NULL;
	req->done(req);
}

static int
dwmmc_request(device_t brdev, device_t reqdev, struct mmc_request *req)
{
	struct dwmmc_softc *sc;

	sc = device_get_softc(brdev);

	dprintf("%s\n", __func__);

	DWMMC_LOCK(sc);

	if (sc->req != NULL) {
		DWMMC_UNLOCK(sc);
		return (EBUSY);
	}

	sc->req = req;
	sc->flags |= PENDING_CMD;
	if (sc->req->stop)
		sc->flags |= PENDING_STOP;
	dwmmc_next_operation(sc);

	DWMMC_UNLOCK(sc);
	return (0);
}

static int
dwmmc_get_ro(device_t brdev, device_t reqdev)
{

	dprintf("%s\n", __func__);

	return (0);
}

static int
dwmmc_acquire_host(device_t brdev, device_t reqdev)
{
	struct dwmmc_softc *sc;

	sc = device_get_softc(brdev);

	DWMMC_LOCK(sc);
	while (sc->bus_busy)
		msleep(sc, &sc->sc_mtx, PZERO, "dwmmcah", hz / 5);
	sc->bus_busy++;
	DWMMC_UNLOCK(sc);
	return (0);
}

static int
dwmmc_release_host(device_t brdev, device_t reqdev)
{
	struct dwmmc_softc *sc;

	sc = device_get_softc(brdev);

	DWMMC_LOCK(sc);
	sc->bus_busy--;
	wakeup(sc);
	DWMMC_UNLOCK(sc);
	return (0);
}

static int
dwmmc_read_ivar(device_t bus, device_t child, int which, uintptr_t *result)
{
	struct dwmmc_softc *sc;

	sc = device_get_softc(bus);

	switch (which) {
	default:
		return (EINVAL);
	case MMCBR_IVAR_BUS_MODE:
		*(int *)result = sc->host.ios.bus_mode;
		break;
	case MMCBR_IVAR_BUS_WIDTH:
		*(int *)result = sc->host.ios.bus_width;
		break;
	case MMCBR_IVAR_CHIP_SELECT:
		*(int *)result = sc->host.ios.chip_select;
		break;
	case MMCBR_IVAR_CLOCK:
		*(int *)result = sc->host.ios.clock;
		break;
	case MMCBR_IVAR_F_MIN:
		*(int *)result = sc->host.f_min;
		break;
	case MMCBR_IVAR_F_MAX:
		*(int *)result = sc->host.f_max;
		break;
	case MMCBR_IVAR_HOST_OCR:
		*(int *)result = sc->host.host_ocr;
		break;
	case MMCBR_IVAR_MODE:
		*(int *)result = sc->host.mode;
		break;
	case MMCBR_IVAR_OCR:
		*(int *)result = sc->host.ocr;
		break;
	case MMCBR_IVAR_POWER_MODE:
		*(int *)result = sc->host.ios.power_mode;
		break;
	case MMCBR_IVAR_VDD:
		*(int *)result = sc->host.ios.vdd;
		break;
	case MMCBR_IVAR_CAPS:
		*(int *)result = sc->host.caps;
		break;
	case MMCBR_IVAR_MAX_DATA:
		*(int *)result = sc->desc_count;
	}
	return (0);
}

static int
dwmmc_write_ivar(device_t bus, device_t child, int which, uintptr_t value)
{
	struct dwmmc_softc *sc;

	sc = device_get_softc(bus);

	switch (which) {
	default:
		return (EINVAL);
	case MMCBR_IVAR_BUS_MODE:
		sc->host.ios.bus_mode = value;
		break;
	case MMCBR_IVAR_BUS_WIDTH:
		sc->host.ios.bus_width = value;
		break;
	case MMCBR_IVAR_CHIP_SELECT:
		sc->host.ios.chip_select = value;
		break;
	case MMCBR_IVAR_CLOCK:
		sc->host.ios.clock = value;
		break;
	case MMCBR_IVAR_MODE:
		sc->host.mode = value;
		break;
	case MMCBR_IVAR_OCR:
		sc->host.ocr = value;
		break;
	case MMCBR_IVAR_POWER_MODE:
		sc->host.ios.power_mode = value;
		break;
	case MMCBR_IVAR_VDD:
		sc->host.ios.vdd = value;
		break;
	/* These are read-only */
	case MMCBR_IVAR_CAPS:
	case MMCBR_IVAR_HOST_OCR:
	case MMCBR_IVAR_F_MIN:
	case MMCBR_IVAR_F_MAX:
	case MMCBR_IVAR_MAX_DATA:
		return (EINVAL);
	}
	return (0);
}

static device_method_t dwmmc_methods[] = {
	/* Bus interface */
	DEVMETHOD(bus_read_ivar,	dwmmc_read_ivar),
	DEVMETHOD(bus_write_ivar,	dwmmc_write_ivar),

	/* mmcbr_if */
	DEVMETHOD(mmcbr_update_ios,	dwmmc_update_ios),
	DEVMETHOD(mmcbr_request,	dwmmc_request),
	DEVMETHOD(mmcbr_get_ro,		dwmmc_get_ro),
	DEVMETHOD(mmcbr_acquire_host,	dwmmc_acquire_host),
	DEVMETHOD(mmcbr_release_host,	dwmmc_release_host),

	DEVMETHOD_END
};

DEFINE_CLASS_0(dwmmc, dwmmc_driver, dwmmc_methods,
    sizeof(struct dwmmc_softc));
