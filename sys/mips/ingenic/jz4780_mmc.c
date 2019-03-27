/*-
 * Copyright (c) 2015 Alexander Kabaev <kan@FreeBSD.org>
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
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/resource.h>
#include <sys/rman.h>
#include <sys/sysctl.h>

#include <machine/bus.h>

#include <dev/extres/clk/clk.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/mmc/bridge.h>
#include <dev/mmc/mmcreg.h>
#include <dev/mmc/mmcbrvar.h>

#include <mips/ingenic/jz4780_regs.h>

#undef JZ_MMC_DEBUG

#define	JZ_MSC_MEMRES		0
#define	JZ_MSC_IRQRES		1
#define	JZ_MSC_RESSZ		2
#define	JZ_MSC_DMA_SEGS		128
#define	JZ_MSC_DMA_MAX_SIZE	MAXPHYS

#define JZ_MSC_INT_ERR_BITS	(JZ_INT_CRC_RES_ERR | JZ_INT_CRC_READ_ERR | \
				JZ_INT_CRC_WRITE_ERR | JZ_INT_TIMEOUT_RES | \
				JZ_INT_TIMEOUT_READ)
static int jz4780_mmc_pio_mode = 0;

TUNABLE_INT("hw.jz.mmc.pio_mode", &jz4780_mmc_pio_mode);

struct jz4780_mmc_dma_desc {
	uint32_t		dma_next;
	uint32_t		dma_phys;
	uint32_t		dma_len;
	uint32_t		dma_cmd;
};

struct jz4780_mmc_softc {
	bus_space_handle_t	sc_bsh;
	bus_space_tag_t		sc_bst;
	device_t		sc_dev;
	clk_t			sc_clk;
	int			sc_bus_busy;
	int			sc_resid;
	int			sc_timeout;
	struct callout		sc_timeoutc;
	struct mmc_host		sc_host;
	struct mmc_request *	sc_req;
	struct mtx		sc_mtx;
	struct resource *	sc_res[JZ_MSC_RESSZ];
	uint32_t		sc_intr_seen;
	uint32_t		sc_intr_mask;
	uint32_t		sc_intr_wait;
	void *			sc_intrhand;
	uint32_t		sc_cmdat;

	/* Fields required for DMA access. */
	bus_addr_t	  	sc_dma_desc_phys;
	bus_dmamap_t		sc_dma_map;
	bus_dma_tag_t 		sc_dma_tag;
	void * 			sc_dma_desc;
	bus_dmamap_t		sc_dma_buf_map;
	bus_dma_tag_t		sc_dma_buf_tag;
	int			sc_dma_inuse;
	int			sc_dma_map_err;
	uint32_t		sc_dma_ctl;
};

static struct resource_spec jz4780_mmc_res_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,	RF_ACTIVE | RF_SHAREABLE },
	{ -1,			0,	0 }
};

static int jz4780_mmc_probe(device_t);
static int jz4780_mmc_attach(device_t);
static int jz4780_mmc_detach(device_t);
static int jz4780_mmc_setup_dma(struct jz4780_mmc_softc *);
static int jz4780_mmc_reset(struct jz4780_mmc_softc *);
static void jz4780_mmc_intr(void *);
static int jz4780_mmc_enable_clock(struct jz4780_mmc_softc *);
static int jz4780_mmc_config_clock(struct jz4780_mmc_softc *, uint32_t);

static int jz4780_mmc_update_ios(device_t, device_t);
static int jz4780_mmc_request(device_t, device_t, struct mmc_request *);
static int jz4780_mmc_get_ro(device_t, device_t);
static int jz4780_mmc_acquire_host(device_t, device_t);
static int jz4780_mmc_release_host(device_t, device_t);

#define	JZ_MMC_LOCK(_sc)	mtx_lock(&(_sc)->sc_mtx)
#define	JZ_MMC_UNLOCK(_sc)	mtx_unlock(&(_sc)->sc_mtx)
#define	JZ_MMC_READ_2(_sc, _reg)					\
	bus_space_read_2((_sc)->sc_bst, (_sc)->sc_bsh, _reg)
#define	JZ_MMC_WRITE_2(_sc, _reg, _value)				\
	bus_space_write_2((_sc)->sc_bst, (_sc)->sc_bsh, _reg, _value)
#define	JZ_MMC_READ_4(_sc, _reg)					\
	bus_space_read_4((_sc)->sc_bst, (_sc)->sc_bsh, _reg)
#define	JZ_MMC_WRITE_4(_sc, _reg, _value)				\
	bus_space_write_4((_sc)->sc_bst, (_sc)->sc_bsh, _reg, _value)

