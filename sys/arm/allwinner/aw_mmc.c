/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018 Emmanuel Vadot <manu@FreeBSD.org>
 * Copyright (c) 2013 Alexander Fedorov
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

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/mmc/bridge.h>
#include <dev/mmc/mmcbrvar.h>

#include <arm/allwinner/aw_mmc.h>
#include <dev/extres/clk/clk.h>
#include <dev/extres/hwreset/hwreset.h>
#include <dev/extres/regulator/regulator.h>

#include "opt_mmccam.h"

#ifdef MMCCAM
#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_debug.h>
#include <cam/cam_sim.h>
#include <cam/cam_xpt_sim.h>
#endif

#define	AW_MMC_MEMRES		0
#define	AW_MMC_IRQRES		1
#define	AW_MMC_RESSZ		2
#define	AW_MMC_DMA_SEGS		(PAGE_SIZE / sizeof(struct aw_mmc_dma_desc))
#define	AW_MMC_DMA_DESC_SIZE	(sizeof(struct aw_mmc_dma_desc) * AW_MMC_DMA_SEGS)
#define	AW_MMC_DMA_FTRGLEVEL	0x20070008

#define	AW_MMC_RESET_RETRY	1000

#define	CARD_ID_FREQUENCY	400000

struct aw_mmc_conf {
	uint32_t	dma_xferlen;
	bool		mask_data0;
	bool		can_calibrate;
	bool		new_timing;
};

static const struct aw_mmc_conf a10_mmc_conf = {
	.dma_xferlen = 0x2000,
};

static const struct aw_mmc_conf a13_mmc_conf = {
	.dma_xferlen = 0x10000,
};

static const struct aw_mmc_conf a64_mmc_conf = {
	.dma_xferlen = 0x10000,
	.mask_data0 = true,
	.can_calibrate = true,
	.new_timing = true,
};

static const struct aw_mmc_conf a64_emmc_conf = {
	.dma_xferlen = 0x2000,
	.can_calibrate = true,
};

static struct ofw_compat_data compat_data[] = {
	{"allwinner,sun4i-a10-mmc", (uintptr_t)&a10_mmc_conf},
	{"allwinner,sun5i-a13-mmc", (uintptr_t)&a13_mmc_conf},
	{"allwinner,sun7i-a20-mmc", (uintptr_t)&a13_mmc_conf},
	{"allwinner,sun50i-a64-mmc", (uintptr_t)&a64_mmc_conf},
	{"allwinner,sun50i-a64-emmc", (uintptr_t)&a64_emmc_conf},
	{NULL,             0}
};

struct aw_mmc_softc {
	device_t		aw_dev;
	clk_t			aw_clk_ahb;
	clk_t			aw_clk_mmc;
	hwreset_t		aw_rst_ahb;
	int			aw_bus_busy;
	int			aw_resid;
	int			aw_timeout;
	struct callout		aw_timeoutc;
	struct mmc_host		aw_host;
#ifdef MMCCAM
	union ccb *		ccb;
	struct cam_devq *	devq;
	struct cam_sim * 	sim;
	struct mtx		sim_mtx;
#else
	struct mmc_request *	aw_req;
#endif
	struct mtx		aw_mtx;
	struct resource *	aw_res[AW_MMC_RESSZ];
	struct aw_mmc_conf *	aw_mmc_conf;
	uint32_t		aw_intr;
	uint32_t		aw_intr_wait;
	void *			aw_intrhand;
	regulator_t		aw_reg_vmmc;
	regulator_t		aw_reg_vqmmc;
	unsigned int		aw_clock;

	/* Fields required for DMA access. */
	bus_addr_t	  	aw_dma_desc_phys;
	bus_dmamap_t		aw_dma_map;
	bus_dma_tag_t 		aw_dma_tag;
	void * 			aw_dma_desc;
	bus_dmamap_t		aw_dma_buf_map;
	bus_dma_tag_t		aw_dma_buf_tag;
	int			aw_dma_map_err;
};

static struct resource_spec aw_mmc_res_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,	RF_ACTIVE | RF_SHAREABLE },
	{ -1,			0,	0 }
};

static int aw_mmc_probe(device_t);
static int aw_mmc_attach(device_t);
static int aw_mmc_detach(device_t);
static int aw_mmc_setup_dma(struct aw_mmc_softc *);
static int aw_mmc_reset(struct aw_mmc_softc *);
static int aw_mmc_init(struct aw_mmc_softc *);
static void aw_mmc_intr(void *);
static int aw_mmc_update_clock(struct aw_mmc_softc *, uint32_t);

static void aw_mmc_print_error(uint32_t);
static int aw_mmc_update_ios(device_t, device_t);
static int aw_mmc_request(device_t, device_t, struct mmc_request *);
static int aw_mmc_get_ro(device_t, device_t);
static int aw_mmc_acquire_host(device_t, device_t);
static int aw_mmc_release_host(device_t, device_t);
#ifdef MMCCAM
static void aw_mmc_cam_action(struct cam_sim *, union ccb *);
static void aw_mmc_cam_poll(struct cam_sim *);
static int aw_mmc_cam_settran_settings(struct aw_mmc_softc *, union ccb *);
static int aw_mmc_cam_request(struct aw_mmc_softc *, union ccb *);
static void aw_mmc_cam_handle_mmcio(struct cam_sim *, union ccb *);
#endif

#define	AW_MMC_LOCK(_sc)	mtx_lock(&(_sc)->aw_mtx)
#define	AW_MMC_UNLOCK(_sc)	mtx_unlock(&(_sc)->aw_mtx)
#define	AW_MMC_READ_4(_sc, _reg)					\
	bus_read_4((_sc)->aw_res[AW_MMC_MEMRES], _reg)
#define	AW_MMC_WRITE_4(_sc, _reg, _value)				\
	bus_write_4((_sc)->aw_res[AW_MMC_MEMRES], _reg, _value)

#ifdef MMCCAM
static void
aw_mmc_cam_handle_mmcio(struct cam_sim *sim, union ccb *ccb)
{
	struct aw_mmc_softc *sc;

	sc = cam_sim_softc(sim);

	aw_mmc_cam_request(sc, ccb);
}

