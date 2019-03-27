/*-
 * Copyright (c) 2013 Ilya Bakulin.  All rights reserved.
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
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/sysctl.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_debug.h>
#include <cam/cam_sim.h>
#include <cam/cam_xpt_sim.h>
#include <cam/scsi/scsi_all.h>

static int is_sdio_mode = 1;

struct mmcnull_softc {
	device_t dev;
	struct mtx sc_mtx;

	struct cam_devq		*devq;
	struct cam_sim		*sim;
	struct cam_path		*path;

	struct callout		 tick;
	union ccb		*cur_ccb;
};

static void mmcnull_identify(driver_t *, device_t);
static int  mmcnull_probe(device_t);
static int  mmcnull_attach(device_t);
static int  mmcnull_detach(device_t);
static void mmcnull_action_sd(struct cam_sim *, union ccb *);
static void mmcnull_action_sdio(struct cam_sim *, union ccb *);
static void mmcnull_intr_sd(void *xsc);
static void mmcnull_intr_sdio(void *xsc);
static void mmcnull_poll(struct cam_sim *);

static void
mmcnull_identify(driver_t *driver, device_t parent)
{
	device_t child;

	if (resource_disabled("mmcnull", 0))
		return;

	if (device_get_unit(parent) != 0)
		return;

	/* Avoid duplicates. */
	if (device_find_child(parent, "mmcnull", -1))
		return;

	child = BUS_ADD_CHILD(parent, 20, "mmcnull", 0);
	if (child == NULL) {
		device_printf(parent, "add MMCNULL child failed\n");
		return;
	}
}


static int
mmcnull_probe(device_t dev)
{
	device_set_desc(dev, "Emulated MMC controller");
	return (BUS_PROBE_DEFAULT);
}

static int
mmcnull_attach(device_t dev)
{
	struct mmcnull_softc *sc;
	sim_action_func action_func;

	sc = device_get_softc(dev);
	sc->dev = dev;

	mtx_init(&sc->sc_mtx, "mmcnullmtx", NULL, MTX_DEF);

	if ((sc->devq = cam_simq_alloc(1)) == NULL)
		return (ENOMEM);

	if (is_sdio_mode)
		action_func = mmcnull_action_sdio;
	else
		action_func = mmcnull_action_sd;
	sc->sim = cam_sim_alloc(action_func, mmcnull_poll, "mmcnull", sc,
				device_get_unit(dev), &sc->sc_mtx, 1, 1,
				sc->devq);

	if (sc->sim == NULL) {
		cam_simq_free(sc->devq);
		device_printf(dev, "cannot allocate CAM SIM\n");
		return (EINVAL);
	}

	mtx_lock(&sc->sc_mtx);
	if (xpt_bus_register(sc->sim, dev, 0) != 0) {
		device_printf(dev,
			      "cannot register SCSI pass-through bus\n");
		cam_sim_free(sc->sim, FALSE);
		cam_simq_free(sc->devq);
		mtx_unlock(&sc->sc_mtx);
		return (EINVAL);
	}
	mtx_unlock(&sc->sc_mtx);

	callout_init_mtx(&sc->tick, &sc->sc_mtx, 0);	/* Callout to emulate interrupts */

	device_printf(dev, "attached OK\n");

	return (0);
}

static int
mmcnull_detach(device_t dev)
{
	struct mmcnull_softc *sc;

	sc = device_get_softc(dev);

	if (sc == NULL)
		return (EINVAL);

	if (sc->sim != NULL) {
		mtx_lock(&sc->sc_mtx);
		xpt_bus_deregister(cam_sim_path(sc->sim));
		cam_sim_free(sc->sim, FALSE);
		mtx_unlock(&sc->sc_mtx);
	}

	if (sc->devq != NULL)
		cam_simq_free(sc->devq);

	callout_drain(&sc->tick);
	mtx_destroy(&sc->sc_mtx);

	device_printf(dev, "detached OK\n");
	return (0);
}

/*
 * The interrupt handler
 * This implementation calls it via callout(9)
 * with the mutex already taken
 */