static int
jz4780_mmc_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);
	if (!ofw_bus_is_compatible(dev, "ingenic,jz4780-mmc"))
		return (ENXIO);
	if (device_get_unit(dev) > 0) /* XXXKAN */
		return (ENXIO);
	device_set_desc(dev, "Ingenic JZ4780 Integrated MMC/SD controller");

	return (BUS_PROBE_DEFAULT);
}

static int
jz4780_mmc_attach(device_t dev)
{
	struct jz4780_mmc_softc *sc;
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid_list *tree;
	device_t child;
	ssize_t len;
	pcell_t prop;
	phandle_t node;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;
	sc->sc_req = NULL;
	if (bus_alloc_resources(dev, jz4780_mmc_res_spec, sc->sc_res) != 0) {
		device_printf(dev, "cannot allocate device resources\n");
		return (ENXIO);
	}
	sc->sc_bst = rman_get_bustag(sc->sc_res[JZ_MSC_MEMRES]);
	sc->sc_bsh = rman_get_bushandle(sc->sc_res[JZ_MSC_MEMRES]);
	if (bus_setup_intr(dev, sc->sc_res[JZ_MSC_IRQRES],
	    INTR_TYPE_MISC | INTR_MPSAFE, NULL, jz4780_mmc_intr, sc,
	    &sc->sc_intrhand)) {
		bus_release_resources(dev, jz4780_mmc_res_spec, sc->sc_res);
		device_printf(dev, "cannot setup interrupt handler\n");
		return (ENXIO);
	}
	sc->sc_timeout = 10;
	ctx = device_get_sysctl_ctx(dev);
	tree = SYSCTL_CHILDREN(device_get_sysctl_tree(dev));
	SYSCTL_ADD_INT(ctx, tree, OID_AUTO, "req_timeout", CTLFLAG_RW,
	    &sc->sc_timeout, 0, "Request timeout in seconds");
	mtx_init(&sc->sc_mtx, device_get_nameunit(sc->sc_dev), "jz4780_mmc",
	    MTX_DEF);
	callout_init_mtx(&sc->sc_timeoutc, &sc->sc_mtx, 0);

	/* Reset controller. */
	if (jz4780_mmc_reset(sc) != 0) {
		device_printf(dev, "cannot reset the controller\n");
		goto fail;
	}
	if (jz4780_mmc_pio_mode == 0 && jz4780_mmc_setup_dma(sc) != 0) {
		device_printf(sc->sc_dev, "Couldn't setup DMA!\n");
		jz4780_mmc_pio_mode = 1;
	}
	if (bootverbose)
		device_printf(sc->sc_dev, "DMA status: %s\n",
		    jz4780_mmc_pio_mode ? "disabled" : "enabled");

	node = ofw_bus_get_node(dev);
	/* Determine max operating frequency */
	sc->sc_host.f_max = 24000000;
	len = OF_getencprop(node, "max-frequency", &prop, sizeof(prop));
	if (len / sizeof(prop) == 1)
		sc->sc_host.f_max = prop;
	sc->sc_host.f_min = sc->sc_host.f_max / 128;

	sc->sc_host.host_ocr = MMC_OCR_320_330 | MMC_OCR_330_340;
	sc->sc_host.caps = MMC_CAP_HSPEED;
	sc->sc_host.mode = mode_sd;
	/*
	 * Check for bus-width property, default to both 4 and 8 bit
	 * if no bus width is specified.
	 */
	len = OF_getencprop(node, "bus-width", &prop, sizeof(prop));
	if (len / sizeof(prop) != 1)
		sc->sc_host.caps |= MMC_CAP_4_BIT_DATA | MMC_CAP_8_BIT_DATA;
	else if (prop == 8)
		sc->sc_host.caps |= MMC_CAP_8_BIT_DATA;
	else if (prop == 4)
		sc->sc_host.caps |= MMC_CAP_4_BIT_DATA;
	/* Activate the module clock. */
	if (jz4780_mmc_enable_clock(sc) != 0) {
		device_printf(dev, "cannot activate mmc clock\n");
		goto fail;
	}

	child = device_add_child(dev, "mmc", -1);
	if (child == NULL) {
		device_printf(dev, "attaching MMC bus failed!\n");
		goto fail;
	}
	if (device_probe_and_attach(child) != 0) {
		device_printf(dev, "attaching MMC child failed!\n");
		device_delete_child(dev, child);
		goto fail;
	}

	return (0);

fail:
	callout_drain(&sc->sc_timeoutc);
	mtx_destroy(&sc->sc_mtx);
	bus_teardown_intr(dev, sc->sc_res[JZ_MSC_IRQRES], sc->sc_intrhand);
	bus_release_resources(dev, jz4780_mmc_res_spec, sc->sc_res);
	if (sc->sc_clk != NULL)
		clk_release(sc->sc_clk);
	return (ENXIO);
}