static void
aw_mmc_cam_action(struct cam_sim *sim, union ccb *ccb)
{
	struct aw_mmc_softc *sc;

	sc = cam_sim_softc(sim);
	if (sc == NULL) {
		ccb->ccb_h.status = CAM_SEL_TIMEOUT;
		xpt_done(ccb);
		return;
	}

	mtx_assert(&sc->sim_mtx, MA_OWNED);

	switch (ccb->ccb_h.func_code) {
	case XPT_PATH_INQ:
	{
		struct ccb_pathinq *cpi;

		cpi = &ccb->cpi;
		cpi->version_num = 1;
		cpi->hba_inquiry = 0;
		cpi->target_sprt = 0;
		cpi->hba_misc = PIM_NOBUSRESET | PIM_SEQSCAN;
		cpi->hba_eng_cnt = 0;
		cpi->max_target = 0;
		cpi->max_lun = 0;
		cpi->initiator_id = 1;
		cpi->maxio = (sc->aw_mmc_conf->dma_xferlen *
			      AW_MMC_DMA_SEGS) / MMC_SECTOR_SIZE;
		strncpy(cpi->sim_vid, "FreeBSD", SIM_IDLEN);
		strncpy(cpi->hba_vid, "Deglitch Networks", HBA_IDLEN);
		strncpy(cpi->dev_name, cam_sim_name(sim), DEV_IDLEN);
		cpi->unit_number = cam_sim_unit(sim);
		cpi->bus_id = cam_sim_bus(sim);
		cpi->protocol = PROTO_MMCSD;
		cpi->protocol_version = SCSI_REV_0;
		cpi->transport = XPORT_MMCSD;
		cpi->transport_version = 1;

		cpi->ccb_h.status = CAM_REQ_CMP;
		break;
	}
	case XPT_GET_TRAN_SETTINGS:
	{
		struct ccb_trans_settings *cts = &ccb->cts;

		if (bootverbose)
			device_printf(sc->aw_dev, "Got XPT_GET_TRAN_SETTINGS\n");

		cts->protocol = PROTO_MMCSD;
		cts->protocol_version = 1;
		cts->transport = XPORT_MMCSD;
		cts->transport_version = 1;
		cts->xport_specific.valid = 0;
		cts->proto_specific.mmc.host_ocr = sc->aw_host.host_ocr;
		cts->proto_specific.mmc.host_f_min = sc->aw_host.f_min;
		cts->proto_specific.mmc.host_f_max = sc->aw_host.f_max;
		cts->proto_specific.mmc.host_caps = sc->aw_host.caps;
		memcpy(&cts->proto_specific.mmc.ios, &sc->aw_host.ios, sizeof(struct mmc_ios));
		ccb->ccb_h.status = CAM_REQ_CMP;
		break;
	}
	case XPT_SET_TRAN_SETTINGS:
	{
		if (bootverbose)
			device_printf(sc->aw_dev, "Got XPT_SET_TRAN_SETTINGS\n");
		aw_mmc_cam_settran_settings(sc, ccb);
		ccb->ccb_h.status = CAM_REQ_CMP;
		break;
	}
	case XPT_RESET_BUS:
		if (bootverbose)
			device_printf(sc->aw_dev, "Got XPT_RESET_BUS, ACK it...\n");
		ccb->ccb_h.status = CAM_REQ_CMP;
		break;
	case XPT_MMC_IO:
		/*
		 * Here is the HW-dependent part of
		 * sending the command to the underlying h/w
		 * At some point in the future an interrupt comes.
		 * Then the request will be marked as completed.
		 */
		ccb->ccb_h.status = CAM_REQ_INPROG;

		aw_mmc_cam_handle_mmcio(sim, ccb);
		return;
		/* NOTREACHED */
		break;
	default:
		ccb->ccb_h.status = CAM_REQ_INVALID;
		break;
	}
	xpt_done(ccb);
	return;
}

static void
aw_mmc_cam_poll(struct cam_sim *sim)
{
	return;
}

static int
aw_mmc_cam_settran_settings(struct aw_mmc_softc *sc, union ccb *ccb)
{
	struct mmc_ios *ios;
	struct mmc_ios *new_ios;
	struct ccb_trans_settings_mmc *cts;

	ios = &sc->aw_host.ios;

	cts = &ccb->cts.proto_specific.mmc;
	new_ios = &cts->ios;

	/* Update only requested fields */
	if (cts->ios_valid & MMC_CLK) {
		ios->clock = new_ios->clock;
		device_printf(sc->aw_dev, "Clock => %d\n", ios->clock);
	}
	if (cts->ios_valid & MMC_VDD) {
		ios->vdd = new_ios->vdd;
		device_printf(sc->aw_dev, "VDD => %d\n", ios->vdd);
	}
	if (cts->ios_valid & MMC_CS) {
		ios->chip_select = new_ios->chip_select;
		device_printf(sc->aw_dev, "CS => %d\n", ios->chip_select);
	}
	if (cts->ios_valid & MMC_BW) {
		ios->bus_width = new_ios->bus_width;
		device_printf(sc->aw_dev, "Bus width => %d\n", ios->bus_width);
	}
	if (cts->ios_valid & MMC_PM) {
		ios->power_mode = new_ios->power_mode;
		device_printf(sc->aw_dev, "Power mode => %d\n", ios->power_mode);
	}
	if (cts->ios_valid & MMC_BT) {
		ios->timing = new_ios->timing;
		device_printf(sc->aw_dev, "Timing => %d\n", ios->timing);
	}
	if (cts->ios_valid & MMC_BM) {
		ios->bus_mode = new_ios->bus_mode;
		device_printf(sc->aw_dev, "Bus mode => %d\n", ios->bus_mode);
	}

	return (aw_mmc_update_ios(sc->aw_dev, NULL));
}

static int
aw_mmc_cam_request(struct aw_mmc_softc *sc, union ccb *ccb)
{
	struct ccb_mmcio *mmcio;

	mmcio = &ccb->mmcio;

	AW_MMC_LOCK(sc);

#ifdef DEBUG
	if (__predict_false(bootverbose)) {
		device_printf(sc->aw_dev, "CMD%u arg %#x flags %#x dlen %u dflags %#x\n",
			    mmcio->cmd.opcode, mmcio->cmd.arg, mmcio->cmd.flags,
			    mmcio->cmd.data != NULL ? (unsigned int) mmcio->cmd.data->len : 0,
			    mmcio->cmd.data != NULL ? mmcio->cmd.data->flags: 0);
	}
#endif
	if (mmcio->cmd.data != NULL) {
		if (mmcio->cmd.data->len == 0 || mmcio->cmd.data->flags == 0)
			panic("data->len = %d, data->flags = %d -- something is b0rked",
			      (int)mmcio->cmd.data->len, mmcio->cmd.data->flags);
	}
	if (sc->ccb != NULL) {
		device_printf(sc->aw_dev, "Controller still has an active command\n");
		return (EBUSY);
	}
	sc->ccb = ccb;
	/* aw_mmc_request locks again */
	AW_MMC_UNLOCK(sc);
	aw_mmc_request(sc->aw_dev, NULL, NULL);

	return (0);
}
#endif /* MMCCAM */

static int
aw_mmc_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);
	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "Allwinner Integrated MMC/SD controller");

	return (BUS_PROBE_DEFAULT);
}