static void
mmcnull_intr_sd(void *xsc) {
	struct mmcnull_softc *sc;
	union ccb *ccb;
	struct ccb_mmcio *mmcio;

	sc = (struct mmcnull_softc *) xsc;
	mtx_assert(&sc->sc_mtx, MA_OWNED);

	ccb = sc->cur_ccb;
	mmcio = &ccb->mmcio;
	device_printf(sc->dev, "mmcnull_intr: MMC command = %d\n",
		      mmcio->cmd.opcode);

	switch (mmcio->cmd.opcode) {
	case MMC_GO_IDLE_STATE:
		device_printf(sc->dev, "Reset device\n");
		break;
	case SD_SEND_IF_COND:
		mmcio->cmd.resp[0] = 0x1AA; // To match mmc_xpt expectations :-)
		break;
	case MMC_APP_CMD:
		mmcio->cmd.resp[0] = R1_APP_CMD;
		break;
	case SD_SEND_RELATIVE_ADDR:
	case MMC_SELECT_CARD:
		mmcio->cmd.resp[0] = 0x1 << 16;
		break;
	case ACMD_SD_SEND_OP_COND:
		mmcio->cmd.resp[0] = 0xc0ff8000;
		mmcio->cmd.resp[0] |= MMC_OCR_CARD_BUSY;
		break;
	case MMC_ALL_SEND_CID:
		/* Note: this is a real CID from Wandboard int mmc */
		mmcio->cmd.resp[0] = 0x1b534d30;
		mmcio->cmd.resp[1] = 0x30303030;
		mmcio->cmd.resp[2] = 0x10842806;
		mmcio->cmd.resp[3] = 0x5700e900;
		break;
	case MMC_SEND_CSD:
		/* Note: this is a real CSD from Wandboard int mmc */
		mmcio->cmd.resp[0] = 0x400e0032;
		mmcio->cmd.resp[1] = 0x5b590000;
		mmcio->cmd.resp[2] = 0x751f7f80;
		mmcio->cmd.resp[3] = 0x0a404000;
		break;
	case MMC_READ_SINGLE_BLOCK:
	case MMC_READ_MULTIPLE_BLOCK:
		strcpy(mmcio->cmd.data->data, "WTF?!");
		break;
	default:
		device_printf(sc->dev, "mmcnull_intr_sd: unknown command\n");
		mmcio->cmd.error = 1;
	}
	ccb->ccb_h.status = CAM_REQ_CMP;

	sc->cur_ccb = NULL;
	xpt_done(ccb);
}

static void
mmcnull_intr_sdio_newintr(void *xsc) {
	struct mmcnull_softc *sc;
	struct cam_path *dpath;

	sc = (struct mmcnull_softc *) xsc;
	mtx_assert(&sc->sc_mtx, MA_OWNED);
	device_printf(sc->dev, "mmcnull_intr_sdio_newintr()\n");

	/* Our path */
	if (xpt_create_path(&dpath, NULL, cam_sim_path(sc->sim), 0, 0) != CAM_REQ_CMP) {
		device_printf(sc->dev, "mmcnull_intr_sdio_newintr(): cannot create path\n");
		return;
	}
	xpt_async(AC_UNIT_ATTENTION, dpath, NULL);
	xpt_free_path(dpath);
}

static void
mmcnull_intr_sdio(void *xsc) {
	struct mmcnull_softc *sc;
	union ccb *ccb;
	struct ccb_mmcio *mmcio;

	sc = (struct mmcnull_softc *) xsc;
	mtx_assert(&sc->sc_mtx, MA_OWNED);

	ccb = sc->cur_ccb;
	mmcio = &ccb->mmcio;
	device_printf(sc->dev, "mmcnull_intr: MMC command = %d\n",
		      mmcio->cmd.opcode);

	switch (mmcio->cmd.opcode) {
	case MMC_GO_IDLE_STATE:
		device_printf(sc->dev, "Reset device\n");
		break;
	case SD_SEND_IF_COND:
		mmcio->cmd.resp[0] = 0x1AA; // To match mmc_xpt expectations :-)
		break;
	case MMC_APP_CMD:
		mmcio->cmd.resp[0] = R1_APP_CMD;
		break;
	case IO_SEND_OP_COND:
		mmcio->cmd.resp[0] = 0x12345678;
		mmcio->cmd.resp[0] |= ~ R4_IO_MEM_PRESENT;
		break;
	case SD_SEND_RELATIVE_ADDR:
	case MMC_SELECT_CARD:
		mmcio->cmd.resp[0] = 0x1 << 16;
		break;
	case ACMD_SD_SEND_OP_COND:
		/* TODO: steal valid OCR from somewhere :-) */
		mmcio->cmd.resp[0] = 0x123;
		mmcio->cmd.resp[0] |= MMC_OCR_CARD_BUSY;
		break;
	case MMC_ALL_SEND_CID:
		mmcio->cmd.resp[0] = 0x1234;
		mmcio->cmd.resp[1] = 0x5678;
		mmcio->cmd.resp[2] = 0x9ABC;
		mmcio->cmd.resp[3] = 0xDEF0;
		break;
	case MMC_READ_SINGLE_BLOCK:
	case MMC_READ_MULTIPLE_BLOCK:
		strcpy(mmcio->cmd.data->data, "WTF?!");
		break;
	case SD_IO_RW_DIRECT:
		device_printf(sc->dev, "Scheduling interrupt generation...\n");
		callout_reset(&sc->tick, hz / 10, mmcnull_intr_sdio_newintr, sc);
		break;
	default:
		device_printf(sc->dev, "mmcnull_intr_sdio: unknown command\n");
	}
	ccb->ccb_h.status = CAM_REQ_CMP;

	sc->cur_ccb = NULL;
	xpt_done(ccb);
}

/*
 * This is a MMC IO handler
 * It extracts MMC command from CCB and sends it
 * to the h/w
 */