static int
jz4780_mmc_detach(device_t dev)
{

	return (EBUSY);
}

static int
jz4780_mmc_enable_clock(struct jz4780_mmc_softc *sc)
{
	int err;

	err = clk_get_by_ofw_name(sc->sc_dev, 0, "mmc", &sc->sc_clk);
	if (err == 0)
		err = clk_enable(sc->sc_clk);
	if (err == 0)
		err = clk_set_freq(sc->sc_clk, sc->sc_host.f_max, 0);
	if (err != 0)
		clk_release(sc->sc_clk);
	return (err);
}

static void
jz4780_mmc_dma_desc_cb(void *arg, bus_dma_segment_t *segs, int nsegs, int err)
{
	struct jz4780_mmc_softc *sc;

	sc = (struct jz4780_mmc_softc *)arg;
	if (err) {
		sc->sc_dma_map_err = err;
		return;
	}
	sc->sc_dma_desc_phys = segs[0].ds_addr;
}

static int
jz4780_mmc_setup_dma(struct jz4780_mmc_softc *sc)
{
	int dma_desc_size, error;

	/* Allocate the DMA descriptor memory. */
	dma_desc_size = sizeof(struct jz4780_mmc_dma_desc) * JZ_MSC_DMA_SEGS;
	error = bus_dma_tag_create(bus_get_dma_tag(sc->sc_dev), 1, 0,
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL,
	    dma_desc_size, 1, dma_desc_size, 0, NULL, NULL, &sc->sc_dma_tag);
	if (error)
		return (error);
	error = bus_dmamem_alloc(sc->sc_dma_tag, &sc->sc_dma_desc,
	    BUS_DMA_WAITOK | BUS_DMA_ZERO, &sc->sc_dma_map);
	if (error)
		return (error);

	error = bus_dmamap_load(sc->sc_dma_tag, sc->sc_dma_map,
	    sc->sc_dma_desc, dma_desc_size, jz4780_mmc_dma_desc_cb, sc, 0);
	if (error)
		return (error);
	if (sc->sc_dma_map_err)
		return (sc->sc_dma_map_err);

	/* Create the DMA map for data transfers. */
	error = bus_dma_tag_create(bus_get_dma_tag(sc->sc_dev), 1, 0,
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL,
	    JZ_MSC_DMA_MAX_SIZE * JZ_MSC_DMA_SEGS, JZ_MSC_DMA_SEGS,
	    JZ_MSC_DMA_MAX_SIZE, BUS_DMA_ALLOCNOW, NULL, NULL,
	    &sc->sc_dma_buf_tag);
	if (error)
		return (error);
	error = bus_dmamap_create(sc->sc_dma_buf_tag, 0,
	    &sc->sc_dma_buf_map);
	if (error)
		return (error);

	return (0);
}

static void
jz4780_mmc_dma_cb(void *arg, bus_dma_segment_t *segs, int nsegs, int err)
{
	struct jz4780_mmc_dma_desc *dma_desc;
	struct jz4780_mmc_softc *sc;
	uint32_t dma_desc_phys;
	int i;

	sc = (struct jz4780_mmc_softc *)arg;
	sc->sc_dma_map_err = err;
	dma_desc = sc->sc_dma_desc;
	dma_desc_phys = sc->sc_dma_desc_phys;

	/* Note nsegs is guaranteed to be zero if err is non-zero. */
	for (i = 0; i < nsegs; i++) {
		dma_desc[i].dma_phys = segs[i].ds_addr;
		dma_desc[i].dma_len  = segs[i].ds_len;
		if (i < (nsegs - 1)) {
			dma_desc_phys += sizeof(struct jz4780_mmc_dma_desc);
			dma_desc[i].dma_next = dma_desc_phys;
			dma_desc[i].dma_cmd = (i << 16) | JZ_DMA_LINK;
		} else {
			dma_desc[i].dma_next = 0;
			dma_desc[i].dma_cmd = (i << 16) | JZ_DMA_ENDI;
		}
#ifdef JZ_MMC_DEBUG
		device_printf(sc->sc_dev, "%d: desc %#x phys %#x len %d next %#x cmd %#x\n",
		    i, dma_desc_phys - sizeof(struct jz4780_mmc_dma_desc),
		    dma_desc[i].dma_phys, dma_desc[i].dma_len,
		    dma_desc[i].dma_next, dma_desc[i].dma_cmd);
#endif
 	}
}