static int
aw_mmc_attach(device_t dev)
{
	device_t child;
	struct aw_mmc_softc *sc;
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid_list *tree;
	uint32_t bus_width, max_freq;
	phandle_t node;
	int error;

	node = ofw_bus_get_node(dev);
	sc = device_get_softc(dev);
	sc->aw_dev = dev;

	sc->aw_mmc_conf = (struct aw_mmc_conf *)ofw_bus_search_compatible(dev, compat_data)->ocd_data;

#ifndef MMCCAM
	sc->aw_req = NULL;
#endif
	if (bus_alloc_resources(dev, aw_mmc_res_spec, sc->aw_res) != 0) {
		device_printf(dev, "cannot allocate device resources\n");
		return (ENXIO);
	}
	if (bus_setup_intr(dev, sc->aw_res[AW_MMC_IRQRES],
	    INTR_TYPE_MISC | INTR_MPSAFE, NULL, aw_mmc_intr, sc,
	    &sc->aw_intrhand)) {
		bus_release_resources(dev, aw_mmc_res_spec, sc->aw_res);
		device_printf(dev, "cannot setup interrupt handler\n");
		return (ENXIO);
	}
	mtx_init(&sc->aw_mtx, device_get_nameunit(sc->aw_dev), "aw_mmc",
	    MTX_DEF);
	callout_init_mtx(&sc->aw_timeoutc, &sc->aw_mtx, 0);

	/* De-assert reset */
	if (hwreset_get_by_ofw_name(dev, 0, "ahb", &sc->aw_rst_ahb) == 0) {
		error = hwreset_deassert(sc->aw_rst_ahb);
		if (error != 0) {
			device_printf(dev, "cannot de-assert reset\n");
			goto fail;
		}
	}

	/* Activate the module clock. */
	error = clk_get_by_ofw_name(dev, 0, "ahb", &sc->aw_clk_ahb);
	if (error != 0) {
		device_printf(dev, "cannot get ahb clock\n");
		goto fail;
	}
	error = clk_enable(sc->aw_clk_ahb);
	if (error != 0) {
		device_printf(dev, "cannot enable ahb clock\n");
		goto fail;
	}
	error = clk_get_by_ofw_name(dev, 0, "mmc", &sc->aw_clk_mmc);
	if (error != 0) {
		device_printf(dev, "cannot get mmc clock\n");
		goto fail;
	}
	error = clk_set_freq(sc->aw_clk_mmc, CARD_ID_FREQUENCY,
	    CLK_SET_ROUND_DOWN);
	if (error != 0) {
		device_printf(dev, "cannot init mmc clock\n");
		goto fail;
	}
	error = clk_enable(sc->aw_clk_mmc);
	if (error != 0) {
		device_printf(dev, "cannot enable mmc clock\n");
		goto fail;
	}

	sc->aw_timeout = 10;
	ctx = device_get_sysctl_ctx(dev);
	tree = SYSCTL_CHILDREN(device_get_sysctl_tree(dev));
	SYSCTL_ADD_INT(ctx, tree, OID_AUTO, "req_timeout", CTLFLAG_RW,
	    &sc->aw_timeout, 0, "Request timeout in seconds");

	/* Soft Reset controller. */
	if (aw_mmc_reset(sc) != 0) {
		device_printf(dev, "cannot reset the controller\n");
		goto fail;
	}

	if (aw_mmc_setup_dma(sc) != 0) {
		device_printf(sc->aw_dev, "Couldn't setup DMA!\n");
		goto fail;
	}

	if (OF_getencprop(node, "bus-width", &bus_width, sizeof(uint32_t)) <= 0)
		bus_width = 4;

	if (regulator_get_by_ofw_property(dev, 0, "vmmc-supply",
	    &sc->aw_reg_vmmc) == 0) {
		if (bootverbose)
			device_printf(dev, "vmmc-supply regulator found\n");
	}
	if (regulator_get_by_ofw_property(dev, 0, "vqmmc-supply",
	    &sc->aw_reg_vqmmc) == 0 && bootverbose) {
		if (bootverbose)
			device_printf(dev, "vqmmc-supply regulator found\n");
	}

	sc->aw_host.f_min = 400000;

	if (OF_getencprop(node, "max-frequency", &max_freq,
	    sizeof(uint32_t)) <= 0)
		max_freq = 52000000;
	sc->aw_host.f_max = max_freq;

	sc->aw_host.host_ocr = MMC_OCR_320_330 | MMC_OCR_330_340;
	sc->aw_host.caps = MMC_CAP_HSPEED | MMC_CAP_UHS_SDR12 |
			   MMC_CAP_UHS_SDR25 | MMC_CAP_UHS_SDR50 |
			   MMC_CAP_UHS_DDR50 | MMC_CAP_MMC_DDR52;

	sc->aw_host.caps |= MMC_CAP_SIGNALING_330 | MMC_CAP_SIGNALING_180;

	if (bus_width >= 4)
		sc->aw_host.caps |= MMC_CAP_4_BIT_DATA;
	if (bus_width >= 8)
		sc->aw_host.caps |= MMC_CAP_8_BIT_DATA;

#ifdef MMCCAM
	child = NULL; /* Not used by MMCCAM, need to silence compiler warnings */
	sc->ccb = NULL;
	if ((sc->devq = cam_simq_alloc(1)) == NULL) {
		goto fail;
	}

	mtx_init(&sc->sim_mtx, "awmmcsim", NULL, MTX_DEF);
	sc->sim = cam_sim_alloc(aw_mmc_cam_action, aw_mmc_cam_poll,
	    "aw_mmc_sim", sc, device_get_unit(dev),
	    &sc->sim_mtx, 1, 1, sc->devq);

	if (sc->sim == NULL) {
		cam_simq_free(sc->devq);
		device_printf(dev, "cannot allocate CAM SIM\n");
		goto fail;
	}

	mtx_lock(&sc->sim_mtx);
	if (xpt_bus_register(sc->sim, sc->aw_dev, 0) != 0) {
		device_printf(dev, "cannot register SCSI pass-through bus\n");
		cam_sim_free(sc->sim, FALSE);
		cam_simq_free(sc->devq);
		mtx_unlock(&sc->sim_mtx);
		goto fail;
	}

	mtx_unlock(&sc->sim_mtx);
#else /* !MMCCAM */
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
#endif /* MMCCAM */
	return (0);

fail:
	callout_drain(&sc->aw_timeoutc);
	mtx_destroy(&sc->aw_mtx);
	bus_teardown_intr(dev, sc->aw_res[AW_MMC_IRQRES], sc->aw_intrhand);
	bus_release_resources(dev, aw_mmc_res_spec, sc->aw_res);

#ifdef MMCCAM
	if (sc->sim != NULL) {
		mtx_lock(&sc->sim_mtx);
		xpt_bus_deregister(cam_sim_path(sc->sim));
		cam_sim_free(sc->sim, FALSE);
		mtx_unlock(&sc->sim_mtx);
	}

	if (sc->devq != NULL)
		cam_simq_free(sc->devq);
#endif
	return (ENXIO);
}

static int
aw_mmc_detach(device_t dev)
{

	return (EBUSY);
}

static void
aw_dma_desc_cb(void *arg, bus_dma_segment_t *segs, int nsegs, int err)
{
	struct aw_mmc_softc *sc;

	sc = (struct aw_mmc_softc *)arg;
	if (err) {
		sc->aw_dma_map_err = err;
		return;
	}
	sc->aw_dma_desc_phys = segs[0].ds_addr;
}