static void
mmcnull_handle_mmcio(struct cam_sim *sim, union ccb *ccb)
{
	struct mmcnull_softc *sc;
	struct ccb_mmcio *mmcio;

	sc = cam_sim_softc(sim);
	mmcio = &ccb->mmcio;
	ccb->ccb_h.status = CAM_REQ_INPROG;
	sc->cur_ccb = ccb;

	/* Real h/w will wait for the interrupt */
	if (is_sdio_mode)
		callout_reset(&sc->tick, hz / 10, mmcnull_intr_sdio, sc);
	else
		callout_reset(&sc->tick, hz / 10, mmcnull_intr_sd, sc);
}

static void
mmcnull_action_sd(struct cam_sim *sim, union ccb *ccb)
{
	struct mmcnull_softc *sc;

	sc = cam_sim_softc(sim);
	if (sc == NULL) {
		ccb->ccb_h.status = CAM_SEL_TIMEOUT;
		xpt_done(ccb);
		return;
	}

	mtx_assert(&sc->sc_mtx, MA_OWNED);

	device_printf(sc->dev, "action: func_code %0x\n", ccb->ccb_h.func_code);

	switch (ccb->ccb_h.func_code) {
	case XPT_PATH_INQ:
	{
		struct ccb_pathinq *cpi;

		cpi = &ccb->cpi;
		cpi->version_num = 1;
		cpi->hba_inquiry = PI_SDTR_ABLE | PI_TAG_ABLE | PI_WIDE_16;
		cpi->target_sprt = 0;
		cpi->hba_misc = PIM_NOBUSRESET | PIM_SEQSCAN;
		cpi->hba_eng_cnt = 0;
		cpi->max_target = 0;
		cpi->max_lun = 0;
		cpi->initiator_id = 1;
		strncpy(cpi->sim_vid, "FreeBSD", SIM_IDLEN);
		strncpy(cpi->hba_vid, "FreeBSD Foundation", HBA_IDLEN);
		strncpy(cpi->dev_name, cam_sim_name(sim), DEV_IDLEN);
		cpi->unit_number = cam_sim_unit(sim);
		cpi->bus_id = cam_sim_bus(sim);
		cpi->base_transfer_speed = 100; /* XXX WTF? */
		cpi->protocol = PROTO_MMCSD;
		cpi->protocol_version = SCSI_REV_0;
		cpi->transport = XPORT_MMCSD;
		cpi->transport_version = 0;

		cpi->ccb_h.status = CAM_REQ_CMP;
		break;
	}
	case XPT_GET_TRAN_SETTINGS:
	{
		struct ccb_trans_settings *cts = &ccb->cts;
		struct ccb_trans_settings_mmc *mcts;
		mcts = &ccb->cts.proto_specific.mmc;

                device_printf(sc->dev, "Got XPT_GET_TRAN_SETTINGS\n");

                cts->protocol = PROTO_MMCSD;
                cts->protocol_version = 0;
                cts->transport = XPORT_MMCSD;
                cts->transport_version = 0;
                cts->xport_specific.valid = 0;
		mcts->host_f_max = 12000000;
		mcts->host_f_min = 200000;
		mcts->host_ocr = 1; /* Fix this */
                ccb->ccb_h.status = CAM_REQ_CMP;
                break;
        }
	case XPT_SET_TRAN_SETTINGS:
		device_printf(sc->dev, "Got XPT_SET_TRAN_SETTINGS, should update IOS...\n");
		ccb->ccb_h.status = CAM_REQ_CMP;
		break;
	case XPT_RESET_BUS:
		device_printf(sc->dev, "Got XPT_RESET_BUS, ACK it...\n");
		ccb->ccb_h.status = CAM_REQ_CMP;
                break;
	case XPT_MMC_IO:
		/*
                 * Here is the HW-dependent part of
		 * sending the command to the underlying h/w
		 * At some point in the future an interrupt comes.
		 * Then the request will be marked as completed.
		 */
		device_printf(sc->dev, "Got XPT_MMC_IO\n");
		mmcnull_handle_mmcio(sim, ccb);
		return;
		break;
        case XPT_RESET_DEV:
                /* This is sent by `camcontrol reset`*/
                device_printf(sc->dev, "Got XPT_RESET_DEV\n");
		ccb->ccb_h.status = CAM_REQ_CMP;
                break;
	default:
		device_printf(sc->dev, "Func code %d is unknown\n", ccb->ccb_h.func_code);
		ccb->ccb_h.status = CAM_REQ_INVALID;
		break;
	}
	xpt_done(ccb);
	return;
}

static void
mmcnull_action_sdio(struct cam_sim *sim, union ccb *ccb) {
	mmcnull_action_sd(sim, ccb);
}

static void
mmcnull_poll(struct cam_sim *sim)
{
	return;
}


static device_method_t mmcnull_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,      mmcnull_identify),
	DEVMETHOD(device_probe,         mmcnull_probe),
	DEVMETHOD(device_attach,        mmcnull_attach),
	DEVMETHOD(device_detach,        mmcnull_detach),
	DEVMETHOD_END
};

static driver_t mmcnull_driver = {
	"mmcnull", mmcnull_methods, sizeof(struct mmcnull_softc)
};

static devclass_t mmcnull_devclass;

DRIVER_MODULE(mmcnull, isa, mmcnull_driver, mmcnull_devclass, 0, 0);