static int
jz4780_mmc_prepare_dma(struct jz4780_mmc_softc *sc)
{
	bus_dmasync_op_t sync_op;
	int error;
	struct mmc_command *cmd;
	uint32_t off;

	cmd = sc->sc_req->cmd;
	if (cmd->data->len > JZ_MSC_DMA_MAX_SIZE * JZ_MSC_DMA_SEGS)
		return (EFBIG);
	error = bus_dmamap_load(sc->sc_dma_buf_tag, sc->sc_dma_buf_map,
	    cmd->data->data, cmd->data->len, jz4780_mmc_dma_cb, sc,
	    BUS_DMA_NOWAIT);
	if (error)
		return (error);
	if (sc->sc_dma_map_err)
		return (sc->sc_dma_map_err);

	sc->sc_dma_inuse = 1;
	if (cmd->data->flags & MMC_DATA_WRITE)
		sync_op = BUS_DMASYNC_PREWRITE;
	else
		sync_op = BUS_DMASYNC_PREREAD;
	bus_dmamap_sync(sc->sc_dma_buf_tag, sc->sc_dma_buf_map, sync_op);
	bus_dmamap_sync(sc->sc_dma_tag, sc->sc_dma_map, BUS_DMASYNC_PREWRITE);

	/* Configure default DMA parameters */
	sc->sc_dma_ctl = JZ_MODE_SEL | JZ_INCR_64 | JZ_DMAEN;

	/* Enable unaligned buffer handling */
	off = (uintptr_t)cmd->data->data & 3;
	if (off != 0)
		sc->sc_dma_ctl |= (off << JZ_AOFST_S) | JZ_ALIGNEN;
	return (0);
}

static void
jz4780_mmc_start_dma(struct jz4780_mmc_softc *sc)
{

	/* Set the address of the first descriptor */
	JZ_MMC_WRITE_4(sc, JZ_MSC_DMANDA, sc->sc_dma_desc_phys);
	/* Enable and start the dma engine */
	JZ_MMC_WRITE_4(sc, JZ_MSC_DMAC, sc->sc_dma_ctl);
}

static int
jz4780_mmc_reset(struct jz4780_mmc_softc *sc)
{
	int timeout;

	/* Stop the clock */
	JZ_MMC_WRITE_4(sc, JZ_MSC_CTRL, JZ_CLOCK_STOP);

	timeout = 1000;
	while (--timeout > 0) {
		if ((JZ_MMC_READ_4(sc, JZ_MSC_STAT) & JZ_CLK_EN) == 0)
			break;
		DELAY(100);
	}
	if (timeout == 0) {
		device_printf(sc->sc_dev, "Failed to stop clk.\n");
		return (ETIMEDOUT);
	}

	/* Reset */
	JZ_MMC_WRITE_4(sc, JZ_MSC_CTRL, JZ_RESET);

	timeout = 10;
	while (--timeout > 0) {
		if ((JZ_MMC_READ_4(sc, JZ_MSC_STAT) & JZ_IS_RESETTING) == 0)
			break;
		DELAY(1000);
	}

	if (timeout == 0) {
		/*
		 * X1000 never clears reseting bit.
		 * Ignore for now.
		 */
	}

	/* Set the timeouts. */
	JZ_MMC_WRITE_4(sc, JZ_MSC_RESTO, 0xffff);
	JZ_MMC_WRITE_4(sc, JZ_MSC_RDTO, 0xffffffff);

	/* Mask all interrupt initially */
	JZ_MMC_WRITE_4(sc, JZ_MSC_IMASK, 0xffffffff);
	/* Clear pending interrupts. */
	JZ_MMC_WRITE_4(sc, JZ_MSC_IFLG, 0xffffffff);

	/* Remember interrupts we always want */
	sc->sc_intr_mask = JZ_MSC_INT_ERR_BITS;

	return (0);
}

static void
jz4780_mmc_req_done(struct jz4780_mmc_softc *sc)
{
	struct mmc_command *cmd;
	struct mmc_request *req;
	bus_dmasync_op_t sync_op;

	cmd = sc->sc_req->cmd;
	/* Reset the controller in case of errors */
	if (cmd->error != MMC_ERR_NONE)
		jz4780_mmc_reset(sc);
	/* Unmap DMA if necessary */
	if (sc->sc_dma_inuse == 1) {
		if (cmd->data->flags & MMC_DATA_WRITE)
			sync_op = BUS_DMASYNC_POSTWRITE;
		else
			sync_op = BUS_DMASYNC_POSTREAD;
		bus_dmamap_sync(sc->sc_dma_buf_tag, sc->sc_dma_buf_map,
		    sync_op);
		bus_dmamap_sync(sc->sc_dma_tag, sc->sc_dma_map,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dma_buf_tag, sc->sc_dma_buf_map);
	}
	req = sc->sc_req;
	callout_stop(&sc->sc_timeoutc);
	sc->sc_req = NULL;
	sc->sc_resid = 0;
	sc->sc_dma_inuse = 0;
	sc->sc_dma_map_err = 0;
	sc->sc_intr_wait = 0;
	sc->sc_intr_seen = 0;
	req->done(req);
}