static int
aw_mmc_setup_dma(struct aw_mmc_softc *sc)
{
	int error;

	/* Allocate the DMA descriptor memory. */
	error = bus_dma_tag_create(
	    bus_get_dma_tag(sc->aw_dev),	/* parent */
	    AW_MMC_DMA_ALIGN, 0,		/* align, boundary */
	    BUS_SPACE_MAXADDR_32BIT,		/* lowaddr */
	    BUS_SPACE_MAXADDR,			/* highaddr */
	    NULL, NULL,				/* filter, filterarg*/
	    AW_MMC_DMA_DESC_SIZE, 1,		/* maxsize, nsegment */
	    AW_MMC_DMA_DESC_SIZE,		/* maxsegsize */
	    0,					/* flags */
	    NULL, NULL,				/* lock, lockarg*/
	    &sc->aw_dma_tag);
	if (error)
		return (error);

	error = bus_dmamem_alloc(sc->aw_dma_tag, &sc->aw_dma_desc,
	    BUS_DMA_COHERENT | BUS_DMA_WAITOK | BUS_DMA_ZERO,
	    &sc->aw_dma_map);
	if (error)
		return (error);

	error = bus_dmamap_load(sc->aw_dma_tag,
	    sc->aw_dma_map,
	    sc->aw_dma_desc, AW_MMC_DMA_DESC_SIZE,
	    aw_dma_desc_cb, sc, 0);
	if (error)
		return (error);
	if (sc->aw_dma_map_err)
		return (sc->aw_dma_map_err);

	/* Create the DMA map for data transfers. */
	error = bus_dma_tag_create(
	    bus_get_dma_tag(sc->aw_dev),	/* parent */
	    AW_MMC_DMA_ALIGN, 0,		/* align, boundary */
	    BUS_SPACE_MAXADDR_32BIT,		/* lowaddr */
	    BUS_SPACE_MAXADDR,			/* highaddr */
	    NULL, NULL,				/* filter, filterarg*/
	    sc->aw_mmc_conf->dma_xferlen *
	    AW_MMC_DMA_SEGS, AW_MMC_DMA_SEGS,	/* maxsize, nsegments */
	    sc->aw_mmc_conf->dma_xferlen,	/* maxsegsize */
	    BUS_DMA_ALLOCNOW,			/* flags */
	    NULL, NULL,				/* lock, lockarg*/
	    &sc->aw_dma_buf_tag);
	if (error)
		return (error);
	error = bus_dmamap_create(sc->aw_dma_buf_tag, 0,
	    &sc->aw_dma_buf_map);
	if (error)
		return (error);

	return (0);
}

static void
aw_dma_cb(void *arg, bus_dma_segment_t *segs, int nsegs, int err)
{
	int i;
	struct aw_mmc_dma_desc *dma_desc;
	struct aw_mmc_softc *sc;

	sc = (struct aw_mmc_softc *)arg;
	sc->aw_dma_map_err = err;

	if (err)
		return;

	dma_desc = sc->aw_dma_desc;
	for (i = 0; i < nsegs; i++) {
		if (segs[i].ds_len == sc->aw_mmc_conf->dma_xferlen)
			dma_desc[i].buf_size = 0;		/* Size of 0 indicate max len */
		else
			dma_desc[i].buf_size = segs[i].ds_len;
		dma_desc[i].buf_addr = segs[i].ds_addr;
		dma_desc[i].config = AW_MMC_DMA_CONFIG_CH |
			AW_MMC_DMA_CONFIG_OWN | AW_MMC_DMA_CONFIG_DIC;

		dma_desc[i].next = sc->aw_dma_desc_phys +
			((i + 1) * sizeof(struct aw_mmc_dma_desc));
	}

	dma_desc[0].config |= AW_MMC_DMA_CONFIG_FD;
	dma_desc[nsegs - 1].config |= AW_MMC_DMA_CONFIG_LD |
		AW_MMC_DMA_CONFIG_ER;
	dma_desc[nsegs - 1].config &= ~AW_MMC_DMA_CONFIG_DIC;
	dma_desc[nsegs - 1].next = 0;
}

static int
aw_mmc_prepare_dma(struct aw_mmc_softc *sc)
{
	bus_dmasync_op_t sync_op;
	int error;
	struct mmc_command *cmd;
	uint32_t val;

#ifdef MMCCAM
	cmd = &sc->ccb->mmcio.cmd;
#else
	cmd = sc->aw_req->cmd;
#endif
	if (cmd->data->len > (sc->aw_mmc_conf->dma_xferlen * AW_MMC_DMA_SEGS))
		return (EFBIG);
	error = bus_dmamap_load(sc->aw_dma_buf_tag, sc->aw_dma_buf_map,
	    cmd->data->data, cmd->data->len, aw_dma_cb, sc, 0);
	if (error)
		return (error);
	if (sc->aw_dma_map_err)
		return (sc->aw_dma_map_err);

	if (cmd->data->flags & MMC_DATA_WRITE)
		sync_op = BUS_DMASYNC_PREWRITE;
	else
		sync_op = BUS_DMASYNC_PREREAD;
	bus_dmamap_sync(sc->aw_dma_buf_tag, sc->aw_dma_buf_map, sync_op);
	bus_dmamap_sync(sc->aw_dma_tag, sc->aw_dma_map, BUS_DMASYNC_PREWRITE);

	/* Enable DMA */
	val = AW_MMC_READ_4(sc, AW_MMC_GCTL);
	val &= ~AW_MMC_GCTL_FIFO_AC_MOD;
	val |= AW_MMC_GCTL_DMA_ENB;
	AW_MMC_WRITE_4(sc, AW_MMC_GCTL, val);

	/* Reset DMA */
	val |= AW_MMC_GCTL_DMA_RST;
	AW_MMC_WRITE_4(sc, AW_MMC_GCTL, val);

	AW_MMC_WRITE_4(sc, AW_MMC_DMAC, AW_MMC_DMAC_IDMAC_SOFT_RST);
	AW_MMC_WRITE_4(sc, AW_MMC_DMAC,
	    AW_MMC_DMAC_IDMAC_IDMA_ON | AW_MMC_DMAC_IDMAC_FIX_BURST);

	/* Enable RX or TX DMA interrupt */
	val = AW_MMC_READ_4(sc, AW_MMC_IDIE);
	if (cmd->data->flags & MMC_DATA_WRITE)
		val |= AW_MMC_IDST_TX_INT;
	else
		val |= AW_MMC_IDST_RX_INT;
	AW_MMC_WRITE_4(sc, AW_MMC_IDIE, val);

	/* Set DMA descritptor list address */
	AW_MMC_WRITE_4(sc, AW_MMC_DLBA, sc->aw_dma_desc_phys);

	/* FIFO trigger level */
	AW_MMC_WRITE_4(sc, AW_MMC_FWLR, AW_MMC_DMA_FTRGLEVEL);

	return (0);
}