static void
jz4780_mmc_read_response(struct jz4780_mmc_softc *sc)
{
	struct mmc_command *cmd;
	int i;

	cmd = sc->sc_req->cmd;
	if (cmd->flags & MMC_RSP_PRESENT) {
		if (cmd->flags & MMC_RSP_136) {
			uint16_t val;

			val = JZ_MMC_READ_2(sc, JZ_MSC_RES);
			for (i = 0; i < 4; i++) {
				cmd->resp[i] = val << 24;
				val = JZ_MMC_READ_2(sc, JZ_MSC_RES);
				cmd->resp[i] |= val << 8;
				val = JZ_MMC_READ_2(sc, JZ_MSC_RES);
				cmd->resp[i] |= val >> 8;
			}
		} else {
			cmd->resp[0] = JZ_MMC_READ_2(sc, JZ_MSC_RES) << 24;
			cmd->resp[0] |= JZ_MMC_READ_2(sc, JZ_MSC_RES) << 8;
			cmd->resp[0] |= JZ_MMC_READ_2(sc, JZ_MSC_RES) & 0xff;
		}
	}
}

static void
jz4780_mmc_req_ok(struct jz4780_mmc_softc *sc)
{
	struct mmc_command *cmd;

	cmd = sc->sc_req->cmd;
	/* All data has been transferred ? */
	if (cmd->data != NULL && (sc->sc_resid << 2) < cmd->data->len)
		cmd->error = MMC_ERR_FAILED;
	jz4780_mmc_req_done(sc);
}

static void
jz4780_mmc_timeout(void *arg)
{
	struct jz4780_mmc_softc *sc;

	sc = (struct jz4780_mmc_softc *)arg;
	if (sc->sc_req != NULL) {
		device_printf(sc->sc_dev, "controller timeout, rint %#x stat %#x\n",
		    JZ_MMC_READ_4(sc, JZ_MSC_IFLG), JZ_MMC_READ_4(sc, JZ_MSC_STAT));
		sc->sc_req->cmd->error = MMC_ERR_TIMEOUT;
		jz4780_mmc_req_done(sc);
	} else
		device_printf(sc->sc_dev,
		    "Spurious timeout - no active request\n");
}

static int
jz4780_mmc_pio_transfer(struct jz4780_mmc_softc *sc, struct mmc_data *data)
{
	uint32_t mask, *buf;
	int i, write;

	buf = (uint32_t *)data->data;
	write = (data->flags & MMC_DATA_WRITE) ? 1 : 0;
	mask = write ? JZ_DATA_FIFO_FULL : JZ_DATA_FIFO_EMPTY;
	for (i = sc->sc_resid; i < (data->len >> 2); i++) {
		if ((JZ_MMC_READ_4(sc, JZ_MSC_STAT) & mask))
			return (1);
		if (write)
			JZ_MMC_WRITE_4(sc, JZ_MSC_TXFIFO, buf[i]);
		else
			buf[i] = JZ_MMC_READ_4(sc, JZ_MSC_RXFIFO);
		sc->sc_resid = i + 1;
	}

	/* Done with pio transfer, shut FIFO interrupts down */
	mask = JZ_MMC_READ_4(sc, JZ_MSC_IMASK);
	mask |= (JZ_INT_TXFIFO_WR_REQ | JZ_INT_RXFIFO_RD_REQ);
	JZ_MMC_WRITE_4(sc, JZ_MSC_IMASK, mask);
	return (0);
}

static void
jz4780_mmc_intr(void *arg)
{
	struct jz4780_mmc_softc *sc;
	struct mmc_data *data;
	uint32_t rint;

	sc = (struct jz4780_mmc_softc *)arg;
	JZ_MMC_LOCK(sc);
	rint  = JZ_MMC_READ_4(sc, JZ_MSC_IFLG);
#if defined(JZ_MMC_DEBUG)
	device_printf(sc->sc_dev, "rint: %#x, stat: %#x\n",
	    rint, JZ_MMC_READ_4(sc, JZ_MSC_STAT));
	if (sc->sc_dma_inuse == 1 && (sc->sc_intr_seen & JZ_INT_DMAEND) == 0)
		device_printf(sc->sc_dev, "\tdmada %#x dmanext %#x dmac %#x"
		    " dmalen %d dmacmd %#x\n",
		    JZ_MMC_READ_4(sc, JZ_MSC_DMADA),
		    JZ_MMC_READ_4(sc, JZ_MSC_DMANDA),
		    JZ_MMC_READ_4(sc, JZ_MSC_DMAC),
		    JZ_MMC_READ_4(sc, JZ_MSC_DMALEN),
		    JZ_MMC_READ_4(sc, JZ_MSC_DMACMD));
#endif
	if (sc->sc_req == NULL) {
		device_printf(sc->sc_dev,
		    "Spurious interrupt - no active request, rint: 0x%08X\n",
		    rint);
		goto end;
	}
	if (rint & JZ_MSC_INT_ERR_BITS) {
#if defined(JZ_MMC_DEBUG)
		device_printf(sc->sc_dev, "controller error, rint %#x stat %#x\n",
		    rint,  JZ_MMC_READ_4(sc, JZ_MSC_STAT));
#endif
		if (rint & (JZ_INT_TIMEOUT_RES | JZ_INT_TIMEOUT_READ))
			sc->sc_req->cmd->error = MMC_ERR_TIMEOUT;
		else
			sc->sc_req->cmd->error = MMC_ERR_FAILED;
		jz4780_mmc_req_done(sc);
		goto end;
	}
	data = sc->sc_req->cmd->data;
	/* Check for command response */
	if (rint & JZ_INT_END_CMD_RES) {
		jz4780_mmc_read_response(sc);
		if (sc->sc_dma_inuse == 1)
			jz4780_mmc_start_dma(sc);
	}
	if (data != NULL) {
		if (sc->sc_dma_inuse == 1 && (rint & JZ_INT_DMAEND))
			sc->sc_resid = data->len >> 2;
		else if (sc->sc_dma_inuse == 0 &&
		    (rint & (JZ_INT_TXFIFO_WR_REQ | JZ_INT_RXFIFO_RD_REQ)))
			jz4780_mmc_pio_transfer(sc, data);
	}
	sc->sc_intr_seen |= rint;
	if ((sc->sc_intr_seen & sc->sc_intr_wait) == sc->sc_intr_wait)
		jz4780_mmc_req_ok(sc);
end:
	JZ_MMC_WRITE_4(sc, JZ_MSC_IFLG, rint);
	JZ_MMC_UNLOCK(sc);
}

static int
jz4780_mmc_request(device_t bus, device_t child, struct mmc_request *req)
{
	struct jz4780_mmc_softc *sc;
	struct mmc_command *cmd;
	uint32_t cmdat, iwait;
	int blksz;

	sc = device_get_softc(bus);
	JZ_MMC_LOCK(sc);
	if (sc->sc_req != NULL) {
		JZ_MMC_UNLOCK(sc);
		return (EBUSY);
	}
	/* Start with template value */
	cmdat = sc->sc_cmdat;
	iwait = JZ_INT_END_CMD_RES;

	/* Configure response format */
	cmd = req->cmd;
	switch (MMC_RSP(cmd->flags)) {
	case MMC_RSP_R1:
	case MMC_RSP_R1B:
		cmdat |= JZ_RES_R1;
		break;
	case MMC_RSP_R2:
		cmdat |= JZ_RES_R2;
		break;
	case MMC_RSP_R3:
		cmdat |= JZ_RES_R3;
		break;
	};
	if (cmd->opcode == MMC_GO_IDLE_STATE)
		cmdat |= JZ_INIT;
	if (cmd->flags & MMC_RSP_BUSY) {
		cmdat |= JZ_BUSY;
		iwait |= JZ_INT_PRG_DONE;
	}

	sc->sc_req = req;
	sc->sc_resid = 0;
	cmd->error = MMC_ERR_NONE;

	if (cmd->data != NULL) {
		cmdat |= JZ_DATA_EN;
		if (cmd->data->flags & MMC_DATA_MULTI) {
			cmdat |= JZ_AUTO_CMD12;
			iwait |= JZ_INT_AUTO_CMD12_DONE;
		}
		if (cmd->data->flags & MMC_DATA_WRITE) {
			cmdat |= JZ_WRITE;
			iwait |= JZ_INT_PRG_DONE;
		}
		if (cmd->data->flags & MMC_DATA_STREAM)
			cmdat |= JZ_STREAM;
		else
			iwait |= JZ_INT_DATA_TRAN_DONE;

		blksz = min(cmd->data->len, MMC_SECTOR_SIZE);
		JZ_MMC_WRITE_4(sc, JZ_MSC_BLKLEN, blksz);
		JZ_MMC_WRITE_4(sc, JZ_MSC_NOB, cmd->data->len / blksz);

		/* Attempt to setup DMA for this transaction */
		if (jz4780_mmc_pio_mode == 0)
			jz4780_mmc_prepare_dma(sc);
		if (sc->sc_dma_inuse != 0) {
			/* Wait for DMA completion interrupt */
			iwait |= JZ_INT_DMAEND;
		} else {
			iwait |= (cmd->data->flags & MMC_DATA_WRITE) ?
			    JZ_INT_TXFIFO_WR_REQ : JZ_INT_RXFIFO_RD_REQ;
			JZ_MMC_WRITE_4(sc, JZ_MSC_DMAC, 0);
		}
	}

	sc->sc_intr_seen = 0;
	sc->sc_intr_wait = iwait;
	JZ_MMC_WRITE_4(sc, JZ_MSC_IMASK, ~(sc->sc_intr_mask | iwait));

#if defined(JZ_MMC_DEBUG)
	device_printf(sc->sc_dev,
	    "REQUEST: CMD%u arg %#x flags %#x cmdat %#x sc_intr_wait = %#x\n",
	    cmd->opcode, cmd->arg, cmd->flags, cmdat, sc->sc_intr_wait);
#endif

	JZ_MMC_WRITE_4(sc, JZ_MSC_ARG, cmd->arg);
	JZ_MMC_WRITE_4(sc, JZ_MSC_CMD, cmd->opcode);
	JZ_MMC_WRITE_4(sc, JZ_MSC_CMDAT, cmdat);

	JZ_MMC_WRITE_4(sc, JZ_MSC_CTRL, JZ_START_OP | JZ_CLOCK_START);

	callout_reset(&sc->sc_timeoutc, sc->sc_timeout * hz,
	    jz4780_mmc_timeout, sc);
	JZ_MMC_UNLOCK(sc);

	return (0);
}