static int
aw_mmc_reset(struct aw_mmc_softc *sc)
{
	uint32_t reg;
	int timeout;

	reg = AW_MMC_READ_4(sc, AW_MMC_GCTL);
	reg |= AW_MMC_GCTL_RESET;
	AW_MMC_WRITE_4(sc, AW_MMC_GCTL, reg);
	timeout = AW_MMC_RESET_RETRY;
	while (--timeout > 0) {
		if ((AW_MMC_READ_4(sc, AW_MMC_GCTL) & AW_MMC_GCTL_RESET) == 0)
			break;
		DELAY(100);
	}
	if (timeout == 0)
		return (ETIMEDOUT);

	return (0);
}

static int
aw_mmc_init(struct aw_mmc_softc *sc)
{
	uint32_t reg;
	int ret;

	ret = aw_mmc_reset(sc);
	if (ret != 0)
		return (ret);

	/* Set the timeout. */
	AW_MMC_WRITE_4(sc, AW_MMC_TMOR,
	    AW_MMC_TMOR_DTO_LMT_SHIFT(AW_MMC_TMOR_DTO_LMT_MASK) |
	    AW_MMC_TMOR_RTO_LMT_SHIFT(AW_MMC_TMOR_RTO_LMT_MASK));

	/* Unmask interrupts. */
	AW_MMC_WRITE_4(sc, AW_MMC_IMKR, 0);

	/* Clear pending interrupts. */
	AW_MMC_WRITE_4(sc, AW_MMC_RISR, 0xffffffff);

	/* Debug register, undocumented */
	AW_MMC_WRITE_4(sc, AW_MMC_DBGC, 0xdeb);

	/* Function select register */
	AW_MMC_WRITE_4(sc, AW_MMC_FUNS, 0xceaa0000);

	AW_MMC_WRITE_4(sc, AW_MMC_IDST, 0xffffffff);

	/* Enable interrupts and disable AHB access. */
	reg = AW_MMC_READ_4(sc, AW_MMC_GCTL);
	reg |= AW_MMC_GCTL_INT_ENB;
	reg &= ~AW_MMC_GCTL_FIFO_AC_MOD;
	reg &= ~AW_MMC_GCTL_WAIT_MEM_ACCESS;
	AW_MMC_WRITE_4(sc, AW_MMC_GCTL, reg);

	return (0);
}

static void
aw_mmc_req_done(struct aw_mmc_softc *sc)
{
	struct mmc_command *cmd;
#ifdef MMCCAM
	union ccb *ccb;
#else
	struct mmc_request *req;
#endif
	uint32_t val, mask;
	int retry;

#ifdef MMCCAM
	ccb = sc->ccb;
	cmd = &ccb->mmcio.cmd;
#else
	cmd = sc->aw_req->cmd;
#endif
#ifdef DEBUG
	if (bootverbose) {
		device_printf(sc->aw_dev, "%s: cmd %d err %d\n", __func__, cmd->opcode, cmd->error);
	}
#endif
	if (cmd->error != MMC_ERR_NONE) {
		/* Reset the FIFO and DMA engines. */
		mask = AW_MMC_GCTL_FIFO_RST | AW_MMC_GCTL_DMA_RST;
		val = AW_MMC_READ_4(sc, AW_MMC_GCTL);
		AW_MMC_WRITE_4(sc, AW_MMC_GCTL, val | mask);

		retry = AW_MMC_RESET_RETRY;
		while (--retry > 0) {
			if ((AW_MMC_READ_4(sc, AW_MMC_GCTL) &
			    AW_MMC_GCTL_RESET) == 0)
				break;
			DELAY(100);
		}
		if (retry == 0)
			device_printf(sc->aw_dev,
			    "timeout resetting DMA/FIFO\n");
		aw_mmc_update_clock(sc, 1);
	}

	callout_stop(&sc->aw_timeoutc);
	sc->aw_intr = 0;
	sc->aw_resid = 0;
	sc->aw_dma_map_err = 0;
	sc->aw_intr_wait = 0;
#ifdef MMCCAM
	sc->ccb = NULL;
	ccb->ccb_h.status =
		(ccb->mmcio.cmd.error == 0 ? CAM_REQ_CMP : CAM_REQ_CMP_ERR);
	xpt_done(ccb);
#else
	req = sc->aw_req;
	sc->aw_req = NULL;
	req->done(req);
#endif
}

static void
aw_mmc_req_ok(struct aw_mmc_softc *sc)
{
	int timeout;
	struct mmc_command *cmd;
	uint32_t status;

	timeout = 1000;
	while (--timeout > 0) {
		status = AW_MMC_READ_4(sc, AW_MMC_STAR);
		if ((status & AW_MMC_STAR_CARD_BUSY) == 0)
			break;
		DELAY(1000);
	}
#ifdef MMCCAM
	cmd = &sc->ccb->mmcio.cmd;
#else
	cmd = sc->aw_req->cmd;
#endif
	if (timeout == 0) {
		cmd->error = MMC_ERR_FAILED;
		aw_mmc_req_done(sc);
		return;
	}
	if (cmd->flags & MMC_RSP_PRESENT) {
		if (cmd->flags & MMC_RSP_136) {
			cmd->resp[0] = AW_MMC_READ_4(sc, AW_MMC_RESP3);
			cmd->resp[1] = AW_MMC_READ_4(sc, AW_MMC_RESP2);
			cmd->resp[2] = AW_MMC_READ_4(sc, AW_MMC_RESP1);
			cmd->resp[3] = AW_MMC_READ_4(sc, AW_MMC_RESP0);
		} else
			cmd->resp[0] = AW_MMC_READ_4(sc, AW_MMC_RESP0);
	}
	/* All data has been transferred ? */
	if (cmd->data != NULL && (sc->aw_resid << 2) < cmd->data->len)
		cmd->error = MMC_ERR_FAILED;
	aw_mmc_req_done(sc);
}


static inline void
set_mmc_error(struct aw_mmc_softc *sc, int error_code)
{
#ifdef MMCCAM
	sc->ccb->mmcio.cmd.error = error_code;
#else
	sc->aw_req->cmd->error = error_code;
#endif
}