static int
jz4780_mmc_read_ivar(device_t bus, device_t child, int which,
    uintptr_t *result)
{
	struct jz4780_mmc_softc *sc;

	sc = device_get_softc(bus);
	switch (which) {
	default:
		return (EINVAL);
	case MMCBR_IVAR_BUS_MODE:
		*(int *)result = sc->sc_host.ios.bus_mode;
		break;
	case MMCBR_IVAR_BUS_WIDTH:
		*(int *)result = sc->sc_host.ios.bus_width;
		break;
	case MMCBR_IVAR_CHIP_SELECT:
		*(int *)result = sc->sc_host.ios.chip_select;
		break;
	case MMCBR_IVAR_CLOCK:
		*(int *)result = sc->sc_host.ios.clock;
		break;
	case MMCBR_IVAR_F_MIN:
		*(int *)result = sc->sc_host.f_min;
		break;
	case MMCBR_IVAR_F_MAX:
		*(int *)result = sc->sc_host.f_max;
		break;
	case MMCBR_IVAR_HOST_OCR:
		*(int *)result = sc->sc_host.host_ocr;
		break;
	case MMCBR_IVAR_MODE:
		*(int *)result = sc->sc_host.mode;
		break;
	case MMCBR_IVAR_OCR:
		*(int *)result = sc->sc_host.ocr;
		break;
	case MMCBR_IVAR_POWER_MODE:
		*(int *)result = sc->sc_host.ios.power_mode;
		break;
	case MMCBR_IVAR_VDD:
		*(int *)result = sc->sc_host.ios.vdd;
		break;
	case MMCBR_IVAR_CAPS:
		*(int *)result = sc->sc_host.caps;
		break;
	case MMCBR_IVAR_MAX_DATA:
		*(int *)result = 65535;
		break;
	case MMCBR_IVAR_TIMING:
		*(int *)result = sc->sc_host.ios.timing;
		break;
	}

	return (0);
}

static int
jz4780_mmc_write_ivar(device_t bus, device_t child, int which,
    uintptr_t value)
{
	struct jz4780_mmc_softc *sc;

	sc = device_get_softc(bus);
	switch (which) {
	default:
		return (EINVAL);
	case MMCBR_IVAR_BUS_MODE:
		sc->sc_host.ios.bus_mode = value;
		break;
	case MMCBR_IVAR_BUS_WIDTH:
		sc->sc_host.ios.bus_width = value;
		break;
	case MMCBR_IVAR_CHIP_SELECT:
		sc->sc_host.ios.chip_select = value;
		break;
	case MMCBR_IVAR_CLOCK:
		sc->sc_host.ios.clock = value;
		break;
	case MMCBR_IVAR_MODE:
		sc->sc_host.mode = value;
		break;
	case MMCBR_IVAR_OCR:
		sc->sc_host.ocr = value;
		break;
	case MMCBR_IVAR_POWER_MODE:
		sc->sc_host.ios.power_mode = value;
		break;
	case MMCBR_IVAR_VDD:
		sc->sc_host.ios.vdd = value;
		break;
	case MMCBR_IVAR_TIMING:
		sc->sc_host.ios.timing = value;
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

static int
jz4780_mmc_disable_clock(struct jz4780_mmc_softc *sc)
{
	int timeout;

	JZ_MMC_WRITE_4(sc, JZ_MSC_CTRL, JZ_CLOCK_STOP);

	for (timeout = 1000; timeout > 0; timeout--)
		if ((JZ_MMC_READ_4(sc, JZ_MSC_STAT) & JZ_CLK_EN) == 0)
			return (0);
	return (ETIMEDOUT);
}

static int
jz4780_mmc_config_clock(struct jz4780_mmc_softc *sc, uint32_t freq)
{
	uint64_t rate;
	uint32_t clk_freq;
	int err, div;

	err = jz4780_mmc_disable_clock(sc);
	if (err != 0)
		return (err);

	clk_get_freq(sc->sc_clk, &rate);
	clk_freq = (uint32_t)rate;

	div = 0;
	while (clk_freq > freq) {
		div++;
		clk_freq >>= 1;
	}
	if (div >= 7)
		div = 7;
#if defined(JZ_MMC_DEBUG)
	if (div != JZ_MMC_READ_4(sc, JZ_MSC_CLKRT))
		device_printf(sc->sc_dev,
		    "UPDATE_IOS: clk -> %u\n", clk_freq);
#endif
	JZ_MMC_WRITE_4(sc, JZ_MSC_CLKRT, div);
	return (0);
}

static int
jz4780_mmc_update_ios(device_t bus, device_t child)
{
	struct jz4780_mmc_softc *sc;
	struct mmc_ios *ios;
	int error;

	sc = device_get_softc(bus);
	ios = &sc->sc_host.ios;
	if (ios->clock) {
		/* Set the MMC clock. */
		error = jz4780_mmc_config_clock(sc, ios->clock);
		if (error != 0)
			return (error);
	}

	/* Set the bus width. */
	switch (ios->bus_width) {
	case bus_width_1:
		sc->sc_cmdat &= ~(JZ_BUS_WIDTH_M);
		sc->sc_cmdat |= JZ_BUS_1BIT;
		break;
	case bus_width_4:
		sc->sc_cmdat &= ~(JZ_BUS_WIDTH_M);
		sc->sc_cmdat |= JZ_BUS_4BIT;
		break;
	case bus_width_8:
		sc->sc_cmdat &= ~(JZ_BUS_WIDTH_M);
		sc->sc_cmdat |= JZ_BUS_8BIT;
		break;
	}
	return (0);
}

static int
jz4780_mmc_get_ro(device_t bus, device_t child)
{

	return (0);
}

static int
jz4780_mmc_acquire_host(device_t bus, device_t child)
{
	struct jz4780_mmc_softc *sc;
	int error;

	sc = device_get_softc(bus);
	JZ_MMC_LOCK(sc);
	while (sc->sc_bus_busy) {
		error = msleep(sc, &sc->sc_mtx, PCATCH, "mmchw", 0);
		if (error != 0) {
			JZ_MMC_UNLOCK(sc);
			return (error);
		}
	}
	sc->sc_bus_busy++;
	JZ_MMC_UNLOCK(sc);

	return (0);
}

static int
jz4780_mmc_release_host(device_t bus, device_t child)
{
	struct jz4780_mmc_softc *sc;

	sc = device_get_softc(bus);
	JZ_MMC_LOCK(sc);
	sc->sc_bus_busy--;
	wakeup(sc);
	JZ_MMC_UNLOCK(sc);

	return (0);
}

static device_method_t jz4780_mmc_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		jz4780_mmc_probe),
	DEVMETHOD(device_attach,	jz4780_mmc_attach),
	DEVMETHOD(device_detach,	jz4780_mmc_detach),

	/* Bus interface */
	DEVMETHOD(bus_read_ivar,	jz4780_mmc_read_ivar),
	DEVMETHOD(bus_write_ivar,	jz4780_mmc_write_ivar),

	/* MMC bridge interface */
	DEVMETHOD(mmcbr_update_ios,	jz4780_mmc_update_ios),
	DEVMETHOD(mmcbr_request,	jz4780_mmc_request),
	DEVMETHOD(mmcbr_get_ro,		jz4780_mmc_get_ro),
	DEVMETHOD(mmcbr_acquire_host,	jz4780_mmc_acquire_host),
	DEVMETHOD(mmcbr_release_host,	jz4780_mmc_release_host),

	DEVMETHOD_END
};

static devclass_t jz4780_mmc_devclass;

static driver_t jz4780_mmc_driver = {
	"jzmmc",
	jz4780_mmc_methods,
	sizeof(struct jz4780_mmc_softc),
};

DRIVER_MODULE(jzmmc, simplebus, jz4780_mmc_driver, jz4780_mmc_devclass, NULL,
    NULL);
MMC_DECLARE_BRIDGE(jzmmc);