static void
aw_mmc_timeout(void *arg)
{
	struct aw_mmc_softc *sc;

	sc = (struct aw_mmc_softc *)arg;
#ifdef MMCCAM
	if (sc->ccb != NULL) {
#else
	if (sc->aw_req != NULL) {
#endif
		device_printf(sc->aw_dev, "controller timeout\n");
		set_mmc_error(sc, MMC_ERR_TIMEOUT);
		aw_mmc_req_done(sc);
	} else
		device_printf(sc->aw_dev,
		    "Spurious timeout - no active request\n");
}

static void
aw_mmc_print_error(uint32_t err)
{
	if(err & AW_MMC_INT_RESP_ERR)
		printf("AW_MMC_INT_RESP_ERR ");
	if (err & AW_MMC_INT_RESP_CRC_ERR)
		printf("AW_MMC_INT_RESP_CRC_ERR ");
	if (err & AW_MMC_INT_DATA_CRC_ERR)
		printf("AW_MMC_INT_DATA_CRC_ERR ");
	if (err & AW_MMC_INT_RESP_TIMEOUT)
		printf("AW_MMC_INT_RESP_TIMEOUT ");
	if (err & AW_MMC_INT_FIFO_RUN_ERR)
		printf("AW_MMC_INT_FIFO_RUN_ERR ");
	if (err & AW_MMC_INT_CMD_BUSY)
		printf("AW_MMC_INT_CMD_BUSY ");
	if (err & AW_MMC_INT_DATA_START_ERR)
		printf("AW_MMC_INT_DATA_START_ERR ");
	if (err & AW_MMC_INT_DATA_END_BIT_ERR)
		printf("AW_MMC_INT_DATA_END_BIT_ERR");
	printf("\n");
}

static void
aw_mmc_intr(void *arg)
{
	bus_dmasync_op_t sync_op;
	struct aw_mmc_softc *sc;
	struct mmc_data *data;
	uint32_t idst, imask, rint;

	sc = (struct aw_mmc_softc *)arg;
	AW_MMC_LOCK(sc);
	rint = AW_MMC_READ_4(sc, AW_MMC_RISR);
	idst = AW_MMC_READ_4(sc, AW_MMC_IDST);
	imask = AW_MMC_READ_4(sc, AW_MMC_IMKR);
	if (idst == 0 && imask == 0 && rint == 0) {
		AW_MMC_UNLOCK(sc);
		return;
	}
#ifdef DEBUG
	device_printf(sc->aw_dev, "idst: %#x, imask: %#x, rint: %#x\n",
	    idst, imask, rint);
#endif
#ifdef MMCCAM
	if (sc->ccb == NULL) {
#else
	if (sc->aw_req == NULL) {
#endif
		device_printf(sc->aw_dev,
		    "Spurious interrupt - no active request, rint: 0x%08X\n",
		    rint);
		aw_mmc_print_error(rint);
		goto end;
	}
	if (rint & AW_MMC_INT_ERR_BIT) {
		if (bootverbose)
			device_printf(sc->aw_dev, "error rint: 0x%08X\n", rint);
		aw_mmc_print_error(rint);
		if (rint & AW_MMC_INT_RESP_TIMEOUT)
			set_mmc_error(sc, MMC_ERR_TIMEOUT);
		else
			set_mmc_error(sc, MMC_ERR_FAILED);
		aw_mmc_req_done(sc);
		goto end;
	}
	if (idst & AW_MMC_IDST_ERROR) {
		device_printf(sc->aw_dev, "error idst: 0x%08x\n", idst);
		set_mmc_error(sc, MMC_ERR_FAILED);
		aw_mmc_req_done(sc);
		goto end;
	}

	sc->aw_intr |= rint;
#ifdef MMCCAM
	data = sc->ccb->mmcio.cmd.data;
#else
	data = sc->aw_req->cmd->data;
#endif
	if (data != NULL && (idst & AW_MMC_IDST_COMPLETE) != 0) {
		if (data->flags & MMC_DATA_WRITE)
			sync_op = BUS_DMASYNC_POSTWRITE;
		else
			sync_op = BUS_DMASYNC_POSTREAD;
		bus_dmamap_sync(sc->aw_dma_buf_tag, sc->aw_dma_buf_map,
		    sync_op);
		bus_dmamap_sync(sc->aw_dma_tag, sc->aw_dma_map,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->aw_dma_buf_tag, sc->aw_dma_buf_map);
		sc->aw_resid = data->len >> 2;
	}
	if ((sc->aw_intr & sc->aw_intr_wait) == sc->aw_intr_wait)
		aw_mmc_req_ok(sc);

end:
	AW_MMC_WRITE_4(sc, AW_MMC_IDST, idst);
	AW_MMC_WRITE_4(sc, AW_MMC_RISR, rint);
	AW_MMC_UNLOCK(sc);
}

static int
aw_mmc_request(device_t bus, device_t child, struct mmc_request *req)
{
	int blksz;
	struct aw_mmc_softc *sc;
	struct mmc_command *cmd;
	uint32_t cmdreg, imask;
	int err;

	sc = device_get_softc(bus);

	AW_MMC_LOCK(sc);
#ifdef MMCCAM
	KASSERT(req == NULL, ("req should be NULL in MMCCAM case!"));
	/*
	 * For MMCCAM, sc->ccb has been NULL-checked and populated
	 * by aw_mmc_cam_request() already.
	 */
	cmd = &sc->ccb->mmcio.cmd;
#else
	if (sc->aw_req) {
		AW_MMC_UNLOCK(sc);
		return (EBUSY);
	}
	sc->aw_req = req;
	cmd = req->cmd;

#ifdef DEBUG
	if (bootverbose)
		device_printf(sc->aw_dev, "CMD%u arg %#x flags %#x dlen %u dflags %#x\n",
			      cmd->opcode, cmd->arg, cmd->flags,
			      cmd->data != NULL ? (unsigned int)cmd->data->len : 0,
			      cmd->data != NULL ? cmd->data->flags: 0);
#endif
#endif
	cmdreg = AW_MMC_CMDR_LOAD;
	imask = AW_MMC_INT_ERR_BIT;
	sc->aw_intr_wait = 0;
	sc->aw_intr = 0;
	sc->aw_resid = 0;
	cmd->error = MMC_ERR_NONE;

	if (cmd->opcode == MMC_GO_IDLE_STATE)
		cmdreg |= AW_MMC_CMDR_SEND_INIT_SEQ;

	if (cmd->flags & MMC_RSP_PRESENT)
		cmdreg |= AW_MMC_CMDR_RESP_RCV;
	if (cmd->flags & MMC_RSP_136)
		cmdreg |= AW_MMC_CMDR_LONG_RESP;
	if (cmd->flags & MMC_RSP_CRC)
		cmdreg |= AW_MMC_CMDR_CHK_RESP_CRC;

	if (cmd->data) {
		cmdreg |= AW_MMC_CMDR_DATA_TRANS | AW_MMC_CMDR_WAIT_PRE_OVER;

		if (cmd->data->flags & MMC_DATA_MULTI) {
			cmdreg |= AW_MMC_CMDR_STOP_CMD_FLAG;
			imask |= AW_MMC_INT_AUTO_STOP_DONE;
			sc->aw_intr_wait |= AW_MMC_INT_AUTO_STOP_DONE;
		} else {
			sc->aw_intr_wait |= AW_MMC_INT_DATA_OVER;
			imask |= AW_MMC_INT_DATA_OVER;
		}
		if (cmd->data->flags & MMC_DATA_WRITE)
			cmdreg |= AW_MMC_CMDR_DIR_WRITE;

		blksz = min(cmd->data->len, MMC_SECTOR_SIZE);
		AW_MMC_WRITE_4(sc, AW_MMC_BKSR, blksz);
		AW_MMC_WRITE_4(sc, AW_MMC_BYCR, cmd->data->len);
	} else {
		imask |= AW_MMC_INT_CMD_DONE;
	}

	/* Enable the interrupts we are interested in */
	AW_MMC_WRITE_4(sc, AW_MMC_IMKR, imask);
	AW_MMC_WRITE_4(sc, AW_MMC_RISR, 0xffffffff);

	/* Enable auto stop if needed */
	AW_MMC_WRITE_4(sc, AW_MMC_A12A,
	    cmdreg & AW_MMC_CMDR_STOP_CMD_FLAG ? 0 : 0xffff);

	/* Write the command argument */
	AW_MMC_WRITE_4(sc, AW_MMC_CAGR, cmd->arg);

	/* 
	 * If we don't have data start the request
	 * if we do prepare the dma request and start the request
	 */
	if (cmd->data == NULL) {
		AW_MMC_WRITE_4(sc, AW_MMC_CMDR, cmdreg | cmd->opcode);
	} else {
		err = aw_mmc_prepare_dma(sc);
		if (err != 0)
			device_printf(sc->aw_dev, "prepare_dma failed: %d\n", err);

		AW_MMC_WRITE_4(sc, AW_MMC_CMDR, cmdreg | cmd->opcode);
	}

	callout_reset(&sc->aw_timeoutc, sc->aw_timeout * hz,
	    aw_mmc_timeout, sc);
	AW_MMC_UNLOCK(sc);

	return (0);
}

static int
aw_mmc_read_ivar(device_t bus, device_t child, int which,
    uintptr_t *result)
{
	struct aw_mmc_softc *sc;

	sc = device_get_softc(bus);
	switch (which) {
	default:
		return (EINVAL);
	case MMCBR_IVAR_BUS_MODE:
		*(int *)result = sc->aw_host.ios.bus_mode;
		break;
	case MMCBR_IVAR_BUS_WIDTH:
		*(int *)result = sc->aw_host.ios.bus_width;
		break;
	case MMCBR_IVAR_CHIP_SELECT:
		*(int *)result = sc->aw_host.ios.chip_select;
		break;
	case MMCBR_IVAR_CLOCK:
		*(int *)result = sc->aw_host.ios.clock;
		break;
	case MMCBR_IVAR_F_MIN:
		*(int *)result = sc->aw_host.f_min;
		break;
	case MMCBR_IVAR_F_MAX:
		*(int *)result = sc->aw_host.f_max;
		break;
	case MMCBR_IVAR_HOST_OCR:
		*(int *)result = sc->aw_host.host_ocr;
		break;
	case MMCBR_IVAR_MODE:
		*(int *)result = sc->aw_host.mode;
		break;
	case MMCBR_IVAR_OCR:
		*(int *)result = sc->aw_host.ocr;
		break;
	case MMCBR_IVAR_POWER_MODE:
		*(int *)result = sc->aw_host.ios.power_mode;
		break;
	case MMCBR_IVAR_VDD:
		*(int *)result = sc->aw_host.ios.vdd;
		break;
	case MMCBR_IVAR_VCCQ:
		*(int *)result = sc->aw_host.ios.vccq;
		break;
	case MMCBR_IVAR_CAPS:
		*(int *)result = sc->aw_host.caps;
		break;
	case MMCBR_IVAR_TIMING:
		*(int *)result = sc->aw_host.ios.timing;
		break;
	case MMCBR_IVAR_MAX_DATA:
		*(int *)result = (sc->aw_mmc_conf->dma_xferlen *
		    AW_MMC_DMA_SEGS) / MMC_SECTOR_SIZE;
		break;
	case MMCBR_IVAR_RETUNE_REQ:
		*(int *)result = retune_req_none;
		break;
	}

	return (0);
}

static int
aw_mmc_write_ivar(device_t bus, device_t child, int which,
    uintptr_t value)
{
	struct aw_mmc_softc *sc;

	sc = device_get_softc(bus);
	switch (which) {
	default:
		return (EINVAL);
	case MMCBR_IVAR_BUS_MODE:
		sc->aw_host.ios.bus_mode = value;
		break;
	case MMCBR_IVAR_BUS_WIDTH:
		sc->aw_host.ios.bus_width = value;
		break;
	case MMCBR_IVAR_CHIP_SELECT:
		sc->aw_host.ios.chip_select = value;
		break;
	case MMCBR_IVAR_CLOCK:
		sc->aw_host.ios.clock = value;
		break;
	case MMCBR_IVAR_MODE:
		sc->aw_host.mode = value;
		break;
	case MMCBR_IVAR_OCR:
		sc->aw_host.ocr = value;
		break;
	case MMCBR_IVAR_POWER_MODE:
		sc->aw_host.ios.power_mode = value;
		break;
	case MMCBR_IVAR_VDD:
		sc->aw_host.ios.vdd = value;
		break;
	case MMCBR_IVAR_VCCQ:
		sc->aw_host.ios.vccq = value;
		break;
	case MMCBR_IVAR_TIMING:
		sc->aw_host.ios.timing = value;
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
aw_mmc_update_clock(struct aw_mmc_softc *sc, uint32_t clkon)
{
	uint32_t reg;
	int retry;

	reg = AW_MMC_READ_4(sc, AW_MMC_CKCR);
	reg &= ~(AW_MMC_CKCR_ENB | AW_MMC_CKCR_LOW_POWER |
	    AW_MMC_CKCR_MASK_DATA0);

	if (clkon)
		reg |= AW_MMC_CKCR_ENB;
	if (sc->aw_mmc_conf->mask_data0)
		reg |= AW_MMC_CKCR_MASK_DATA0;

	AW_MMC_WRITE_4(sc, AW_MMC_CKCR, reg);

	reg = AW_MMC_CMDR_LOAD | AW_MMC_CMDR_PRG_CLK |
	    AW_MMC_CMDR_WAIT_PRE_OVER;
	AW_MMC_WRITE_4(sc, AW_MMC_CMDR, reg);
	retry = 0xfffff;

	while (reg & AW_MMC_CMDR_LOAD && --retry > 0) {
		reg = AW_MMC_READ_4(sc, AW_MMC_CMDR);
		DELAY(10);
	}
	AW_MMC_WRITE_4(sc, AW_MMC_RISR, 0xffffffff);

	if (reg & AW_MMC_CMDR_LOAD) {
		device_printf(sc->aw_dev, "timeout updating clock\n");
		return (ETIMEDOUT);
	}

	if (sc->aw_mmc_conf->mask_data0) {
		reg = AW_MMC_READ_4(sc, AW_MMC_CKCR);
		reg &= ~AW_MMC_CKCR_MASK_DATA0;
		AW_MMC_WRITE_4(sc, AW_MMC_CKCR, reg);
	}

	return (0);
}

static int
aw_mmc_switch_vccq(device_t bus, device_t child)
{
	struct aw_mmc_softc *sc;
	int uvolt, err;

	sc = device_get_softc(bus);

	if (sc->aw_reg_vqmmc == NULL)
		return EOPNOTSUPP;

	switch (sc->aw_host.ios.vccq) {
	case vccq_180:
		uvolt = 1800000;
		break;
	case vccq_330:
		uvolt = 3300000;
		break;
	default:
		return EINVAL;
	}

	err = regulator_set_voltage(sc->aw_reg_vqmmc, uvolt, uvolt);
	if (err != 0) {
		device_printf(sc->aw_dev,
		    "Cannot set vqmmc to %d<->%d\n",
		    uvolt,
		    uvolt);
		return (err);
	}

	return (0);
}

static int
aw_mmc_update_ios(device_t bus, device_t child)
{
	int error;
	struct aw_mmc_softc *sc;
	struct mmc_ios *ios;
	unsigned int clock;
	uint32_t reg, div = 1;

	sc = device_get_softc(bus);

	ios = &sc->aw_host.ios;

	/* Set the bus width. */
	switch (ios->bus_width) {
	case bus_width_1:
		AW_MMC_WRITE_4(sc, AW_MMC_BWDR, AW_MMC_BWDR1);
		break;
	case bus_width_4:
		AW_MMC_WRITE_4(sc, AW_MMC_BWDR, AW_MMC_BWDR4);
		break;
	case bus_width_8:
		AW_MMC_WRITE_4(sc, AW_MMC_BWDR, AW_MMC_BWDR8);
		break;
	}

	switch (ios->power_mode) {
	case power_on:
		break;
	case power_off:
		if (bootverbose)
			device_printf(sc->aw_dev, "Powering down sd/mmc\n");

		if (sc->aw_reg_vmmc)
			regulator_disable(sc->aw_reg_vmmc);
		if (sc->aw_reg_vqmmc)
			regulator_disable(sc->aw_reg_vqmmc);

		aw_mmc_reset(sc);
		break;
	case power_up:
		if (bootverbose)
			device_printf(sc->aw_dev, "Powering up sd/mmc\n");

		if (sc->aw_reg_vmmc)
			regulator_enable(sc->aw_reg_vmmc);
		if (sc->aw_reg_vqmmc)
			regulator_enable(sc->aw_reg_vqmmc);
		aw_mmc_init(sc);
		break;
	};

	/* Enable ddr mode if needed */
	reg = AW_MMC_READ_4(sc, AW_MMC_GCTL);
	if (ios->timing == bus_timing_uhs_ddr50 ||
	  ios->timing == bus_timing_mmc_ddr52)
		reg |= AW_MMC_GCTL_DDR_MOD_SEL;
	else
		reg &= ~AW_MMC_GCTL_DDR_MOD_SEL;
	AW_MMC_WRITE_4(sc, AW_MMC_GCTL, reg);

	if (ios->clock && ios->clock != sc->aw_clock) {
		sc->aw_clock = clock = ios->clock;

		/* Disable clock */
		error = aw_mmc_update_clock(sc, 0);
		if (error != 0)
			return (error);

		if (ios->timing == bus_timing_mmc_ddr52 &&
		    (sc->aw_mmc_conf->new_timing ||
		    ios->bus_width == bus_width_8)) {
			div = 2;
			clock <<= 1;
		}

		/* Reset the divider. */
		reg = AW_MMC_READ_4(sc, AW_MMC_CKCR);
		reg &= ~AW_MMC_CKCR_DIV;
		reg |= div - 1;
		AW_MMC_WRITE_4(sc, AW_MMC_CKCR, reg);

		/* New timing mode if needed */
		if (sc->aw_mmc_conf->new_timing) {
			reg = AW_MMC_READ_4(sc, AW_MMC_NTSR);
			reg |= AW_MMC_NTSR_MODE_SELECT;
			AW_MMC_WRITE_4(sc, AW_MMC_NTSR, reg);
		}

		/* Set the MMC clock. */
		error = clk_set_freq(sc->aw_clk_mmc, clock,
		    CLK_SET_ROUND_DOWN);
		if (error != 0) {
			device_printf(sc->aw_dev,
			    "failed to set frequency to %u Hz: %d\n",
			    clock, error);
			return (error);
		}

		if (sc->aw_mmc_conf->can_calibrate)
			AW_MMC_WRITE_4(sc, AW_MMC_SAMP_DL, AW_MMC_SAMP_DL_SW_EN);

		/* Enable clock. */
		error = aw_mmc_update_clock(sc, 1);
		if (error != 0)
			return (error);
	}


	return (0);
}

static int
aw_mmc_get_ro(device_t bus, device_t child)
{

	return (0);
}

static int
aw_mmc_acquire_host(device_t bus, device_t child)
{
	struct aw_mmc_softc *sc;
	int error;

	sc = device_get_softc(bus);
	AW_MMC_LOCK(sc);
	while (sc->aw_bus_busy) {
		error = msleep(sc, &sc->aw_mtx, PCATCH, "mmchw", 0);
		if (error != 0) {
			AW_MMC_UNLOCK(sc);
			return (error);
		}
	}
	sc->aw_bus_busy++;
	AW_MMC_UNLOCK(sc);

	return (0);
}

static int
aw_mmc_release_host(device_t bus, device_t child)
{
	struct aw_mmc_softc *sc;

	sc = device_get_softc(bus);
	AW_MMC_LOCK(sc);
	sc->aw_bus_busy--;
	wakeup(sc);
	AW_MMC_UNLOCK(sc);

	return (0);
}

static device_method_t aw_mmc_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		aw_mmc_probe),
	DEVMETHOD(device_attach,	aw_mmc_attach),
	DEVMETHOD(device_detach,	aw_mmc_detach),

	/* Bus interface */
	DEVMETHOD(bus_read_ivar,	aw_mmc_read_ivar),
	DEVMETHOD(bus_write_ivar,	aw_mmc_write_ivar),

	/* MMC bridge interface */
	DEVMETHOD(mmcbr_update_ios,	aw_mmc_update_ios),
	DEVMETHOD(mmcbr_request,	aw_mmc_request),
	DEVMETHOD(mmcbr_get_ro,		aw_mmc_get_ro),
	DEVMETHOD(mmcbr_switch_vccq,	aw_mmc_switch_vccq),
	DEVMETHOD(mmcbr_acquire_host,	aw_mmc_acquire_host),
	DEVMETHOD(mmcbr_release_host,	aw_mmc_release_host),

	DEVMETHOD_END
};

static devclass_t aw_mmc_devclass;

static driver_t aw_mmc_driver = {
	"aw_mmc",
	aw_mmc_methods,
	sizeof(struct aw_mmc_softc),
};

DRIVER_MODULE(aw_mmc, simplebus, aw_mmc_driver, aw_mmc_devclass, NULL,
    NULL);
#ifndef MMCCAM
MMC_DECLARE_BRIDGE(aw_mmc);
#endif
