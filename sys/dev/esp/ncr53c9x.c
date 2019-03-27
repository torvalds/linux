/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD AND BSD-2-Clause NetBSD
 *
 * Copyright (c) 2004 Scott Long
 * Copyright (c) 2005, 2008 Marius Strobl <marius@FreeBSD.org>
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
 *
 */

/*	$NetBSD: ncr53c9x.c,v 1.145 2012/06/18 21:23:56 martin Exp $	*/

/*-
 * Copyright (c) 1998, 2002 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Charles M. Hannum.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*-
 * Copyright (c) 1994 Peter Galbavy
 * Copyright (c) 1995 Paul Kranenburg
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Peter Galbavy
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Based on aic6360 by Jarle Greipsland
 *
 * Acknowledgements: Many of the algorithms used in this driver are
 * inspired by the work of Julian Elischer (julian@FreeBSD.org) and
 * Charles Hannum (mycroft@duality.gnu.ai.mit.edu).  Thanks a million!
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/queue.h>
#include <sys/time.h>
#include <sys/callout.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_debug.h>
#include <cam/cam_sim.h>
#include <cam/cam_xpt_sim.h>
#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_message.h>

#include <dev/esp/ncr53c9xreg.h>
#include <dev/esp/ncr53c9xvar.h>

devclass_t esp_devclass;

MODULE_DEPEND(esp, cam, 1, 1, 1);

#ifdef NCR53C9X_DEBUG
int ncr53c9x_debug =
    NCR_SHOWMISC /* | NCR_SHOWPHASE | NCR_SHOWTRAC | NCR_SHOWCMDS */;
#endif

static void	ncr53c9x_abort(struct ncr53c9x_softc *sc,
		    struct ncr53c9x_ecb *ecb);
static void	ncr53c9x_action(struct cam_sim *sim, union ccb *ccb);
static void	ncr53c9x_async(void *cbarg, uint32_t code,
		    struct cam_path *path, void *arg);
static void	ncr53c9x_callout(void *arg);
static void	ncr53c9x_clear(struct ncr53c9x_softc *sc, cam_status result);
static void	ncr53c9x_clear_target(struct ncr53c9x_softc *sc, int target,
		    cam_status result);
static void	ncr53c9x_dequeue(struct ncr53c9x_softc *sc,
		    struct ncr53c9x_ecb *ecb);
static void	ncr53c9x_done(struct ncr53c9x_softc *sc,
		    struct ncr53c9x_ecb *ecb);
static void	ncr53c9x_free_ecb(struct ncr53c9x_softc *sc,
		    struct ncr53c9x_ecb *ecb);
static void	ncr53c9x_msgin(struct ncr53c9x_softc *sc);
static void	ncr53c9x_msgout(struct ncr53c9x_softc *sc);
static void	ncr53c9x_init(struct ncr53c9x_softc *sc, int doreset);
static void	ncr53c9x_intr1(struct ncr53c9x_softc *sc);
static void	ncr53c9x_poll(struct cam_sim *sim);
static int	ncr53c9x_rdfifo(struct ncr53c9x_softc *sc, int how);
static int	ncr53c9x_reselect(struct ncr53c9x_softc *sc, int message,
		    int tagtype, int tagid);
static void	ncr53c9x_reset(struct ncr53c9x_softc *sc);
static void	ncr53c9x_sense(struct ncr53c9x_softc *sc,
		    struct ncr53c9x_ecb *ecb);
static void	ncr53c9x_sched(struct ncr53c9x_softc *sc);
static void	ncr53c9x_select(struct ncr53c9x_softc *sc,
		    struct ncr53c9x_ecb *ecb);
static void	ncr53c9x_watch(void *arg);
static void	ncr53c9x_wrfifo(struct ncr53c9x_softc *sc, uint8_t *p,
		    int len);

static struct ncr53c9x_ecb	*ncr53c9x_get_ecb(struct ncr53c9x_softc *sc);
static struct ncr53c9x_linfo	*ncr53c9x_lunsearch(struct ncr53c9x_tinfo *sc,
				    int64_t lun);

static inline void	ncr53c9x_readregs(struct ncr53c9x_softc *sc);
static inline void	ncr53c9x_setsync(struct ncr53c9x_softc *sc,
			    struct ncr53c9x_tinfo *ti);
static inline int	ncr53c9x_stp2cpb(struct ncr53c9x_softc *sc,
			    int period);

#define	NCR_RDFIFO_START   0
#define	NCR_RDFIFO_CONTINUE 1

#define	NCR_SET_COUNT(sc, size) do {					\
		NCR_WRITE_REG((sc), NCR_TCL, (size));			\
		NCR_WRITE_REG((sc), NCR_TCM, (size) >> 8);		\
		if ((sc->sc_features & NCR_F_LARGEXFER) != 0)		\
			NCR_WRITE_REG((sc), NCR_TCH, (size) >> 16);	\
		if (sc->sc_rev == NCR_VARIANT_FAS366)			\
			NCR_WRITE_REG(sc, NCR_RCH, 0);			\
} while (/* CONSTCOND */0)

#ifndef mstohz
#define	mstohz(ms) \
	(((ms) < 0x20000) ? \
	    ((ms +0u) / 1000u) * hz : \
	    ((ms +0u) * hz) /1000u)
#endif

/*
 * Names for the NCR53c9x variants, corresponding to the variant tags
 * in ncr53c9xvar.h.
 */
static const char *ncr53c9x_variant_names[] = {
	"ESP100",
	"ESP100A",
	"ESP200",
	"NCR53C94",
	"NCR53C96",
	"ESP406",
	"FAS408",
	"FAS216",
	"AM53C974",
	"FAS366/HME",
	"NCR53C90 (86C01)",
	"FAS100A",
	"FAS236",
};

/*
 * Search linked list for LUN info by LUN id.
 */
static struct ncr53c9x_linfo *
ncr53c9x_lunsearch(struct ncr53c9x_tinfo *ti, int64_t lun)
{
	struct ncr53c9x_linfo *li;

	LIST_FOREACH(li, &ti->luns, link)
		if (li->lun == lun)
			return (li);
	return (NULL);
}

/*
 * Attach this instance, and then all the sub-devices.
 */
int
ncr53c9x_attach(struct ncr53c9x_softc *sc)
{
	struct cam_devq *devq;
	struct cam_sim *sim;
	struct cam_path *path;
	struct ncr53c9x_ecb *ecb;
	int error, i;

	if (NCR_LOCK_INITIALIZED(sc) == 0) {
		device_printf(sc->sc_dev, "mutex not initialized\n");
		return (ENXIO);
	}

	callout_init_mtx(&sc->sc_watchdog, &sc->sc_lock, 0);

	/*
	 * Note, the front-end has set us up to print the chip variation.
	 */
	if (sc->sc_rev >= NCR_VARIANT_MAX) {
		device_printf(sc->sc_dev, "unknown variant %d, devices not "
		    "attached\n", sc->sc_rev);
		return (EINVAL);
	}

	device_printf(sc->sc_dev, "%s, %d MHz, SCSI ID %d\n",
	    ncr53c9x_variant_names[sc->sc_rev], sc->sc_freq, sc->sc_id);

	sc->sc_ntarg = (sc->sc_rev == NCR_VARIANT_FAS366) ? 16 : 8;

	/*
	 * Allocate SCSI message buffers.
	 * Front-ends can override allocation to avoid alignment
	 * handling in the DMA engines.  Note that ncr53c9x_msgout()
	 * can request a 1 byte DMA transfer.
	 */
	if (sc->sc_omess == NULL) {
		sc->sc_omess_self = 1;
		sc->sc_omess = malloc(NCR_MAX_MSG_LEN, M_DEVBUF, M_NOWAIT);
		if (sc->sc_omess == NULL) {
			device_printf(sc->sc_dev,
			    "cannot allocate MSGOUT buffer\n");
			return (ENOMEM);
		}
	} else
		sc->sc_omess_self = 0;

	if (sc->sc_imess == NULL) {
		sc->sc_imess_self = 1;
		sc->sc_imess = malloc(NCR_MAX_MSG_LEN + 1, M_DEVBUF, M_NOWAIT);
		if (sc->sc_imess == NULL) {
			device_printf(sc->sc_dev,
			    "cannot allocate MSGIN buffer\n");
			error = ENOMEM;
			goto fail_omess;
		}
	} else
		sc->sc_imess_self = 0;

	sc->sc_tinfo = malloc(sc->sc_ntarg * sizeof(sc->sc_tinfo[0]),
	    M_DEVBUF, M_NOWAIT | M_ZERO);
	if (sc->sc_tinfo == NULL) {
		device_printf(sc->sc_dev,
		    "cannot allocate target info buffer\n");
		error = ENOMEM;
		goto fail_imess;
	}

	/*
	 * Treat NCR53C90 with the 86C01 DMA chip exactly as ESP100
	 * from now on.
	 */
	if (sc->sc_rev == NCR_VARIANT_NCR53C90_86C01)
		sc->sc_rev = NCR_VARIANT_ESP100;

	sc->sc_ccf = FREQTOCCF(sc->sc_freq);

	/* The value *must not* be == 1.  Make it 2. */
	if (sc->sc_ccf == 1)
		sc->sc_ccf = 2;

	/*
	 * The recommended timeout is 250ms.  This register is loaded
	 * with a value calculated as follows, from the docs:
	 *
	 *		(timeout period) x (CLK frequency)
	 *	reg = -------------------------------------
	 *		 8192 x (Clock Conversion Factor)
	 *
	 * Since CCF has a linear relation to CLK, this generally computes
	 * to the constant of 153.
	 */
	sc->sc_timeout = ((250 * 1000) * sc->sc_freq) / (8192 * sc->sc_ccf);

	/* The CCF register only has 3 bits; 0 is actually 8. */
	sc->sc_ccf &= 7;

	/*
	 * Register with CAM.
	 */
	devq = cam_simq_alloc(sc->sc_ntarg);
	if (devq == NULL) {
		device_printf(sc->sc_dev, "cannot allocate device queue\n");
		error = ENOMEM;
		goto fail_tinfo;
	}

	sim = cam_sim_alloc(ncr53c9x_action, ncr53c9x_poll, "esp", sc,
	    device_get_unit(sc->sc_dev), &sc->sc_lock, 1, NCR_TAG_DEPTH, devq);
	if (sim == NULL) {
		device_printf(sc->sc_dev, "cannot allocate SIM entry\n");
		error = ENOMEM;
		goto fail_devq;
	}

	NCR_LOCK(sc);

	if (xpt_bus_register(sim, sc->sc_dev, 0) != CAM_SUCCESS) {
		device_printf(sc->sc_dev, "cannot register bus\n");
		error = EIO;
		goto fail_lock;
	}

	if (xpt_create_path(&path, NULL, cam_sim_path(sim),
	    CAM_TARGET_WILDCARD, CAM_LUN_WILDCARD) != CAM_REQ_CMP) {
		device_printf(sc->sc_dev, "cannot create path\n");
		error = EIO;
		goto fail_bus;
	}

	if (xpt_register_async(AC_LOST_DEVICE, ncr53c9x_async, sim, path) !=
	    CAM_REQ_CMP) {
		device_printf(sc->sc_dev, "cannot register async handler\n");
		error = EIO;
		goto fail_path;
	}

	sc->sc_sim = sim;
	sc->sc_path = path;

	/* Reset state and bus. */
#if 0
	sc->sc_cfflags = sc->sc_dev.dv_cfdata->cf_flags;
#else
	sc->sc_cfflags = 0;
#endif
	sc->sc_state = 0;
	ncr53c9x_init(sc, 1);

	TAILQ_INIT(&sc->free_list);
	if ((sc->ecb_array =
	    malloc(sizeof(struct ncr53c9x_ecb) * NCR_TAG_DEPTH, M_DEVBUF,
	    M_NOWAIT | M_ZERO)) == NULL) {
		device_printf(sc->sc_dev, "cannot allocate ECB array\n");
		error = ENOMEM;
		goto fail_async;
	}
	for (i = 0; i < NCR_TAG_DEPTH; i++) {
		ecb = &sc->ecb_array[i];
		ecb->sc = sc;
		ecb->tag_id = i;
		callout_init_mtx(&ecb->ch, &sc->sc_lock, 0);
		TAILQ_INSERT_HEAD(&sc->free_list, ecb, free_links);
	}

	callout_reset(&sc->sc_watchdog, 60 * hz, ncr53c9x_watch, sc);

	NCR_UNLOCK(sc);

	return (0);

fail_async:
	xpt_register_async(0, ncr53c9x_async, sim, path);
fail_path:
	xpt_free_path(path);
fail_bus:
	xpt_bus_deregister(cam_sim_path(sim));
fail_lock:
	NCR_UNLOCK(sc);
	cam_sim_free(sim, TRUE);
fail_devq:
	cam_simq_free(devq);
fail_tinfo:
	free(sc->sc_tinfo, M_DEVBUF);
fail_imess:
	if (sc->sc_imess_self)
		free(sc->sc_imess, M_DEVBUF);
fail_omess:
	if (sc->sc_omess_self)
		free(sc->sc_omess, M_DEVBUF);
	return (error);
}

int
ncr53c9x_detach(struct ncr53c9x_softc *sc)
{
	struct ncr53c9x_linfo *li, *nextli;
	int t;

	callout_drain(&sc->sc_watchdog);

	NCR_LOCK(sc);

	if (sc->sc_tinfo) {
		/* Cancel all commands. */
		ncr53c9x_clear(sc, CAM_REQ_ABORTED);

		/* Free logical units. */
		for (t = 0; t < sc->sc_ntarg; t++) {
			for (li = LIST_FIRST(&sc->sc_tinfo[t].luns); li;
			    li = nextli) {
				nextli = LIST_NEXT(li, link);
				free(li, M_DEVBUF);
			}
		}
	}

	xpt_register_async(0, ncr53c9x_async, sc->sc_sim, sc->sc_path);
	xpt_free_path(sc->sc_path);
	xpt_bus_deregister(cam_sim_path(sc->sc_sim));
	cam_sim_free(sc->sc_sim, TRUE);

	NCR_UNLOCK(sc);

	free(sc->ecb_array, M_DEVBUF);
	free(sc->sc_tinfo, M_DEVBUF);
	if (sc->sc_imess_self)
		free(sc->sc_imess, M_DEVBUF);
	if (sc->sc_omess_self)
		free(sc->sc_omess, M_DEVBUF);

	return (0);
}

/*
 * This is the generic ncr53c9x reset function.  It does not reset the SCSI
 * bus, only this controller, but kills any on-going commands, and also stops
 * and resets the DMA.
 *
 * After reset, registers are loaded with the defaults from the attach
 * routine above.
 */
static void
ncr53c9x_reset(struct ncr53c9x_softc *sc)
{

	NCR_LOCK_ASSERT(sc, MA_OWNED);

	/* Reset DMA first. */
	NCRDMA_RESET(sc);

	/* Reset SCSI chip. */
	NCRCMD(sc, NCRCMD_RSTCHIP);
	NCRCMD(sc, NCRCMD_NOP);
	DELAY(500);

	/* Do these backwards, and fall through. */
	switch (sc->sc_rev) {
	case NCR_VARIANT_ESP406:
	case NCR_VARIANT_FAS408:
		NCR_WRITE_REG(sc, NCR_CFG5, sc->sc_cfg5 | NCRCFG5_SINT);
		NCR_WRITE_REG(sc, NCR_CFG4, sc->sc_cfg4);
		/* FALLTHROUGH */
	case NCR_VARIANT_AM53C974:
	case NCR_VARIANT_FAS100A:
	case NCR_VARIANT_FAS216:
	case NCR_VARIANT_FAS236:
	case NCR_VARIANT_NCR53C94:
	case NCR_VARIANT_NCR53C96:
	case NCR_VARIANT_ESP200:
		sc->sc_features |= NCR_F_HASCFG3;
		NCR_WRITE_REG(sc, NCR_CFG3, sc->sc_cfg3);
		/* FALLTHROUGH */
	case NCR_VARIANT_ESP100A:
		sc->sc_features |= NCR_F_SELATN3;
		if ((sc->sc_cfg2 & NCRCFG2_FE) != 0)
			sc->sc_features |= NCR_F_LARGEXFER;
		NCR_WRITE_REG(sc, NCR_CFG2, sc->sc_cfg2);
		/* FALLTHROUGH */
	case NCR_VARIANT_ESP100:
		NCR_WRITE_REG(sc, NCR_CFG1, sc->sc_cfg1);
		NCR_WRITE_REG(sc, NCR_CCF, sc->sc_ccf);
		NCR_WRITE_REG(sc, NCR_SYNCOFF, 0);
		NCR_WRITE_REG(sc, NCR_TIMEOUT, sc->sc_timeout);
		break;

	case NCR_VARIANT_FAS366:
		sc->sc_features |= NCR_F_HASCFG3 | NCR_F_FASTSCSI |
		    NCR_F_SELATN3 | NCR_F_LARGEXFER;
		sc->sc_cfg3 = NCRFASCFG3_FASTCLK | NCRFASCFG3_OBAUTO;
		if (sc->sc_id > 7)
			sc->sc_cfg3 |= NCRFASCFG3_IDBIT3;
		sc->sc_cfg3_fscsi = NCRFASCFG3_FASTSCSI;
		NCR_WRITE_REG(sc, NCR_CFG3, sc->sc_cfg3);
		sc->sc_cfg2 = NCRCFG2_HMEFE | NCRCFG2_HME32;
		NCR_WRITE_REG(sc, NCR_CFG2, sc->sc_cfg2);
		NCR_WRITE_REG(sc, NCR_CFG1, sc->sc_cfg1);
		NCR_WRITE_REG(sc, NCR_CCF, sc->sc_ccf);
		NCR_WRITE_REG(sc, NCR_SYNCOFF, 0);
		NCR_WRITE_REG(sc, NCR_TIMEOUT, sc->sc_timeout);
		break;

	default:
		device_printf(sc->sc_dev,
		    "unknown revision code, assuming ESP100\n");
		NCR_WRITE_REG(sc, NCR_CFG1, sc->sc_cfg1);
		NCR_WRITE_REG(sc, NCR_CCF, sc->sc_ccf);
		NCR_WRITE_REG(sc, NCR_SYNCOFF, 0);
		NCR_WRITE_REG(sc, NCR_TIMEOUT, sc->sc_timeout);
	}

	if (sc->sc_rev == NCR_VARIANT_AM53C974)
		NCR_WRITE_REG(sc, NCR_AMDCFG4, sc->sc_cfg4);

#if 0
	device_printf(sc->sc_dev, "%s: revision %d\n", __func__, sc->sc_rev);
	device_printf(sc->sc_dev, "%s: cfg1 0x%x, cfg2 0x%x, cfg3 0x%x, ccf "
	    "0x%x, timeout 0x%x\n", __func__, sc->sc_cfg1, sc->sc_cfg2,
	    sc->sc_cfg3, sc->sc_ccf, sc->sc_timeout);
#endif
}

/*
 * Clear all commands.
 */
static void
ncr53c9x_clear(struct ncr53c9x_softc *sc, cam_status result)
{
	struct ncr53c9x_ecb *ecb;
	int r;

	NCR_LOCK_ASSERT(sc, MA_OWNED);

	/* Cancel any active commands. */
	sc->sc_state = NCR_CLEANING;
	sc->sc_msgify = 0;
	ecb = sc->sc_nexus;
	if (ecb != NULL) {
		ecb->ccb->ccb_h.status = result;
		ncr53c9x_done(sc, ecb);
	}
	/* Cancel outstanding disconnected commands. */
	for (r = 0; r < sc->sc_ntarg; r++)
		ncr53c9x_clear_target(sc, r, result);
}

/*
 * Clear all commands for a specific target.
 */
static void
ncr53c9x_clear_target(struct ncr53c9x_softc *sc, int target,
    cam_status result)
{
	struct ncr53c9x_ecb *ecb;
	struct ncr53c9x_linfo *li;
	int i;

	NCR_LOCK_ASSERT(sc, MA_OWNED);

	/* Cancel outstanding disconnected commands on each LUN. */
	LIST_FOREACH(li, &sc->sc_tinfo[target].luns, link) {
		ecb = li->untagged;
		if (ecb != NULL) {
			li->untagged = NULL;
			/*
			 * XXX should we terminate a command
			 * that never reached the disk?
			 */
			li->busy = 0;
			ecb->ccb->ccb_h.status = result;
			ncr53c9x_done(sc, ecb);
		}
		for (i = 0; i < NCR_TAG_DEPTH; i++) {
			ecb = li->queued[i];
			if (ecb != NULL) {
				li->queued[i] = NULL;
				ecb->ccb->ccb_h.status = result;
				ncr53c9x_done(sc, ecb);
			}
		}
		li->used = 0;
	}
}

/*
 * Initialize ncr53c9x state machine.
 */
static void
ncr53c9x_init(struct ncr53c9x_softc *sc, int doreset)
{
	struct ncr53c9x_tinfo *ti;
	int r;

	NCR_LOCK_ASSERT(sc, MA_OWNED);

	NCR_MISC(("[NCR_INIT(%d) %d] ", doreset, sc->sc_state));

	if (sc->sc_state == 0) {
		/* First time through; initialize. */

		TAILQ_INIT(&sc->ready_list);
		sc->sc_nexus = NULL;
		memset(sc->sc_tinfo, 0, sizeof(*sc->sc_tinfo));
		for (r = 0; r < sc->sc_ntarg; r++) {
			LIST_INIT(&sc->sc_tinfo[r].luns);
		}
	} else
		ncr53c9x_clear(sc, CAM_CMD_TIMEOUT);

	/*
	 * Reset the chip to a known state.
	 */
	ncr53c9x_reset(sc);

	sc->sc_flags = 0;
	sc->sc_msgpriq = sc->sc_msgout = sc->sc_msgoutq = 0;
	sc->sc_phase = sc->sc_prevphase = INVALID_PHASE;

	/*
	 * If we're the first time through, set the default parameters
	 * for all targets.  Otherwise we only clear their current transfer
	 * settings so we'll renegotiate their goal settings with the next
	 * command.
	 */
	if (sc->sc_state == 0) {
		for (r = 0; r < sc->sc_ntarg; r++) {
			ti = &sc->sc_tinfo[r];
/* XXX - config flags per target: low bits: no reselect; high bits: no synch */

			ti->flags = ((sc->sc_minsync != 0 &&
			    (sc->sc_cfflags & (1 << ((r & 7) + 8))) == 0) ?
			    0 : T_SYNCHOFF) |
			    ((sc->sc_cfflags & (1 << (r & 7))) == 0 ?
			    0 : T_RSELECTOFF);
			ti->curr.period = ti->goal.period = 0;
			ti->curr.offset = ti->goal.offset = 0;
			ti->curr.width = ti->goal.width =
			    MSG_EXT_WDTR_BUS_8_BIT;
		}
	} else {
		for (r = 0; r < sc->sc_ntarg; r++) {
			ti = &sc->sc_tinfo[r];
			ti->flags &= ~(T_SDTRSENT | T_WDTRSENT);
			ti->curr.period = 0;
			ti->curr.offset = 0;
			ti->curr.width = MSG_EXT_WDTR_BUS_8_BIT;
		}
	}

	if (doreset) {
		sc->sc_state = NCR_SBR;
		NCRCMD(sc, NCRCMD_RSTSCSI);
		/* Give the bus a fighting chance to settle. */
		DELAY(250000);
	} else {
		sc->sc_state = NCR_IDLE;
		ncr53c9x_sched(sc);
	}
}

/*
 * Read the NCR registers, and save their contents for later use.
 * NCR_STAT, NCR_STEP & NCR_INTR are mostly zeroed out when reading
 * NCR_INTR - so make sure it is the last read.
 *
 * I think that (from reading the docs) most bits in these registers
 * only make sense when the DMA CSR has an interrupt showing.  Call only
 * if an interrupt is pending.
 */
static inline void
ncr53c9x_readregs(struct ncr53c9x_softc *sc)
{

	NCR_LOCK_ASSERT(sc, MA_OWNED);

	sc->sc_espstat = NCR_READ_REG(sc, NCR_STAT);
	/* Only the step bits are of interest. */
	sc->sc_espstep = NCR_READ_REG(sc, NCR_STEP) & NCRSTEP_MASK;

	if (sc->sc_rev == NCR_VARIANT_FAS366)
		sc->sc_espstat2 = NCR_READ_REG(sc, NCR_STAT2);

	sc->sc_espintr = NCR_READ_REG(sc, NCR_INTR);

	/*
	 * Determine the SCSI bus phase, return either a real SCSI bus phase
	 * or some pseudo phase we use to detect certain exceptions.
	 */
	sc->sc_phase = (sc->sc_espintr & NCRINTR_DIS) ?
	    BUSFREE_PHASE : sc->sc_espstat & NCRSTAT_PHASE;

	NCR_INTS(("regs[intr=%02x,stat=%02x,step=%02x,stat2=%02x] ",
	    sc->sc_espintr, sc->sc_espstat, sc->sc_espstep, sc->sc_espstat2));
}

/*
 * Convert Synchronous Transfer Period to chip register Clock Per Byte value.
 */
static inline int
ncr53c9x_stp2cpb(struct ncr53c9x_softc *sc, int period)
{
	int v;

	NCR_LOCK_ASSERT(sc, MA_OWNED);

	v = (sc->sc_freq * period) / 250;
	if (ncr53c9x_cpb2stp(sc, v) < period)
		/* Correct round-down error. */
		v++;
	return (v);
}

static inline void
ncr53c9x_setsync(struct ncr53c9x_softc *sc, struct ncr53c9x_tinfo *ti)
{
	uint8_t cfg3, syncoff, synctp;

	NCR_LOCK_ASSERT(sc, MA_OWNED);

	cfg3 = sc->sc_cfg3;
	if (ti->curr.offset != 0) {
		syncoff = ti->curr.offset;
		synctp = ncr53c9x_stp2cpb(sc, ti->curr.period);
		if (sc->sc_features & NCR_F_FASTSCSI) {
			/*
			 * If the period is 200ns or less (ti->period <= 50),
			 * put the chip in Fast SCSI mode.
			 */
			if (ti->curr.period <= 50)
				/*
				 * There are (at least) 4 variations of the
				 * configuration 3 register.  The drive attach
				 * routine sets the appropriate bit to put the
				 * chip into Fast SCSI mode so that it doesn't
				 * have to be figured out here each time.
				 */
				cfg3 |= sc->sc_cfg3_fscsi;
		}

		/*
		 * Am53c974 requires different SYNCTP values when the
		 * FSCSI bit is off.
		 */
		if (sc->sc_rev == NCR_VARIANT_AM53C974 &&
		    (cfg3 & NCRAMDCFG3_FSCSI) == 0)
			synctp--;
	} else {
		syncoff = 0;
		synctp = 0;
	}

	if (ti->curr.width != MSG_EXT_WDTR_BUS_8_BIT) {
		if (sc->sc_rev == NCR_VARIANT_FAS366)
			cfg3 |= NCRFASCFG3_EWIDE;
	}

	if (sc->sc_features & NCR_F_HASCFG3)
		NCR_WRITE_REG(sc, NCR_CFG3, cfg3);

	NCR_WRITE_REG(sc, NCR_SYNCOFF, syncoff);
	NCR_WRITE_REG(sc, NCR_SYNCTP, synctp);
}

/*
 * Send a command to a target, set the driver state to NCR_SELECTING
 * and let the caller take care of the rest.
 *
 * Keeping this as a function allows me to say that this may be done
 * by DMA instead of programmed I/O soon.
 */
static void
ncr53c9x_select(struct ncr53c9x_softc *sc, struct ncr53c9x_ecb *ecb)
{
	struct ncr53c9x_tinfo *ti;
	uint8_t *cmd;
	size_t dmasize;
	int clen, error, selatn3, selatns;
	int lun = ecb->ccb->ccb_h.target_lun;
	int target = ecb->ccb->ccb_h.target_id;

	NCR_LOCK_ASSERT(sc, MA_OWNED);

	NCR_TRACE(("[%s(t%d,l%d,cmd:%x,tag:%x,%x)] ", __func__, target, lun,
	    ecb->cmd.cmd.opcode, ecb->tag[0], ecb->tag[1]));

	ti = &sc->sc_tinfo[target];
	sc->sc_state = NCR_SELECTING;
	/*
	 * Schedule the callout now, the first time we will go away
	 * expecting to come back due to an interrupt, because it is
	 * always possible that the interrupt may never happen.
	 */
	callout_reset(&ecb->ch, mstohz(ecb->timeout), ncr53c9x_callout, ecb);

	/*
	 * The docs say the target register is never reset, and I
	 * can't think of a better place to set it.
	 */
	if (sc->sc_rev == NCR_VARIANT_FAS366) {
		NCRCMD(sc, NCRCMD_FLUSH);
		NCR_WRITE_REG(sc, NCR_SELID, target | NCR_BUSID_HMEXC32 |
		    NCR_BUSID_HMEENCID);
	} else
		NCR_WRITE_REG(sc, NCR_SELID, target);

	/*
	 * If we are requesting sense, force a renegotiation if we are
	 * currently using anything different from asynchronous at 8 bit
	 * as the target might have lost our transfer negotiations.
	 */
	if ((ecb->flags & ECB_SENSE) != 0 && (ti->curr.offset != 0 ||
	    ti->curr.width != MSG_EXT_WDTR_BUS_8_BIT)) {
		ti->curr.period = 0;
		ti->curr.offset = 0;
		ti->curr.width = MSG_EXT_WDTR_BUS_8_BIT;
	}
	ncr53c9x_setsync(sc, ti);

	selatn3 = selatns = 0;
	if (ecb->tag[0] != 0) {
		if (sc->sc_features & NCR_F_SELATN3)
			/* Use SELATN3 to send tag messages. */
			selatn3 = 1;
		else
			/* We don't have SELATN3; use SELATNS to send tags. */
			selatns = 1;
	}

	if (ti->curr.period != ti->goal.period ||
	    ti->curr.offset != ti->goal.offset ||
	    ti->curr.width != ti->goal.width) {
		/* We have to use SELATNS to send sync/wide messages. */
		selatn3 = 0;
		selatns = 1;
	}

	cmd = (uint8_t *)&ecb->cmd.cmd;

	if (selatn3) {
		/* We'll use tags with SELATN3. */
		clen = ecb->clen + 3;
		cmd -= 3;
		cmd[0] = MSG_IDENTIFY(lun, 1);	/* msg[0] */
		cmd[1] = ecb->tag[0];		/* msg[1] */
		cmd[2] = ecb->tag[1];		/* msg[2] */
	} else {
		/* We don't have tags, or will send messages with SELATNS. */
		clen = ecb->clen + 1;
		cmd -= 1;
		cmd[0] = MSG_IDENTIFY(lun, (ti->flags & T_RSELECTOFF) == 0);
	}

	if ((sc->sc_features & NCR_F_DMASELECT) && !selatns) {
		/* Setup DMA transfer for command. */
		dmasize = clen;
		sc->sc_cmdlen = clen;
		sc->sc_cmdp = cmd;
		error = NCRDMA_SETUP(sc, &sc->sc_cmdp, &sc->sc_cmdlen, 0,
		    &dmasize);
		if (error != 0)
			goto cmd;

		/* Program the SCSI counter. */
		NCR_SET_COUNT(sc, dmasize);

		/* Load the count in. */
		NCRCMD(sc, NCRCMD_NOP | NCRCMD_DMA);

		/* And get the target's attention. */
		if (selatn3) {
			sc->sc_msgout = SEND_TAG;
			sc->sc_flags |= NCR_ATN;
			NCRCMD(sc, NCRCMD_SELATN3 | NCRCMD_DMA);
		} else
			NCRCMD(sc, NCRCMD_SELATN | NCRCMD_DMA);
		NCRDMA_GO(sc);
		return;
	}

cmd:
	/*
	 * Who am I?  This is where we tell the target that we are
	 * happy for it to disconnect etc.
	 */

	/* Now get the command into the FIFO. */
	sc->sc_cmdlen = 0;
	ncr53c9x_wrfifo(sc, cmd, clen);

	/* And get the target's attention. */
	if (selatns) {
		NCR_MSGS(("SELATNS \n"));
		/* Arbitrate, select and stop after IDENTIFY message. */
		NCRCMD(sc, NCRCMD_SELATNS);
	} else if (selatn3) {
		sc->sc_msgout = SEND_TAG;
		sc->sc_flags |= NCR_ATN;
		NCRCMD(sc, NCRCMD_SELATN3);
	} else
		NCRCMD(sc, NCRCMD_SELATN);
}

static void
ncr53c9x_free_ecb(struct ncr53c9x_softc *sc, struct ncr53c9x_ecb *ecb)
{

	NCR_LOCK_ASSERT(sc, MA_OWNED);

	ecb->flags = 0;
	TAILQ_INSERT_TAIL(&sc->free_list, ecb, free_links);
}

static struct ncr53c9x_ecb *
ncr53c9x_get_ecb(struct ncr53c9x_softc *sc)
{
	struct ncr53c9x_ecb *ecb;

	NCR_LOCK_ASSERT(sc, MA_OWNED);

	ecb = TAILQ_FIRST(&sc->free_list);
	if (ecb) {
		if (ecb->flags != 0)
			panic("%s: ecb flags not cleared", __func__);
		TAILQ_REMOVE(&sc->free_list, ecb, free_links);
		ecb->flags = ECB_ALLOC;
		bzero(&ecb->ccb, sizeof(struct ncr53c9x_ecb) -
		    offsetof(struct ncr53c9x_ecb, ccb));
	}
	return (ecb);
}

/*
 * DRIVER FUNCTIONS CALLABLE FROM HIGHER LEVEL DRIVERS:
 */

/*
 * Start a SCSI-command.
 * This function is called by the higher level SCSI-driver to queue/run
 * SCSI-commands.
 */

static void
ncr53c9x_action(struct cam_sim *sim, union ccb *ccb)
{
	struct ccb_pathinq *cpi;
	struct ccb_scsiio *csio;
	struct ccb_trans_settings *cts;
	struct ccb_trans_settings_scsi *scsi;
	struct ccb_trans_settings_spi *spi;
	struct ncr53c9x_ecb *ecb;
	struct ncr53c9x_softc *sc;
	struct ncr53c9x_tinfo *ti;
	int target;

	sc = cam_sim_softc(sim);

	NCR_LOCK_ASSERT(sc, MA_OWNED);

	NCR_TRACE(("[%s %d]", __func__, ccb->ccb_h.func_code));

	switch (ccb->ccb_h.func_code) {
	case XPT_RESET_BUS:
		ncr53c9x_init(sc, 1);
		ccb->ccb_h.status = CAM_REQ_CMP;
		break;

	case XPT_CALC_GEOMETRY:
		cam_calc_geometry(&ccb->ccg, sc->sc_extended_geom);
		break;

	case XPT_PATH_INQ:
		cpi = &ccb->cpi;
		cpi->version_num = 1;
		cpi->hba_inquiry = PI_SDTR_ABLE | PI_TAG_ABLE;
		cpi->hba_inquiry |=
		    (sc->sc_rev == NCR_VARIANT_FAS366) ? PI_WIDE_16 : 0;
		cpi->target_sprt = 0;
		cpi->hba_misc = 0;
		cpi->hba_eng_cnt = 0;
		cpi->max_target = sc->sc_ntarg - 1;
		cpi->max_lun = 7;
		cpi->initiator_id = sc->sc_id;
		strlcpy(cpi->sim_vid, "FreeBSD", SIM_IDLEN);
		strlcpy(cpi->hba_vid, "NCR", HBA_IDLEN);
		strlcpy(cpi->dev_name, cam_sim_name(sim), DEV_IDLEN);
		cpi->unit_number = cam_sim_unit(sim);
		cpi->bus_id = 0;
		cpi->base_transfer_speed = 3300;
		cpi->protocol = PROTO_SCSI;
		cpi->protocol_version = SCSI_REV_2;
		cpi->transport = XPORT_SPI;
		cpi->transport_version = 2;
		cpi->maxio = sc->sc_maxxfer;
		ccb->ccb_h.status = CAM_REQ_CMP;
		break;

	case XPT_GET_TRAN_SETTINGS:
		cts = &ccb->cts;
		ti = &sc->sc_tinfo[ccb->ccb_h.target_id];
		scsi = &cts->proto_specific.scsi;
		spi = &cts->xport_specific.spi;

		cts->protocol = PROTO_SCSI;
		cts->protocol_version = SCSI_REV_2;
		cts->transport = XPORT_SPI;
		cts->transport_version = 2;

		if (cts->type == CTS_TYPE_CURRENT_SETTINGS) {
			spi->sync_period = ti->curr.period;
			spi->sync_offset = ti->curr.offset;
			spi->bus_width = ti->curr.width;
			if ((ti->flags & T_TAG) != 0) {
				spi->flags |= CTS_SPI_FLAGS_DISC_ENB;
				scsi->flags |= CTS_SCSI_FLAGS_TAG_ENB;
			} else {
				spi->flags &= ~CTS_SPI_FLAGS_DISC_ENB;
				scsi->flags &= ~CTS_SCSI_FLAGS_TAG_ENB;
			}
		} else {
			if ((ti->flags & T_SYNCHOFF) != 0) {
				spi->sync_period = 0;
				spi->sync_offset = 0;
			} else {
				spi->sync_period = sc->sc_minsync;
				spi->sync_offset = sc->sc_maxoffset;
			}
			spi->bus_width = sc->sc_maxwidth;
			spi->flags |= CTS_SPI_FLAGS_DISC_ENB;
			scsi->flags |= CTS_SCSI_FLAGS_TAG_ENB;
		}
		spi->valid =
		    CTS_SPI_VALID_BUS_WIDTH |
		    CTS_SPI_VALID_SYNC_RATE |
		    CTS_SPI_VALID_SYNC_OFFSET |
		    CTS_SPI_VALID_DISC;
		scsi->valid = CTS_SCSI_VALID_TQ;
		ccb->ccb_h.status = CAM_REQ_CMP;
		break;

	case XPT_ABORT:
		device_printf(sc->sc_dev, "XPT_ABORT called\n");
		ccb->ccb_h.status = CAM_FUNC_NOTAVAIL;
		break;

	case XPT_TERM_IO:
		device_printf(sc->sc_dev, "XPT_TERM_IO called\n");
		ccb->ccb_h.status = CAM_FUNC_NOTAVAIL;
		break;

	case XPT_RESET_DEV:
	case XPT_SCSI_IO:
		if (ccb->ccb_h.target_id >= sc->sc_ntarg) {
			ccb->ccb_h.status = CAM_PATH_INVALID;
			goto done;
		}
		/* Get an ECB to use. */
		ecb = ncr53c9x_get_ecb(sc);
		/*
		 * This should never happen as we track resources
		 * in the mid-layer.
		 */
		if (ecb == NULL) {
			xpt_freeze_simq(sim, 1);
			ccb->ccb_h.status = CAM_REQUEUE_REQ;
			device_printf(sc->sc_dev, "unable to allocate ecb\n");
			goto done;
		}

		/* Initialize ecb. */
		ecb->ccb = ccb;
		ecb->timeout = ccb->ccb_h.timeout;

		if (ccb->ccb_h.func_code == XPT_RESET_DEV) {
			ecb->flags |= ECB_RESET;
			ecb->clen = 0;
			ecb->dleft = 0;
		} else {
			csio = &ccb->csio;
			if ((ccb->ccb_h.flags & CAM_CDB_POINTER) != 0)
				bcopy(csio->cdb_io.cdb_ptr, &ecb->cmd.cmd,
				    csio->cdb_len);
			else
				bcopy(csio->cdb_io.cdb_bytes, &ecb->cmd.cmd,
				    csio->cdb_len);
			ecb->clen = csio->cdb_len;
			ecb->daddr = csio->data_ptr;
			ecb->dleft = csio->dxfer_len;
		}
		ecb->stat = 0;

		TAILQ_INSERT_TAIL(&sc->ready_list, ecb, chain);
		ecb->flags |= ECB_READY;
		if (sc->sc_state == NCR_IDLE)
			ncr53c9x_sched(sc);
		return;

	case XPT_SET_TRAN_SETTINGS:
		cts = &ccb->cts;
		target = ccb->ccb_h.target_id;
		ti = &sc->sc_tinfo[target];
		scsi = &cts->proto_specific.scsi;
		spi = &cts->xport_specific.spi;

		if ((scsi->valid & CTS_SCSI_VALID_TQ) != 0) {
			if ((sc->sc_cfflags & (1<<((target & 7) + 16))) == 0 &&
			    (scsi->flags & CTS_SCSI_FLAGS_TAG_ENB)) {
				NCR_MISC(("%s: target %d: tagged queuing\n",
				    device_get_nameunit(sc->sc_dev), target));
				ti->flags |= T_TAG;
			} else
				ti->flags &= ~T_TAG;
		}

		if ((spi->valid & CTS_SPI_VALID_BUS_WIDTH) != 0) {
			NCR_MISC(("%s: target %d: wide negotiation\n",
			    device_get_nameunit(sc->sc_dev), target));
			ti->goal.width = spi->bus_width;
		}

		if ((spi->valid & CTS_SPI_VALID_SYNC_RATE) != 0) {
			NCR_MISC(("%s: target %d: sync period negotiation\n",
			    device_get_nameunit(sc->sc_dev), target));
			ti->goal.period = spi->sync_period;
		}

		if ((spi->valid & CTS_SPI_VALID_SYNC_OFFSET) != 0) {
			NCR_MISC(("%s: target %d: sync offset negotiation\n",
			    device_get_nameunit(sc->sc_dev), target));
			ti->goal.offset = spi->sync_offset;
		}

		ccb->ccb_h.status = CAM_REQ_CMP;
		break;

	default:
		device_printf(sc->sc_dev, "Unhandled function code %d\n",
		    ccb->ccb_h.func_code);
		ccb->ccb_h.status = CAM_PROVIDE_FAIL;
	}

done:
	xpt_done(ccb);
}

/*
 * Used when interrupt driven I/O is not allowed, e.g. during boot.
 */
static void
ncr53c9x_poll(struct cam_sim *sim)
{
	struct ncr53c9x_softc *sc;

	sc = cam_sim_softc(sim);

	NCR_LOCK_ASSERT(sc, MA_OWNED);

	NCR_TRACE(("[%s] ", __func__));

	if (NCRDMA_ISINTR(sc))
		ncr53c9x_intr1(sc);
}

/*
 *  Asynchronous notification handler
 */
static void
ncr53c9x_async(void *cbarg, uint32_t code, struct cam_path *path, void *arg)
{
	struct ncr53c9x_softc *sc;
	struct ncr53c9x_tinfo *ti;
	int target;

	sc = cam_sim_softc(cbarg);

	NCR_LOCK_ASSERT(sc, MA_OWNED);

	switch (code) {
	case AC_LOST_DEVICE:
		target = xpt_path_target_id(path);
		if (target < 0 || target >= sc->sc_ntarg)
			break;

		/* Cancel outstanding disconnected commands. */
		ncr53c9x_clear_target(sc, target, CAM_REQ_ABORTED);

		/* Set the default parameters for the target. */
		ti = &sc->sc_tinfo[target];
/* XXX - config flags per target: low bits: no reselect; high bits: no synch */
		ti->flags = ((sc->sc_minsync != 0 &&
		    (sc->sc_cfflags & (1 << ((target & 7) + 8))) == 0) ?
		    0 : T_SYNCHOFF) |
		    ((sc->sc_cfflags & (1 << (target & 7))) == 0 ?
		    0 : T_RSELECTOFF);
		ti->curr.period = ti->goal.period = 0;
		ti->curr.offset = ti->goal.offset = 0;
		ti->curr.width = ti->goal.width = MSG_EXT_WDTR_BUS_8_BIT;
		break;
	}
}

/*
 * LOW LEVEL SCSI UTILITIES
 */

/*
 * Schedule a SCSI operation.  This has now been pulled out of the interrupt
 * handler so that we may call it from ncr53c9x_action and ncr53c9x_done.
 * This may save us an unnecessary interrupt just to get things going.
 * Should only be called when state == NCR_IDLE and with sc_lock held.
 */
static void
ncr53c9x_sched(struct ncr53c9x_softc *sc)
{
	struct ncr53c9x_ecb *ecb;
	struct ncr53c9x_linfo *li;
	struct ncr53c9x_tinfo *ti;
	int lun, tag;

	NCR_LOCK_ASSERT(sc, MA_OWNED);

	NCR_TRACE(("[%s] ", __func__));

	if (sc->sc_state != NCR_IDLE)
		panic("%s: not IDLE (state=%d)", __func__, sc->sc_state);

	/*
	 * Find first ecb in ready queue that is for a target/lunit
	 * combinations that is not busy.
	 */
	TAILQ_FOREACH(ecb, &sc->ready_list, chain) {
		ti = &sc->sc_tinfo[ecb->ccb->ccb_h.target_id];
		lun = ecb->ccb->ccb_h.target_lun;

		/* Select type of tag for this command */
		if ((ti->flags & (T_RSELECTOFF | T_TAG)) != T_TAG)
			tag = 0;
		else if ((ecb->flags & ECB_SENSE) != 0)
			tag = 0;
		else if ((ecb->ccb->ccb_h.flags & CAM_TAG_ACTION_VALID) == 0)
			tag = 0;
		else if (ecb->ccb->csio.tag_action == CAM_TAG_ACTION_NONE)
			tag = 0;
		else
			tag = ecb->ccb->csio.tag_action;

		li = TINFO_LUN(ti, lun);
		if (li == NULL) {
			/* Initialize LUN info and add to list. */
			li = malloc(sizeof(*li), M_DEVBUF, M_NOWAIT | M_ZERO);
			if (li == NULL)
				continue;
			li->lun = lun;

			LIST_INSERT_HEAD(&ti->luns, li, link);
			if (lun < NCR_NLUN)
				ti->lun[lun] = li;
		}
		li->last_used = time_second;
		if (tag == 0) {
			/* Try to issue this as an untagged command. */
			if (li->untagged == NULL)
				li->untagged = ecb;
		}
		if (li->untagged != NULL) {
			tag = 0;
			if ((li->busy != 1) && li->used == 0) {
				/*
				 * We need to issue this untagged command
				 * now.
				 */
				ecb = li->untagged;
			} else {
				/* not ready, yet */
				continue;
			}
		}
		ecb->tag[0] = tag;
		if (tag != 0) {
			li->queued[ecb->tag_id] = ecb;
			ecb->tag[1] = ecb->tag_id;
			li->used++;
		}
		if (li->untagged != NULL && (li->busy != 1)) {
			li->busy = 1;
			TAILQ_REMOVE(&sc->ready_list, ecb, chain);
			ecb->flags &= ~ECB_READY;
			sc->sc_nexus = ecb;
			ncr53c9x_select(sc, ecb);
			break;
		}
		if (li->untagged == NULL && tag != 0) {
			TAILQ_REMOVE(&sc->ready_list, ecb, chain);
			ecb->flags &= ~ECB_READY;
			sc->sc_nexus = ecb;
			ncr53c9x_select(sc, ecb);
			break;
		} else
			NCR_TRACE(("[%s %d:%d busy] \n", __func__,
			    ecb->ccb->ccb_h.target_id,
			    ecb->ccb->ccb_h.target_lun));
	}
}

static void
ncr53c9x_sense(struct ncr53c9x_softc *sc, struct ncr53c9x_ecb *ecb)
{
	union ccb *ccb = ecb->ccb;
	struct ncr53c9x_linfo *li;
	struct ncr53c9x_tinfo *ti;
	struct scsi_request_sense *ss = (void *)&ecb->cmd.cmd;
	int lun;

	NCR_LOCK_ASSERT(sc, MA_OWNED);

	NCR_TRACE(("[%s] ", __func__));

	lun = ccb->ccb_h.target_lun;
	ti = &sc->sc_tinfo[ccb->ccb_h.target_id];

	/* Next, setup a REQUEST SENSE command block. */
	memset(ss, 0, sizeof(*ss));
	ss->opcode = REQUEST_SENSE;
	ss->byte2 = ccb->ccb_h.target_lun << SCSI_CMD_LUN_SHIFT;
	ss->length = sizeof(struct scsi_sense_data);
	ecb->clen = sizeof(*ss);
	memset(&ccb->csio.sense_data, 0, sizeof(ccb->csio.sense_data));
	ecb->daddr = (uint8_t *)&ccb->csio.sense_data;
	ecb->dleft = sizeof(struct scsi_sense_data);
	ecb->flags |= ECB_SENSE;
	ecb->timeout = NCR_SENSE_TIMEOUT;
	ti->senses++;
	li = TINFO_LUN(ti, lun);
	if (li->busy)
		li->busy = 0;
	ncr53c9x_dequeue(sc, ecb);
	li->untagged = ecb;	/* Must be executed first to fix C/A. */
	li->busy = 2;
	if (ecb == sc->sc_nexus)
		ncr53c9x_select(sc, ecb);
	else {
		TAILQ_INSERT_HEAD(&sc->ready_list, ecb, chain);
		ecb->flags |= ECB_READY;
		if (sc->sc_state == NCR_IDLE)
			ncr53c9x_sched(sc);
	}
}

/*
 * POST PROCESSING OF SCSI_CMD (usually current)
 */
static void
ncr53c9x_done(struct ncr53c9x_softc *sc, struct ncr53c9x_ecb *ecb)
{
	union ccb *ccb = ecb->ccb;
	struct ncr53c9x_linfo *li;
	struct ncr53c9x_tinfo *ti;
	int lun, sense_returned;

	NCR_LOCK_ASSERT(sc, MA_OWNED);

	NCR_TRACE(("[%s(status:%x)] ", __func__, ccb->ccb_h.status));

	ti = &sc->sc_tinfo[ccb->ccb_h.target_id];
	lun = ccb->ccb_h.target_lun;
	li = TINFO_LUN(ti, lun);

	callout_stop(&ecb->ch);

	/*
	 * Now, if we've come here with no error code, i.e. we've kept the
	 * initial CAM_REQ_CMP, and the status code signals that we should
	 * check sense, we'll need to set up a request sense cmd block and
	 * push the command back into the ready queue *before* any other
	 * commands for this target/lunit, else we lose the sense info.
	 * We don't support chk sense conditions for the request sense cmd.
	 */
	if (ccb->ccb_h.status == CAM_REQ_CMP) {
		ccb->csio.scsi_status = ecb->stat;
		if ((ecb->flags & ECB_ABORT) != 0)
			ccb->ccb_h.status = CAM_CMD_TIMEOUT;
		else if ((ecb->flags & ECB_SENSE) != 0 &&
		   (ecb->stat != SCSI_STATUS_CHECK_COND)) {
			ccb->csio.scsi_status = SCSI_STATUS_CHECK_COND;
			ccb->ccb_h.status = CAM_SCSI_STATUS_ERROR |
			    CAM_AUTOSNS_VALID;
			sense_returned = sizeof(ccb->csio.sense_data) -
			    ecb->dleft;
			if (sense_returned < ccb->csio.sense_len)
				ccb->csio.sense_resid = ccb->csio.sense_len -
				    sense_returned;
			else
				ccb->csio.sense_resid = 0;
		} else if (ecb->stat == SCSI_STATUS_CHECK_COND) {
			if ((ecb->flags & ECB_SENSE) != 0)
				ccb->ccb_h.status = CAM_AUTOSENSE_FAIL;
			else {
				/* First, save the return values. */
				ccb->csio.resid = ecb->dleft;
				if ((ccb->ccb_h.flags & CAM_DIS_AUTOSENSE) ==
				    0) {
					ncr53c9x_sense(sc, ecb);
					return;
				}
				ccb->ccb_h.status = CAM_SCSI_STATUS_ERROR;
			}
		} else
			ccb->csio.resid = ecb->dleft;
		if (ecb->stat == SCSI_STATUS_QUEUE_FULL)
			ccb->ccb_h.status = CAM_SCSI_STATUS_ERROR;
		else if (ecb->stat == SCSI_STATUS_BUSY)
			ccb->ccb_h.status = CAM_SCSI_BUSY;
	} else if ((ccb->ccb_h.status & CAM_DEV_QFRZN) == 0) {
		ccb->ccb_h.status |= CAM_DEV_QFRZN;
		xpt_freeze_devq(ccb->ccb_h.path, 1);
	}

#ifdef NCR53C9X_DEBUG
	if ((ncr53c9x_debug & NCR_SHOWTRAC) != 0) {
		if (ccb->csio.resid != 0)
			printf("resid=%d ", ccb->csio.resid);
		if ((ccb->ccb_h.status & CAM_AUTOSNS_VALID) != 0)
			printf("sense=0x%02x\n",
			    ccb->csio.sense_data.error_code);
		else
			printf("status SCSI=0x%x CAM=0x%x\n",
			    ccb->csio.scsi_status, ccb->ccb_h.status);
	}
#endif

	/*
	 * Remove the ECB from whatever queue it's on.
	 */
	ncr53c9x_dequeue(sc, ecb);
	if (ecb == sc->sc_nexus) {
		sc->sc_nexus = NULL;
		if (sc->sc_state != NCR_CLEANING) {
			sc->sc_state = NCR_IDLE;
			ncr53c9x_sched(sc);
		}
	}

	if ((ccb->ccb_h.status & CAM_SEL_TIMEOUT) != 0) {
		/* Selection timeout -- discard this LUN if empty. */
		if (li->untagged == NULL && li->used == 0) {
			if (lun < NCR_NLUN)
				ti->lun[lun] = NULL;
			LIST_REMOVE(li, link);
			free(li, M_DEVBUF);
		}
	}

	ncr53c9x_free_ecb(sc, ecb);
	ti->cmds++;
	xpt_done(ccb);
}

static void
ncr53c9x_dequeue(struct ncr53c9x_softc *sc, struct ncr53c9x_ecb *ecb)
{
	struct ncr53c9x_linfo *li;
	struct ncr53c9x_tinfo *ti;
	int64_t lun;

	NCR_LOCK_ASSERT(sc, MA_OWNED);

	ti = &sc->sc_tinfo[ecb->ccb->ccb_h.target_id];
	lun = ecb->ccb->ccb_h.target_lun;
	li = TINFO_LUN(ti, lun);
#ifdef DIAGNOSTIC
	if (li == NULL || li->lun != lun)
		panic("%s: lun %llx for ecb %p does not exist", __func__,
		    (long long)lun, ecb);
#endif
	if (li->untagged == ecb) {
		li->busy = 0;
		li->untagged = NULL;
	}
	if (ecb->tag[0] && li->queued[ecb->tag[1]] != NULL) {
#ifdef DIAGNOSTIC
		if (li->queued[ecb->tag[1]] != NULL &&
		    (li->queued[ecb->tag[1]] != ecb))
			panic("%s: slot %d for lun %llx has %p instead of ecb "
			    "%p", __func__, ecb->tag[1], (long long)lun,
			    li->queued[ecb->tag[1]], ecb);
#endif
		li->queued[ecb->tag[1]] = NULL;
		li->used--;
	}
	ecb->tag[0] = ecb->tag[1] = 0;

	if ((ecb->flags & ECB_READY) != 0) {
		ecb->flags &= ~ECB_READY;
		TAILQ_REMOVE(&sc->ready_list, ecb, chain);
	}
}

/*
 * INTERRUPT/PROTOCOL ENGINE
 */

/*
 * Schedule an outgoing message by prioritizing it, and asserting
 * attention on the bus.  We can only do this when we are the initiator
 * else there will be an illegal command interrupt.
 */
#define	ncr53c9x_sched_msgout(m) do {					\
	NCR_MSGS(("ncr53c9x_sched_msgout %x %d", m, __LINE__));		\
	NCRCMD(sc, NCRCMD_SETATN);					\
	sc->sc_flags |= NCR_ATN;					\
	sc->sc_msgpriq |= (m);						\
} while (/* CONSTCOND */0)

static void
ncr53c9x_flushfifo(struct ncr53c9x_softc *sc)
{

	NCR_LOCK_ASSERT(sc, MA_OWNED);

	NCR_TRACE(("[%s] ", __func__));

	NCRCMD(sc, NCRCMD_FLUSH);

	if (sc->sc_phase == COMMAND_PHASE ||
	    sc->sc_phase == MESSAGE_OUT_PHASE)
		DELAY(2);
}

static int
ncr53c9x_rdfifo(struct ncr53c9x_softc *sc, int how)
{
	int i, n;
	uint8_t *ibuf;

	NCR_LOCK_ASSERT(sc, MA_OWNED);

	switch (how) {
	case NCR_RDFIFO_START:
		ibuf = sc->sc_imess;
		sc->sc_imlen = 0;
		break;

	case NCR_RDFIFO_CONTINUE:
		ibuf = sc->sc_imess + sc->sc_imlen;
		break;

	default:
		panic("%s: bad flag", __func__);
		/* NOTREACHED */
	}

	/*
	 * XXX buffer (sc_imess) size for message
	 */

	n = NCR_READ_REG(sc, NCR_FFLAG) & NCRFIFO_FF;

	if (sc->sc_rev == NCR_VARIANT_FAS366) {
		n *= 2;

		for (i = 0; i < n; i++)
			ibuf[i] = NCR_READ_REG(sc, NCR_FIFO);

		if (sc->sc_espstat2 & NCRFAS_STAT2_ISHUTTLE) {

			NCR_WRITE_REG(sc, NCR_FIFO, 0);
			ibuf[i++] = NCR_READ_REG(sc, NCR_FIFO);

			NCR_READ_REG(sc, NCR_FIFO);

			ncr53c9x_flushfifo(sc);
		}
	} else
		for (i = 0; i < n; i++)
			ibuf[i] = NCR_READ_REG(sc, NCR_FIFO);

	sc->sc_imlen += i;

#if 0
#ifdef NCR53C9X_DEBUG
	NCR_TRACE(("\n[rdfifo %s (%d):",
	    (how == NCR_RDFIFO_START) ? "start" : "cont", (int)sc->sc_imlen));
	if ((ncr53c9x_debug & NCR_SHOWTRAC) != 0) {
		for (i = 0; i < sc->sc_imlen; i++)
			printf(" %02x", sc->sc_imess[i]);
		printf("]\n");
	}
#endif
#endif
	return (sc->sc_imlen);
}

static void
ncr53c9x_wrfifo(struct ncr53c9x_softc *sc, uint8_t *p, int len)
{
	int i;

	NCR_LOCK_ASSERT(sc, MA_OWNED);

#ifdef NCR53C9X_DEBUG
	NCR_MSGS(("[wrfifo(%d):", len));
	if ((ncr53c9x_debug & NCR_SHOWMSGS) != 0) {
		for (i = 0; i < len; i++)
			printf(" %02x", p[i]);
		printf("]\n");
	}
#endif

	for (i = 0; i < len; i++) {
		NCR_WRITE_REG(sc, NCR_FIFO, p[i]);

		if (sc->sc_rev == NCR_VARIANT_FAS366)
			NCR_WRITE_REG(sc, NCR_FIFO, 0);
	}
}

static int
ncr53c9x_reselect(struct ncr53c9x_softc *sc, int message, int tagtype,
    int tagid)
{
	struct ncr53c9x_ecb *ecb = NULL;
	struct ncr53c9x_linfo *li;
	struct ncr53c9x_tinfo *ti;
	uint8_t lun, selid, target;

	NCR_LOCK_ASSERT(sc, MA_OWNED);

	if (sc->sc_rev == NCR_VARIANT_FAS366)
		target = sc->sc_selid;
	else {
		/*
		 * The SCSI chip made a snapshot of the data bus
		 * while the reselection was being negotiated.
		 * This enables us to determine which target did
		 * the reselect.
		 */
		selid = sc->sc_selid & ~(1 << sc->sc_id);
		if (selid & (selid - 1)) {
			device_printf(sc->sc_dev, "reselect with invalid "
			    "selid %02x; sending DEVICE RESET\n", selid);
			goto reset;
		}

		target = ffs(selid) - 1;
	}
	lun = message & 0x07;

	/*
	 * Search wait queue for disconnected command.
	 * The list should be short, so I haven't bothered with
	 * any more sophisticated structures than a simple
	 * singly linked list.
	 */
	ti = &sc->sc_tinfo[target];
	li = TINFO_LUN(ti, lun);

	/*
	 * We can get as far as the LUN with the IDENTIFY
	 * message.  Check to see if we're running an
	 * untagged command.  Otherwise ack the IDENTIFY
	 * and wait for a tag message.
	 */
	if (li != NULL) {
		if (li->untagged != NULL && li->busy)
			ecb = li->untagged;
		else if (tagtype != MSG_SIMPLE_Q_TAG) {
			/* Wait for tag to come by. */
			sc->sc_state = NCR_IDENTIFIED;
			return (0);
		} else if (tagtype)
			ecb = li->queued[tagid];
	}
	if (ecb == NULL) {
		device_printf(sc->sc_dev, "reselect from target %d lun %d "
		    "tag %x:%x with no nexus; sending ABORT\n",
		    target, lun, tagtype, tagid);
		goto abort;
	}

	/* Make this nexus active again. */
	sc->sc_state = NCR_CONNECTED;
	sc->sc_nexus = ecb;
	ncr53c9x_setsync(sc, ti);

	if (ecb->flags & ECB_RESET)
		ncr53c9x_sched_msgout(SEND_DEV_RESET);
	else if (ecb->flags & ECB_ABORT)
		ncr53c9x_sched_msgout(SEND_ABORT);

	/* Do an implicit RESTORE POINTERS. */
	sc->sc_dp = ecb->daddr;
	sc->sc_dleft = ecb->dleft;

	return (0);

reset:
	ncr53c9x_sched_msgout(SEND_DEV_RESET);
	return (1);

abort:
	ncr53c9x_sched_msgout(SEND_ABORT);
	return (1);
}

/* From NetBSD; these should go into CAM at some point. */
#define	MSG_ISEXTENDED(m)	((m) == MSG_EXTENDED)
#define	MSG_IS1BYTE(m) \
	((!MSG_ISEXTENDED(m) && (m) < 0x20) || MSG_ISIDENTIFY(m))
#define	MSG_IS2BYTE(m)		(((m) & 0xf0) == 0x20)

static inline int
__verify_msg_format(uint8_t *p, int len)
{

	if (len == 1 && MSG_IS1BYTE(p[0]))
		return (1);
	if (len == 2 && MSG_IS2BYTE(p[0]))
		return (1);
	if (len >= 3 && MSG_ISEXTENDED(p[0]) &&
	    len == p[1] + 2)
		return (1);

	return (0);
}

/*
 * Get an incoming message as initiator.
 *
 * The SCSI bus must already be in MESSAGE_IN_PHASE and there is a
 * byte in the FIFO.
 */
static void
ncr53c9x_msgin(struct ncr53c9x_softc *sc)
{
	struct ncr53c9x_ecb *ecb;
	struct ncr53c9x_linfo *li;
	struct ncr53c9x_tinfo *ti;
	uint8_t *pb;
	int len, lun;

	NCR_LOCK_ASSERT(sc, MA_OWNED);

	NCR_TRACE(("[%s(curmsglen:%ld)] ", __func__, (long)sc->sc_imlen));

	if (sc->sc_imlen == 0) {
		device_printf(sc->sc_dev, "msgin: no msg byte available\n");
		return;
	}

	/*
	 * Prepare for a new message.  A message should (according
	 * to the SCSI standard) be transmitted in one single
	 * MESSAGE_IN_PHASE.  If we have been in some other phase,
	 * then this is a new message.
	 */
	if (sc->sc_prevphase != MESSAGE_IN_PHASE &&
	    sc->sc_state != NCR_RESELECTED) {
		device_printf(sc->sc_dev, "phase change, dropping message, "
		    "prev %d, state %d\n", sc->sc_prevphase, sc->sc_state);
		sc->sc_flags &= ~NCR_DROP_MSGI;
		sc->sc_imlen = 0;
	}

	/*
	 * If we're going to reject the message, don't bother storing
	 * the incoming bytes.  But still, we need to ACK them.
	 */
	if ((sc->sc_flags & NCR_DROP_MSGI) != 0) {
		NCRCMD(sc, NCRCMD_MSGOK);
		device_printf(sc->sc_dev, "<dropping msg byte %x>",
		    sc->sc_imess[sc->sc_imlen]);
		return;
	}

	if (sc->sc_imlen >= NCR_MAX_MSG_LEN) {
		ncr53c9x_sched_msgout(SEND_REJECT);
		sc->sc_flags |= NCR_DROP_MSGI;
	} else {
		switch (sc->sc_state) {
		/*
		 * if received message is the first of reselection
		 * then first byte is selid, and then message
		 */
		case NCR_RESELECTED:
			pb = sc->sc_imess + 1;
			len = sc->sc_imlen - 1;
			break;

		default:
			pb = sc->sc_imess;
			len = sc->sc_imlen;
		}

		if (__verify_msg_format(pb, len))
			goto gotit;
	}

	/* Acknowledge what we have so far. */
	NCRCMD(sc, NCRCMD_MSGOK);
	return;

gotit:
	NCR_MSGS(("gotmsg(%x) state %d", sc->sc_imess[0], sc->sc_state));
	/*
	 * We got a complete message, flush the imess.
	 * XXX nobody uses imlen below.
	 */
	sc->sc_imlen = 0;
	/*
	 * Now we should have a complete message (1 byte, 2 byte
	 * and moderately long extended messages).  We only handle
	 * extended messages which total length is shorter than
	 * NCR_MAX_MSG_LEN.  Longer messages will be amputated.
	 */
	switch (sc->sc_state) {
	case NCR_CONNECTED:
		ecb = sc->sc_nexus;
		ti = &sc->sc_tinfo[ecb->ccb->ccb_h.target_id];

		switch (sc->sc_imess[0]) {
		case MSG_CMDCOMPLETE:
			NCR_MSGS(("cmdcomplete "));
			if (sc->sc_dleft < 0) {
				xpt_print_path(ecb->ccb->ccb_h.path);
				printf("got %ld extra bytes\n",
				    -(long)sc->sc_dleft);
				sc->sc_dleft = 0;
			}
			ecb->dleft = (ecb->flags & ECB_TENTATIVE_DONE) ?
			    0 : sc->sc_dleft;
			if ((ecb->flags & ECB_SENSE) == 0)
				ecb->ccb->csio.resid = ecb->dleft;
			sc->sc_state = NCR_CMDCOMPLETE;
			break;

		case MSG_MESSAGE_REJECT:
			NCR_MSGS(("msg reject (msgout=%x) ", sc->sc_msgout));
			switch (sc->sc_msgout) {
			case SEND_TAG:
				/*
				 * Target does not like tagged queuing.
				 *  - Flush the command queue
				 *  - Disable tagged queuing for the target
				 *  - Dequeue ecb from the queued array.
				 */
				device_printf(sc->sc_dev, "tagged queuing "
				    "rejected: target %d\n",
				    ecb->ccb->ccb_h.target_id);

				NCR_MSGS(("(rejected sent tag)"));
				NCRCMD(sc, NCRCMD_FLUSH);
				DELAY(1);
				ti->flags &= ~T_TAG;
				lun = ecb->ccb->ccb_h.target_lun;
				li = TINFO_LUN(ti, lun);
				if (ecb->tag[0] &&
				    li->queued[ecb->tag[1]] != NULL) {
					li->queued[ecb->tag[1]] = NULL;
					li->used--;
				}
				ecb->tag[0] = ecb->tag[1] = 0;
				li->untagged = ecb;
				li->busy = 1;
				break;

			case SEND_SDTR:
				device_printf(sc->sc_dev, "sync transfer "
				    "rejected: target %d\n",
				    ecb->ccb->ccb_h.target_id);

				ti->flags &= ~T_SDTRSENT;
				ti->curr.period = ti->goal.period = 0;
				ti->curr.offset = ti->goal.offset = 0;
				ncr53c9x_setsync(sc, ti);
				break;

			case SEND_WDTR:
				device_printf(sc->sc_dev, "wide transfer "
				    "rejected: target %d\n",
				    ecb->ccb->ccb_h.target_id);

				ti->flags &= ~T_WDTRSENT;
				ti->curr.width = ti->goal.width =
				    MSG_EXT_WDTR_BUS_8_BIT;
				ncr53c9x_setsync(sc, ti);
				break;

			case SEND_INIT_DET_ERR:
				goto abort;
			}
			break;

		case MSG_NOOP:
			NCR_MSGS(("noop "));
			break;

		case MSG_HEAD_OF_Q_TAG:
		case MSG_SIMPLE_Q_TAG:
		case MSG_ORDERED_Q_TAG:
			NCR_MSGS(("TAG %x:%x",
			    sc->sc_imess[0], sc->sc_imess[1]));
			break;

		case MSG_DISCONNECT:
			NCR_MSGS(("disconnect "));
			ti->dconns++;
			sc->sc_state = NCR_DISCONNECT;

			/*
			 * Mark the fact that all bytes have moved.  The
			 * target may not bother to do a SAVE POINTERS
			 * at this stage.  This flag will set the residual
			 * count to zero on MSG COMPLETE.
			 */
			if (sc->sc_dleft == 0)
				ecb->flags |= ECB_TENTATIVE_DONE;
			break;

		case MSG_SAVEDATAPOINTER:
			NCR_MSGS(("save datapointer "));
			ecb->daddr = sc->sc_dp;
			ecb->dleft = sc->sc_dleft;
			break;

		case MSG_RESTOREPOINTERS:
			NCR_MSGS(("restore datapointer "));
			sc->sc_dp = ecb->daddr;
			sc->sc_dleft = ecb->dleft;
			break;

		case MSG_IGN_WIDE_RESIDUE:
			NCR_MSGS(("ignore wide residue (%d bytes)",
			    sc->sc_imess[1]));
			if (sc->sc_imess[1] != 1) {
				xpt_print_path(ecb->ccb->ccb_h.path);
				printf("unexpected MESSAGE IGNORE WIDE "
				    "RESIDUE (%d bytes); sending REJECT\n",
				    sc->sc_imess[1]);
				goto reject;
			}
			/*
			 * If there was a last transfer of an even number of
			 * bytes, wipe the "done" memory and adjust by one
			 * byte (sc->sc_imess[1]).
			 */
			len = sc->sc_dleft - ecb->dleft;
			if (len != 0 && (len & 1) == 0) {
				ecb->flags &= ~ECB_TENTATIVE_DONE;
				sc->sc_dp = (char *)sc->sc_dp - 1;
				sc->sc_dleft--;
			}
			break;

		case MSG_EXTENDED:
			NCR_MSGS(("extended(%x) ", sc->sc_imess[2]));
			switch (sc->sc_imess[2]) {
			case MSG_EXT_SDTR:
				NCR_MSGS(("SDTR period %d, offset %d ",
				    sc->sc_imess[3], sc->sc_imess[4]));
				if (sc->sc_imess[1] != 3)
					goto reject;
				ti->curr.period = sc->sc_imess[3];
				ti->curr.offset = sc->sc_imess[4];
				if (sc->sc_minsync == 0 ||
				    ti->curr.offset == 0 ||
				    ti->curr.period > 124) {
#if 0
#ifdef NCR53C9X_DEBUG
					xpt_print_path(ecb->ccb->ccb_h.path);
					printf("async mode\n");
#endif
#endif
					if ((ti->flags & T_SDTRSENT) == 0) {
						/*
						 * target initiated negotiation
						 */
						ti->curr.offset = 0;
						ncr53c9x_sched_msgout(
						    SEND_SDTR);
					}
				} else {
					ti->curr.period =
					    ncr53c9x_cpb2stp(sc,
					    ncr53c9x_stp2cpb(sc,
					    ti->curr.period));
					if ((ti->flags & T_SDTRSENT) == 0) {
						/*
						 * target initiated negotiation
						 */
						if (ti->curr.period <
						    sc->sc_minsync)
							ti->curr.period =
							    sc->sc_minsync;
						if (ti->curr.offset >
						    sc->sc_maxoffset)
							ti->curr.offset =
							    sc->sc_maxoffset;
						ncr53c9x_sched_msgout(
						    SEND_SDTR);
					}
				}
				ti->flags &= ~T_SDTRSENT;
				ti->goal.period = ti->curr.period;
				ti->goal.offset = ti->curr.offset;
				ncr53c9x_setsync(sc, ti);
				break;

			case MSG_EXT_WDTR:
				NCR_MSGS(("wide mode %d ", sc->sc_imess[3]));
				ti->curr.width = sc->sc_imess[3];
				if (!(ti->flags & T_WDTRSENT))
					/*
					 * target initiated negotiation
					 */
					ncr53c9x_sched_msgout(SEND_WDTR);
				ti->flags &= ~T_WDTRSENT;
				ti->goal.width = ti->curr.width;
				ncr53c9x_setsync(sc, ti);
				break;

			default:
				xpt_print_path(ecb->ccb->ccb_h.path);
				printf("unrecognized MESSAGE EXTENDED 0x%x;"
				    " sending REJECT\n", sc->sc_imess[2]);
				goto reject;
			}
			break;

		default:
			NCR_MSGS(("ident "));
			xpt_print_path(ecb->ccb->ccb_h.path);
			printf("unrecognized MESSAGE 0x%x; sending REJECT\n",
			    sc->sc_imess[0]);
			/* FALLTHROUGH */
		reject:
			ncr53c9x_sched_msgout(SEND_REJECT);
			break;
		}
		break;

	case NCR_IDENTIFIED:
		/*
		 * IDENTIFY message was received and queue tag is expected
		 * now.
		 */
		if ((sc->sc_imess[0] != MSG_SIMPLE_Q_TAG) ||
		    (sc->sc_msgify == 0)) {
			device_printf(sc->sc_dev, "TAG reselect without "
			    "IDENTIFY; MSG %x; sending DEVICE RESET\n",
			    sc->sc_imess[0]);
			goto reset;
		}
		(void)ncr53c9x_reselect(sc, sc->sc_msgify,
		    sc->sc_imess[0], sc->sc_imess[1]);
		break;

	case NCR_RESELECTED:
		if (MSG_ISIDENTIFY(sc->sc_imess[1]))
			sc->sc_msgify = sc->sc_imess[1];
		else {
			device_printf(sc->sc_dev, "reselect without IDENTIFY;"
			    " MSG %x; sending DEVICE RESET\n", sc->sc_imess[1]);
			goto reset;
		}
		(void)ncr53c9x_reselect(sc, sc->sc_msgify, 0, 0);
		break;

	default:
		device_printf(sc->sc_dev, "unexpected MESSAGE IN; "
		    "sending DEVICE RESET\n");
		/* FALLTHROUGH */
	reset:
		ncr53c9x_sched_msgout(SEND_DEV_RESET);
		break;

	abort:
		ncr53c9x_sched_msgout(SEND_ABORT);
	}

	/* If we have more messages to send set ATN. */
	if (sc->sc_msgpriq) {
		NCRCMD(sc, NCRCMD_SETATN);
		sc->sc_flags |= NCR_ATN;
	}

	/* Acknowledge last message byte. */
	NCRCMD(sc, NCRCMD_MSGOK);

	/* Done, reset message pointer. */
	sc->sc_flags &= ~NCR_DROP_MSGI;
	sc->sc_imlen = 0;
}

/*
 * Send the highest priority, scheduled message.
 */
static void
ncr53c9x_msgout(struct ncr53c9x_softc *sc)
{
	struct ncr53c9x_tinfo *ti;
	struct ncr53c9x_ecb *ecb;
	size_t size;
	int error;
#ifdef NCR53C9X_DEBUG
	int i;
#endif

	NCR_LOCK_ASSERT(sc, MA_OWNED);

	NCR_TRACE(("[%s(priq:%x, prevphase:%x)]", __func__, sc->sc_msgpriq,
	    sc->sc_prevphase));

	/*
	 * XXX - the NCR_ATN flag is not in sync with the actual ATN
	 *	 condition on the SCSI bus.  The 53c9x chip
	 *	 automatically turns off ATN before sending the
	 *	 message byte.  (See also the comment below in the
	 *	 default case when picking out a message to send.)
	 */
	if (sc->sc_flags & NCR_ATN) {
		if (sc->sc_prevphase != MESSAGE_OUT_PHASE) {
		new:
			NCRCMD(sc, NCRCMD_FLUSH);
#if 0
			DELAY(1);
#endif
			sc->sc_msgoutq = 0;
			sc->sc_omlen = 0;
		}
	} else {
		if (sc->sc_prevphase == MESSAGE_OUT_PHASE) {
			ncr53c9x_sched_msgout(sc->sc_msgoutq);
			goto new;
		} else
			device_printf(sc->sc_dev, "at line %d: unexpected "
			    "MESSAGE OUT phase\n", __LINE__);
	}

	if (sc->sc_omlen == 0) {
		/* Pick up highest priority message. */
		sc->sc_msgout = sc->sc_msgpriq & -sc->sc_msgpriq;
		sc->sc_msgoutq |= sc->sc_msgout;
		sc->sc_msgpriq &= ~sc->sc_msgout;
		sc->sc_omlen = 1;		/* "Default" message len */
		switch (sc->sc_msgout) {
		case SEND_SDTR:
			ecb = sc->sc_nexus;
			ti = &sc->sc_tinfo[ecb->ccb->ccb_h.target_id];
			sc->sc_omess[0] = MSG_EXTENDED;
			sc->sc_omess[1] = MSG_EXT_SDTR_LEN;
			sc->sc_omess[2] = MSG_EXT_SDTR;
			sc->sc_omess[3] = ti->goal.period;
			sc->sc_omess[4] = ti->goal.offset;
			sc->sc_omlen = 5;
			break;

		case SEND_WDTR:
			ecb = sc->sc_nexus;
			ti = &sc->sc_tinfo[ecb->ccb->ccb_h.target_id];
			sc->sc_omess[0] = MSG_EXTENDED;
			sc->sc_omess[1] = MSG_EXT_WDTR_LEN;
			sc->sc_omess[2] = MSG_EXT_WDTR;
			sc->sc_omess[3] = ti->goal.width;
			sc->sc_omlen = 4;
			break;

		case SEND_IDENTIFY:
			if (sc->sc_state != NCR_CONNECTED)
				device_printf(sc->sc_dev, "at line %d: no "
				    "nexus\n", __LINE__);
			ecb = sc->sc_nexus;
			sc->sc_omess[0] =
			    MSG_IDENTIFY(ecb->ccb->ccb_h.target_lun, 0);
			break;

		case SEND_TAG:
			if (sc->sc_state != NCR_CONNECTED)
				device_printf(sc->sc_dev, "at line %d: no "
				    "nexus\n", __LINE__);
			ecb = sc->sc_nexus;
			sc->sc_omess[0] = ecb->tag[0];
			sc->sc_omess[1] = ecb->tag[1];
			sc->sc_omlen = 2;
			break;

		case SEND_DEV_RESET:
			sc->sc_flags |= NCR_ABORTING;
			sc->sc_omess[0] = MSG_BUS_DEV_RESET;
			ecb = sc->sc_nexus;
			ti = &sc->sc_tinfo[ecb->ccb->ccb_h.target_id];
			ti->curr.period = 0;
			ti->curr.offset = 0;
			ti->curr.width = MSG_EXT_WDTR_BUS_8_BIT;
			break;

		case SEND_PARITY_ERROR:
			sc->sc_omess[0] = MSG_PARITY_ERROR;
			break;

		case SEND_ABORT:
			sc->sc_flags |= NCR_ABORTING;
			sc->sc_omess[0] = MSG_ABORT;
			break;

		case SEND_INIT_DET_ERR:
			sc->sc_omess[0] = MSG_INITIATOR_DET_ERR;
			break;

		case SEND_REJECT:
			sc->sc_omess[0] = MSG_MESSAGE_REJECT;
			break;

		default:
			/*
			 * We normally do not get here, since the chip
			 * automatically turns off ATN before the last
			 * byte of a message is sent to the target.
			 * However, if the target rejects our (multi-byte)
			 * message early by switching to MSG IN phase
			 * ATN remains on, so the target may return to
			 * MSG OUT phase.  If there are no scheduled messages
			 * left we send a NO-OP.
			 *
			 * XXX - Note that this leaves no useful purpose for
			 * the NCR_ATN flag.
			 */
			sc->sc_flags &= ~NCR_ATN;
			sc->sc_omess[0] = MSG_NOOP;
		}
		sc->sc_omp = sc->sc_omess;
	}

#ifdef NCR53C9X_DEBUG
	if ((ncr53c9x_debug & NCR_SHOWMSGS) != 0) {
		NCR_MSGS(("<msgout:"));
		for (i = 0; i < sc->sc_omlen; i++)
			NCR_MSGS((" %02x", sc->sc_omess[i]));
		NCR_MSGS(("> "));
	}
#endif

	if (sc->sc_rev != NCR_VARIANT_FAS366) {
		/* (Re)send the message. */
		size = ulmin(sc->sc_omlen, sc->sc_maxxfer);
		error = NCRDMA_SETUP(sc, &sc->sc_omp, &sc->sc_omlen, 0, &size);
		if (error != 0)
			goto cmd;

		/* Program the SCSI counter. */
		NCR_SET_COUNT(sc, size);

		/* Load the count in and start the message-out transfer. */
		NCRCMD(sc, NCRCMD_NOP | NCRCMD_DMA);
		NCRCMD(sc, NCRCMD_TRANS | NCRCMD_DMA);
		NCRDMA_GO(sc);
		return;
	}

cmd:
	/*
	 * XXX FIFO size
	 */
	sc->sc_cmdlen = 0;
	ncr53c9x_flushfifo(sc);
	ncr53c9x_wrfifo(sc, sc->sc_omp, sc->sc_omlen);
	NCRCMD(sc, NCRCMD_TRANS);
}

void
ncr53c9x_intr(void *arg)
{
	struct ncr53c9x_softc *sc = arg;

	if (!NCRDMA_ISINTR(sc))
		return;

	NCR_LOCK(sc);

	ncr53c9x_intr1(sc);

	NCR_UNLOCK(sc);
}

/*
 * This is the most critical part of the driver, and has to know
 * how to deal with *all* error conditions and phases from the SCSI
 * bus.  If there are no errors and the DMA was active, then call the
 * DMA pseudo-interrupt handler.  If this returns 1, then that was it
 * and we can return from here without further processing.
 *
 * Most of this needs verifying.
 */
static void
ncr53c9x_intr1(struct ncr53c9x_softc *sc)
{
	struct ncr53c9x_ecb *ecb;
	struct ncr53c9x_linfo *li;
	struct ncr53c9x_tinfo *ti;
	struct timeval cur, wait;
	size_t size;
	int error, i, nfifo;
	uint8_t msg;

	NCR_LOCK_ASSERT(sc, MA_OWNED);

	NCR_INTS(("[ncr53c9x_intr: state %d]", sc->sc_state));

again:
	/* and what do the registers say... */
	ncr53c9x_readregs(sc);

	/*
	 * At the moment, only a SCSI Bus Reset or Illegal
	 * Command are classed as errors.  A disconnect is a
	 * valid condition, and we let the code check is the
	 * "NCR_BUSFREE_OK" flag was set before declaring it
	 * and error.
	 *
	 * Also, the status register tells us about "Gross
	 * Errors" and "Parity errors".  Only the Gross Error
	 * is really bad, and the parity errors are dealt
	 * with later.
	 *
	 * TODO
	 *	If there are too many parity error, go to slow
	 *	cable mode?
	 */

	if ((sc->sc_espintr & NCRINTR_SBR) != 0) {
		if ((NCR_READ_REG(sc, NCR_FFLAG) & NCRFIFO_FF) != 0) {
			NCRCMD(sc, NCRCMD_FLUSH);
			DELAY(1);
		}
		if (sc->sc_state != NCR_SBR) {
			device_printf(sc->sc_dev, "SCSI bus reset\n");
			ncr53c9x_init(sc, 0);	/* Restart everything. */
			return;
		}
#if 0
/*XXX*/		device_printf(sc->sc_dev, "<expected bus reset: "
		    "[intr %x, stat %x, step %d]>\n",
		    sc->sc_espintr, sc->sc_espstat, sc->sc_espstep);
#endif
		if (sc->sc_nexus != NULL)
			panic("%s: nexus in reset state",
			    device_get_nameunit(sc->sc_dev));
		goto sched;
	}

	ecb = sc->sc_nexus;

#define	NCRINTR_ERR (NCRINTR_SBR | NCRINTR_ILL)
	if (sc->sc_espintr & NCRINTR_ERR ||
	    sc->sc_espstat & NCRSTAT_GE) {
		if ((sc->sc_espstat & NCRSTAT_GE) != 0) {
			/* Gross Error; no target? */
			if (NCR_READ_REG(sc, NCR_FFLAG) & NCRFIFO_FF) {
				NCRCMD(sc, NCRCMD_FLUSH);
				DELAY(1);
			}
			if (sc->sc_state == NCR_CONNECTED ||
			    sc->sc_state == NCR_SELECTING) {
				ecb->ccb->ccb_h.status = CAM_SEL_TIMEOUT;
				ncr53c9x_done(sc, ecb);
			}
			return;
		}

		if ((sc->sc_espintr & NCRINTR_ILL) != 0) {
			if ((sc->sc_flags & NCR_EXPECT_ILLCMD) != 0) {
				/*
				 * Eat away "Illegal command" interrupt
				 * on a ESP100 caused by a re-selection
				 * while we were trying to select
				 * another target.
				 */
#ifdef NCR53C9X_DEBUG
				device_printf(sc->sc_dev, "ESP100 work-around "
				    "activated\n");
#endif
				sc->sc_flags &= ~NCR_EXPECT_ILLCMD;
				return;
			}
			/* Illegal command, out of sync? */
			device_printf(sc->sc_dev, "illegal command: 0x%x "
			    "(state %d, phase %x, prevphase %x)\n",
			    sc->sc_lastcmd,
			    sc->sc_state, sc->sc_phase, sc->sc_prevphase);
			if (NCR_READ_REG(sc, NCR_FFLAG) & NCRFIFO_FF) {
				NCRCMD(sc, NCRCMD_FLUSH);
				DELAY(1);
			}
			goto reset;
		}
	}
	sc->sc_flags &= ~NCR_EXPECT_ILLCMD;

	/*
	 * Call if DMA is active.
	 *
	 * If DMA_INTR returns true, then maybe go 'round the loop
	 * again in case there is no more DMA queued, but a phase
	 * change is expected.
	 */
	if (NCRDMA_ISACTIVE(sc)) {
		if (NCRDMA_INTR(sc) == -1) {
			device_printf(sc->sc_dev, "DMA error; resetting\n");
			goto reset;
		}
		/* If DMA active here, then go back to work... */
		if (NCRDMA_ISACTIVE(sc))
			return;

		if ((sc->sc_espstat & NCRSTAT_TC) == 0) {
			/*
			 * DMA not completed.  If we can not find a
			 * acceptable explanation, print a diagnostic.
			 */
			if (sc->sc_state == NCR_SELECTING)
				/*
				 * This can happen if we are reselected
				 * while using DMA to select a target.
				 */
				/*void*/;
			else if (sc->sc_prevphase == MESSAGE_OUT_PHASE) {
				/*
				 * Our (multi-byte) message (eg SDTR) was
				 * interrupted by the target to send
				 * a MSG REJECT.
				 * Print diagnostic if current phase
				 * is not MESSAGE IN.
				 */
				if (sc->sc_phase != MESSAGE_IN_PHASE)
					device_printf(sc->sc_dev,"!TC on MSGOUT"
					    " [intr %x, stat %x, step %d]"
					    " prevphase %x, resid %lx\n",
					    sc->sc_espintr,
					    sc->sc_espstat,
					    sc->sc_espstep,
					    sc->sc_prevphase,
					    (u_long)sc->sc_omlen);
			} else if (sc->sc_dleft == 0) {
				/*
				 * The DMA operation was started for
				 * a DATA transfer.  Print a diagnostic
				 * if the DMA counter and TC bit
				 * appear to be out of sync.
				 *
				 * XXX This is fatal and usually means that
				 *     the DMA engine is hopelessly out of
				 *     sync with reality.  A disk is likely
				 *     getting spammed at this point.
				 */
				device_printf(sc->sc_dev, "!TC on DATA XFER"
				    " [intr %x, stat %x, step %d]"
				    " prevphase %x, resid %x\n",
				    sc->sc_espintr,
				    sc->sc_espstat,
				    sc->sc_espstep,
				    sc->sc_prevphase,
				    ecb ? ecb->dleft : -1);
				goto reset;
			}
		}
	}

	/*
	 * Check for less serious errors.
	 */
	if ((sc->sc_espstat & NCRSTAT_PE) != 0) {
		device_printf(sc->sc_dev, "SCSI bus parity error\n");
		if (sc->sc_prevphase == MESSAGE_IN_PHASE)
			ncr53c9x_sched_msgout(SEND_PARITY_ERROR);
		else
			ncr53c9x_sched_msgout(SEND_INIT_DET_ERR);
	}

	if ((sc->sc_espintr & NCRINTR_DIS) != 0) {
		sc->sc_msgify = 0;
		NCR_INTS(("<DISC [intr %x, stat %x, step %d]>",
		    sc->sc_espintr,sc->sc_espstat,sc->sc_espstep));
		if (NCR_READ_REG(sc, NCR_FFLAG) & NCRFIFO_FF) {
			NCRCMD(sc, NCRCMD_FLUSH);
#if 0
			DELAY(1);
#endif
		}
		/*
		 * This command must (apparently) be issued within
		 * 250mS of a disconnect.  So here you are...
		 */
		NCRCMD(sc, NCRCMD_ENSEL);

		switch (sc->sc_state) {
		case NCR_RESELECTED:
			goto sched;

		case NCR_SELECTING:
			ecb->ccb->ccb_h.status = CAM_SEL_TIMEOUT;

			/* Selection timeout -- discard all LUNs if empty. */
			ti = &sc->sc_tinfo[ecb->ccb->ccb_h.target_id];
			li = LIST_FIRST(&ti->luns);
			while (li != NULL) {
				if (li->untagged == NULL && li->used == 0) {
					if (li->lun < NCR_NLUN)
						ti->lun[li->lun] = NULL;
					LIST_REMOVE(li, link);
					free(li, M_DEVBUF);
					/*
					 * Restart the search at the beginning.
					 */
					li = LIST_FIRST(&ti->luns);
					continue;
				}
				li = LIST_NEXT(li, link);
			}
			goto finish;

		case NCR_CONNECTED:
			if (ecb != NULL) {
				ti = &sc->sc_tinfo[ecb->ccb->ccb_h.target_id];
				if ((ti->flags & T_SDTRSENT) != 0) {
					xpt_print_path(ecb->ccb->ccb_h.path);
					printf("sync nego not completed!\n");
					ti->flags &= ~T_SDTRSENT;
					ti->curr.period = ti->goal.period = 0;
					ti->curr.offset = ti->goal.offset = 0;
					ncr53c9x_setsync(sc, ti);
				}
				if ((ti->flags & T_WDTRSENT) != 0) {
					xpt_print_path(ecb->ccb->ccb_h.path);
					printf("wide nego not completed!\n");
					ti->flags &= ~T_WDTRSENT;
					ti->curr.width = ti->goal.width =
					    MSG_EXT_WDTR_BUS_8_BIT;
					ncr53c9x_setsync(sc, ti);
				}
			}

			/* It may be OK to disconnect. */
			if ((sc->sc_flags & NCR_ABORTING) == 0) {
				/*
				 * Section 5.1.1 of the SCSI 2 spec
				 * suggests issuing a REQUEST SENSE
				 * following an unexpected disconnect.
				 * Some devices go into a contingent
				 * allegiance condition when
				 * disconnecting, and this is necessary
				 * to clean up their state.
				 */
				device_printf(sc->sc_dev, "unexpected "
				    "disconnect [state %d, intr %x, stat %x, "
				    "phase(c %x, p %x)]; ", sc->sc_state,
				    sc->sc_espintr, sc->sc_espstat,
				    sc->sc_phase, sc->sc_prevphase);

				/*
				 * XXX This will cause a chip reset and will
				 *     prevent us from finding out the real
				 *     problem with the device.  However, it's
				 *     necessary until a way can be found to
				 *     safely cancel the DMA that is in
				 *     progress.
				 */
				if (1 || (ecb->flags & ECB_SENSE) != 0) {
					printf("resetting\n");
					goto reset;
				}
				printf("sending REQUEST SENSE\n");
				callout_stop(&ecb->ch);
				ncr53c9x_sense(sc, ecb);
				return;
			} else if (ecb != NULL &&
			    (ecb->flags & ECB_RESET) != 0) {
				ecb->ccb->ccb_h.status = CAM_REQ_CMP;
				goto finish;
			}

			ecb->ccb->ccb_h.status = CAM_CMD_TIMEOUT;
			goto finish;

		case NCR_DISCONNECT:
			sc->sc_nexus = NULL;
			goto sched;

		case NCR_CMDCOMPLETE:
			ecb->ccb->ccb_h.status = CAM_REQ_CMP;
			goto finish;
		}
	}

	switch (sc->sc_state) {
	case NCR_SBR:
		device_printf(sc->sc_dev, "waiting for Bus Reset to happen\n");
		return;

	case NCR_RESELECTED:
		/*
		 * We must be continuing a message?
		 */
		device_printf(sc->sc_dev, "unhandled reselect continuation, "
		    "state %d, intr %02x\n", sc->sc_state, sc->sc_espintr);
		goto reset;
		break;

	case NCR_IDENTIFIED:
		ecb = sc->sc_nexus;
		if (sc->sc_phase != MESSAGE_IN_PHASE) {
			i = NCR_READ_REG(sc, NCR_FFLAG) & NCRFIFO_FF;
			/*
			 * Things are seriously screwed up.
			 * Pull the brakes, i.e. reset.
			 */
			device_printf(sc->sc_dev, "target didn't send tag: %d "
			    "bytes in FIFO\n", i);
			/* Drain and display FIFO. */
			while (i-- > 0)
				printf("[%d] ", NCR_READ_REG(sc, NCR_FIFO));

			goto reset;
		} else
			goto msgin;

	case NCR_IDLE:
	case NCR_SELECTING:
		ecb = sc->sc_nexus;
		if (sc->sc_espintr & NCRINTR_RESEL) {
			sc->sc_msgpriq = sc->sc_msgout = sc->sc_msgoutq = 0;
			sc->sc_flags = 0;
			/*
			 * If we're trying to select a
			 * target ourselves, push our command
			 * back into the ready list.
			 */
			if (sc->sc_state == NCR_SELECTING) {
				NCR_INTS(("backoff selector "));
				callout_stop(&ecb->ch);
				ncr53c9x_dequeue(sc, ecb);
				TAILQ_INSERT_HEAD(&sc->ready_list, ecb, chain);
				ecb->flags |= ECB_READY;
				ecb = sc->sc_nexus = NULL;
			}
			sc->sc_state = NCR_RESELECTED;
			if (sc->sc_phase != MESSAGE_IN_PHASE) {
				/*
				 * Things are seriously screwed up.
				 * Pull the brakes, i.e. reset
				 */
				device_printf(sc->sc_dev, "target didn't "
				    "identify\n");
				goto reset;
			}
			/*
			 * The C90 only inhibits FIFO writes until reselection
			 * is complete instead of waiting until the interrupt
			 * status register has been read.  So, if the reselect
			 * happens while we were entering command bytes (for
			 * another target) some of those bytes can appear in
			 * the FIFO here, after the interrupt is taken.
			 *
			 * To remedy this situation, pull the Selection ID
			 * and Identify message from the FIFO directly, and
			 * ignore any extraneous FIFO contents.  Also, set
			 * a flag that allows one Illegal Command Interrupt
			 * to occur which the chip also generates as a result
			 * of writing to the FIFO during a reselect.
			 */
			if (sc->sc_rev == NCR_VARIANT_ESP100) {
				nfifo =
				    NCR_READ_REG(sc, NCR_FFLAG) & NCRFIFO_FF;
				sc->sc_imess[0] = NCR_READ_REG(sc, NCR_FIFO);
				sc->sc_imess[1] = NCR_READ_REG(sc, NCR_FIFO);
				sc->sc_imlen = 2;
				if (nfifo != 2) {
					/* Flush the rest. */
					NCRCMD(sc, NCRCMD_FLUSH);
				}
				sc->sc_flags |= NCR_EXPECT_ILLCMD;
				if (nfifo > 2)
					nfifo = 2;	/* We fixed it... */
			} else
				nfifo = ncr53c9x_rdfifo(sc, NCR_RDFIFO_START);

			if (nfifo != 2) {
				device_printf(sc->sc_dev, "RESELECT: %d bytes "
				    "in FIFO! [intr %x, stat %x, step %d, "
				    "prevphase %x]\n",
				    nfifo,
				    sc->sc_espintr,
				    sc->sc_espstat,
				    sc->sc_espstep,
				    sc->sc_prevphase);
				goto reset;
			}
			sc->sc_selid = sc->sc_imess[0];
			NCR_INTS(("selid=%02x ", sc->sc_selid));

			/* Handle IDENTIFY message. */
			ncr53c9x_msgin(sc);

			if (sc->sc_state != NCR_CONNECTED &&
			    sc->sc_state != NCR_IDENTIFIED) {
				/* IDENTIFY fail?! */
				device_printf(sc->sc_dev, "identify failed, "
				    "state %d, intr %02x\n", sc->sc_state,
				    sc->sc_espintr);
				goto reset;
			}
			goto shortcut;	/* i.e. next phase expected soon */
		}

#define	NCRINTR_DONE	(NCRINTR_FC | NCRINTR_BS)
		if ((sc->sc_espintr & NCRINTR_DONE) == NCRINTR_DONE) {
			/*
			 * Arbitration won; examine the `step' register
			 * to determine how far the selection could progress.
			 */
			if (ecb == NULL) {
				/*
				 * When doing path inquiry during boot
				 * FAS100A trigger a stray interrupt which
				 * we just ignore instead of panicing.
				 */
				if (sc->sc_state == NCR_IDLE &&
				    sc->sc_espstep == 0)
					return;
				panic("%s: no nexus", __func__);
			}

			ti = &sc->sc_tinfo[ecb->ccb->ccb_h.target_id];

			switch (sc->sc_espstep) {
			case 0:
				/*
				 * The target did not respond with a
				 * message out phase - probably an old
				 * device that doesn't recognize ATN.
				 * Clear ATN and just continue, the
				 * target should be in the command
				 * phase.
				 * XXX check for command phase?
				 */
				NCRCMD(sc, NCRCMD_RSTATN);
				break;

			case 1:
				if (ti->curr.period == ti->goal.period &&
				    ti->curr.offset == ti->goal.offset &&
				    ti->curr.width == ti->goal.width &&
				    ecb->tag[0] == 0) {
					device_printf(sc->sc_dev, "step 1 "
					    "and no negotiation to perform "
					    "or tag to send\n");
					goto reset;
				}
				if (sc->sc_phase != MESSAGE_OUT_PHASE) {
					device_printf(sc->sc_dev, "step 1 "
					    "but not in MESSAGE_OUT_PHASE\n");
					goto reset;
				}
				sc->sc_prevphase = MESSAGE_OUT_PHASE; /* XXX */
				if (ecb->flags & ECB_RESET) {
					/*
					 * A DEVICE RESET was scheduled and
					 * ATNS used.  As SEND_DEV_RESET has
					 * the highest priority, the target
					 * will reset and disconnect and we
					 * will end up in ncr53c9x_done w/o
					 * negotiating or sending a TAG.  So
					 * we just break here in order to
					 * avoid warnings about negotiation
					 * not having completed.
					 */
					ncr53c9x_sched_msgout(SEND_DEV_RESET);
					break;
				}
				if (ti->curr.width != ti->goal.width) {
					ti->flags |= T_WDTRSENT | T_SDTRSENT;
					ncr53c9x_sched_msgout(SEND_WDTR |
					    SEND_SDTR);
				}
				if (ti->curr.period != ti->goal.period ||
				    ti->curr.offset != ti->goal.offset) {
					ti->flags |= T_SDTRSENT;
					ncr53c9x_sched_msgout(SEND_SDTR);
				}
				if (ecb->tag[0] != 0)
					/* Could not do ATN3 so send TAG. */
					ncr53c9x_sched_msgout(SEND_TAG);
				break;

			case 3:
				/*
				 * Grr, this is supposed to mean
				 * "target left command phase prematurely".
				 * It seems to happen regularly when
				 * sync mode is on.
				 * Look at FIFO to see if command went out.
				 * (Timing problems?)
				 */
				if (sc->sc_features & NCR_F_DMASELECT) {
					if (sc->sc_cmdlen == 0) {
						/* Hope for the best... */
						break;
					}
				} else if ((NCR_READ_REG(sc, NCR_FFLAG) &
				    NCRFIFO_FF) == 0) {
					/* Hope for the best... */
					break;
				}
				xpt_print_path(ecb->ccb->ccb_h.path);
				printf("selection failed; %d left in FIFO "
				    "[intr %x, stat %x, step %d]\n",
				    NCR_READ_REG(sc, NCR_FFLAG) & NCRFIFO_FF,
				    sc->sc_espintr, sc->sc_espstat,
				    sc->sc_espstep);
				NCRCMD(sc, NCRCMD_FLUSH);
				ncr53c9x_sched_msgout(SEND_ABORT);
				return;

			case 2:
				/* Select stuck at Command Phase. */
				NCRCMD(sc, NCRCMD_FLUSH);
				break;

			case 4:
				if (sc->sc_features & NCR_F_DMASELECT &&
				    sc->sc_cmdlen != 0) {
					xpt_print_path(ecb->ccb->ccb_h.path);
					printf("select; %lu left in DMA buffer "
					    "[intr %x, stat %x, step %d]\n",
					    (u_long)sc->sc_cmdlen,
					    sc->sc_espintr,
					    sc->sc_espstat,
					    sc->sc_espstep);
				}
				/* So far, everything went fine. */
				break;
			}

			sc->sc_prevphase = INVALID_PHASE;	/* ??? */
			/* Do an implicit RESTORE POINTERS. */
			sc->sc_dp = ecb->daddr;
			sc->sc_dleft = ecb->dleft;
			sc->sc_state = NCR_CONNECTED;
			break;
		} else {
			device_printf(sc->sc_dev, "unexpected status after "
			    "select: [intr %x, stat %x, step %x]\n",
			    sc->sc_espintr, sc->sc_espstat, sc->sc_espstep);
			NCRCMD(sc, NCRCMD_FLUSH);
			DELAY(1);
			goto reset;
		}
		if (sc->sc_state == NCR_IDLE) {
			device_printf(sc->sc_dev, "stray interrupt\n");
			return;
		}
		break;

	case NCR_CONNECTED:
		if ((sc->sc_flags & NCR_ICCS) != 0) {
			/* "Initiate Command Complete Steps" in progress */
			sc->sc_flags &= ~NCR_ICCS;

			if ((sc->sc_espintr & NCRINTR_DONE) == 0) {
				device_printf(sc->sc_dev, "ICCS: "
				    ": [intr %x, stat %x, step %x]\n",
				    sc->sc_espintr, sc->sc_espstat,
				    sc->sc_espstep);
			}
			ncr53c9x_rdfifo(sc, NCR_RDFIFO_START);
			if (sc->sc_imlen < 2)
				device_printf(sc->sc_dev, "can't get status, "
				    "only %d bytes\n", (int)sc->sc_imlen);
			ecb->stat = sc->sc_imess[sc->sc_imlen - 2];
			msg = sc->sc_imess[sc->sc_imlen - 1];
			NCR_PHASE(("<stat:(%x,%x)>", ecb->stat, msg));
			if (msg == MSG_CMDCOMPLETE) {
				ecb->dleft =
				    (ecb->flags & ECB_TENTATIVE_DONE) ?
				    0 : sc->sc_dleft;
				if ((ecb->flags & ECB_SENSE) == 0)
					ecb->ccb->csio.resid = ecb->dleft;
				sc->sc_state = NCR_CMDCOMPLETE;
			} else
				device_printf(sc->sc_dev, "STATUS_PHASE: "
				    "msg %d\n", msg);
			sc->sc_imlen = 0;
			NCRCMD(sc, NCRCMD_MSGOK);
			goto shortcut;	/* i.e. wait for disconnect */
		}
		break;

	default:
		device_printf(sc->sc_dev, "invalid state: %d [intr %x, "
		    "phase(c %x, p %x)]\n", sc->sc_state,
		    sc->sc_espintr, sc->sc_phase, sc->sc_prevphase);
		goto reset;
	}

	/*
	 * Driver is now in state NCR_CONNECTED, i.e. we
	 * have a current command working the SCSI bus.
	 */
	if (sc->sc_state != NCR_CONNECTED || ecb == NULL)
		panic("%s: no nexus", __func__);

	switch (sc->sc_phase) {
	case MESSAGE_OUT_PHASE:
		NCR_PHASE(("MESSAGE_OUT_PHASE "));
		ncr53c9x_msgout(sc);
		sc->sc_prevphase = MESSAGE_OUT_PHASE;
		break;

	case MESSAGE_IN_PHASE:
msgin:
		NCR_PHASE(("MESSAGE_IN_PHASE "));
		if ((sc->sc_espintr & NCRINTR_BS) != 0) {
			if ((sc->sc_rev != NCR_VARIANT_FAS366) ||
			    (sc->sc_espstat2 & NCRFAS_STAT2_EMPTY) == 0) {
				NCRCMD(sc, NCRCMD_FLUSH);
			}
			sc->sc_flags |= NCR_WAITI;
			NCRCMD(sc, NCRCMD_TRANS);
		} else if ((sc->sc_espintr & NCRINTR_FC) != 0) {
			if ((sc->sc_flags & NCR_WAITI) == 0) {
				device_printf(sc->sc_dev, "MSGIN: unexpected "
				    "FC bit: [intr %x, stat %x, step %x]\n",
				    sc->sc_espintr, sc->sc_espstat,
				    sc->sc_espstep);
			}
			sc->sc_flags &= ~NCR_WAITI;
			ncr53c9x_rdfifo(sc,
			    (sc->sc_prevphase == sc->sc_phase) ?
			    NCR_RDFIFO_CONTINUE : NCR_RDFIFO_START);
			ncr53c9x_msgin(sc);
		} else
			device_printf(sc->sc_dev, "MSGIN: weird bits: "
			    "[intr %x, stat %x, step %x]\n",
			    sc->sc_espintr, sc->sc_espstat, sc->sc_espstep);
		sc->sc_prevphase = MESSAGE_IN_PHASE;
		goto shortcut;	/* i.e. expect data to be ready */

	case COMMAND_PHASE:
		/*
		 * Send the command block.  Normally we don't see this
		 * phase because the SEL_ATN command takes care of
		 * all this.  However, we end up here if either the
		 * target or we wanted to exchange some more messages
		 * first (e.g. to start negotiations).
		 */

		NCR_PHASE(("COMMAND_PHASE 0x%02x (%d) ",
		    ecb->cmd.cmd.opcode, ecb->clen));
		if (NCR_READ_REG(sc, NCR_FFLAG) & NCRFIFO_FF) {
			NCRCMD(sc, NCRCMD_FLUSH);
#if 0
			DELAY(1);
#endif
		}
		/*
		 * If we have more messages to send, e.g. WDTR or SDTR
		 * after we've sent a TAG, set ATN so we'll go back to
		 * MESSAGE_OUT_PHASE.
		 */
		if (sc->sc_msgpriq) {
			NCRCMD(sc, NCRCMD_SETATN);
			sc->sc_flags |= NCR_ATN;
		}
		if (sc->sc_features & NCR_F_DMASELECT) {
			/* Setup DMA transfer for command. */
			size = ecb->clen;
			sc->sc_cmdlen = size;
			sc->sc_cmdp = (void *)&ecb->cmd.cmd;
			error = NCRDMA_SETUP(sc, &sc->sc_cmdp, &sc->sc_cmdlen,
			    0, &size);
			if (error != 0)
				goto cmd;

			/* Program the SCSI counter. */
			NCR_SET_COUNT(sc, size);

			/* Load the count in. */
			NCRCMD(sc, NCRCMD_NOP | NCRCMD_DMA);

			/* Start the command transfer. */
			NCRCMD(sc, NCRCMD_TRANS | NCRCMD_DMA);
			NCRDMA_GO(sc);
			sc->sc_prevphase = COMMAND_PHASE;
			break;
		}
cmd:
		sc->sc_cmdlen = 0;
		ncr53c9x_wrfifo(sc, (uint8_t *)&ecb->cmd.cmd, ecb->clen);
		NCRCMD(sc, NCRCMD_TRANS);
		sc->sc_prevphase = COMMAND_PHASE;
		break;

	case DATA_OUT_PHASE:
		NCR_PHASE(("DATA_OUT_PHASE [%ld] ", (long)sc->sc_dleft));
		sc->sc_prevphase = DATA_OUT_PHASE;
		NCRCMD(sc, NCRCMD_FLUSH);
		size = ulmin(sc->sc_dleft, sc->sc_maxxfer);
		error = NCRDMA_SETUP(sc, &sc->sc_dp, &sc->sc_dleft, 0, &size);
		goto setup_xfer;

	case DATA_IN_PHASE:
		NCR_PHASE(("DATA_IN_PHASE "));
		sc->sc_prevphase = DATA_IN_PHASE;
		if (sc->sc_rev == NCR_VARIANT_ESP100)
			NCRCMD(sc, NCRCMD_FLUSH);
		size = ulmin(sc->sc_dleft, sc->sc_maxxfer);
		error = NCRDMA_SETUP(sc, &sc->sc_dp, &sc->sc_dleft, 1, &size);
setup_xfer:
		if (error != 0) {
			switch (error) {
			case EFBIG:
				ecb->ccb->ccb_h.status |= CAM_REQ_TOO_BIG;
				break;
			case EINPROGRESS:
				panic("%s: cannot deal with deferred DMA",
				    __func__);
			case EINVAL:
				ecb->ccb->ccb_h.status |= CAM_REQ_INVALID;
				break;
			case ENOMEM:
				ecb->ccb->ccb_h.status |= CAM_REQUEUE_REQ;
				break;
			default:
				ecb->ccb->ccb_h.status |= CAM_REQ_CMP_ERR;
			}
			goto finish;
		}

		/* Target returned to data phase: wipe "done" memory. */
		ecb->flags &= ~ECB_TENTATIVE_DONE;

		/* Program the SCSI counter. */
		NCR_SET_COUNT(sc, size);

		/* Load the count in. */
		NCRCMD(sc, NCRCMD_NOP | NCRCMD_DMA);

		/*
		 * Note that if `size' is 0, we've already transceived
		 * all the bytes we want but we're still in DATA PHASE.
		 * Apparently, the device needs padding.  Also, a
		 * transfer size of 0 means "maximum" to the chip
		 * DMA logic.
		 */
		NCRCMD(sc,
		    (size == 0 ? NCRCMD_TRPAD : NCRCMD_TRANS) | NCRCMD_DMA);
		NCRDMA_GO(sc);
		return;

	case STATUS_PHASE:
		NCR_PHASE(("STATUS_PHASE "));
		sc->sc_flags |= NCR_ICCS;
		NCRCMD(sc, NCRCMD_ICCS);
		sc->sc_prevphase = STATUS_PHASE;
		goto shortcut;	/* i.e. expect status results soon */

	case INVALID_PHASE:
		break;

	default:
		device_printf(sc->sc_dev,
		    "unexpected bus phase; resetting\n");
		goto reset;
	}

	return;

reset:
	ncr53c9x_init(sc, 1);
	return;

finish:
	ncr53c9x_done(sc, ecb);
	return;

sched:
	sc->sc_state = NCR_IDLE;
	ncr53c9x_sched(sc);
	return;

shortcut:
	/*
	 * The idea is that many of the SCSI operations take very little
	 * time, and going away and getting interrupted is too high an
	 * overhead to pay.  For example, selecting, sending a message
	 * and command and then doing some work can be done in one "pass".
	 *
	 * The delay is a heuristic.  It is 2 when at 20 MHz, 2 at 25 MHz and
	 * 1 at 40 MHz.  This needs testing.
	 */
	microtime(&wait);
	wait.tv_usec += 50 / sc->sc_freq;
	if (wait.tv_usec > 1000000) {
		wait.tv_sec++;
		wait.tv_usec -= 1000000;
	}
	do {
		if (NCRDMA_ISINTR(sc))
			goto again;
		microtime(&cur);
	} while (cur.tv_sec <= wait.tv_sec && cur.tv_usec <= wait.tv_usec);
}

static void
ncr53c9x_abort(struct ncr53c9x_softc *sc, struct ncr53c9x_ecb *ecb)
{

	NCR_LOCK_ASSERT(sc, MA_OWNED);

	/* 2 secs for the abort */
	ecb->timeout = NCR_ABORT_TIMEOUT;
	ecb->flags |= ECB_ABORT;

	if (ecb == sc->sc_nexus) {
		/*
		 * If we're still selecting, the message will be scheduled
		 * after selection is complete.
		 */
		if (sc->sc_state == NCR_CONNECTED)
			ncr53c9x_sched_msgout(SEND_ABORT);

		/*
		 * Reschedule callout.
		 */
		callout_reset(&ecb->ch, mstohz(ecb->timeout),
		    ncr53c9x_callout, ecb);
	} else {
		/*
		 * Just leave the command where it is.
		 * XXX - what choice do we have but to reset the SCSI
		 *	 eventually?
		 */
		if (sc->sc_state == NCR_IDLE)
			ncr53c9x_sched(sc);
	}
}

static void
ncr53c9x_callout(void *arg)
{
	struct ncr53c9x_ecb *ecb = arg;
	union ccb *ccb = ecb->ccb;
	struct ncr53c9x_softc *sc = ecb->sc;
	struct ncr53c9x_tinfo *ti;

	NCR_LOCK_ASSERT(sc, MA_OWNED);

	ti = &sc->sc_tinfo[ccb->ccb_h.target_id];
	xpt_print_path(ccb->ccb_h.path);
	device_printf(sc->sc_dev, "timed out [ecb %p (flags 0x%x, dleft %x, "
	    "stat %x)], <state %d, nexus %p, phase(l %x, c %x, p %x), "
	    "resid %lx, msg(q %x,o %x) %s>",
	    ecb, ecb->flags, ecb->dleft, ecb->stat,
	    sc->sc_state, sc->sc_nexus,
	    NCR_READ_REG(sc, NCR_STAT),
	    sc->sc_phase, sc->sc_prevphase,
	    (long)sc->sc_dleft, sc->sc_msgpriq, sc->sc_msgout,
	    NCRDMA_ISACTIVE(sc) ? "DMA active" : "");
#if defined(NCR53C9X_DEBUG) && NCR53C9X_DEBUG > 1
	printf("TRACE: %s.", ecb->trace);
#endif

	if (ecb->flags & ECB_ABORT) {
		/* Abort timed out. */
		printf(" AGAIN\n");
		ncr53c9x_init(sc, 1);
	} else {
		/* Abort the operation that has timed out. */
		printf("\n");
		ccb->ccb_h.status = CAM_CMD_TIMEOUT;
		ncr53c9x_abort(sc, ecb);

		/* Disable sync mode if stuck in a data phase. */
		if (ecb == sc->sc_nexus && ti->curr.offset != 0 &&
		    (sc->sc_phase & (MSGI | CDI)) == 0) {
			/* XXX ASYNC CALLBACK! */
			ti->goal.offset = 0;
			xpt_print_path(ccb->ccb_h.path);
			printf("sync negotiation disabled\n");
		}
	}
}

static void
ncr53c9x_watch(void *arg)
{
	struct ncr53c9x_softc *sc = arg;
	struct ncr53c9x_linfo *li;
	struct ncr53c9x_tinfo *ti;
	time_t old;
	int t;

	NCR_LOCK_ASSERT(sc, MA_OWNED);

	/* Delete any structures that have not been used in 10min. */
	old = time_second - (10 * 60);

	for (t = 0; t < sc->sc_ntarg; t++) {
		ti = &sc->sc_tinfo[t];
		li = LIST_FIRST(&ti->luns);
		while (li) {
			if (li->last_used < old &&
			    li->untagged == NULL &&
			    li->used == 0) {
				if (li->lun < NCR_NLUN)
					ti->lun[li->lun] = NULL;
				LIST_REMOVE(li, link);
				free(li, M_DEVBUF);
				/* Restart the search at the beginning. */
				li = LIST_FIRST(&ti->luns);
				continue;
			}
			li = LIST_NEXT(li, link);
		}
	}
	callout_reset(&sc->sc_watchdog, 60 * hz, ncr53c9x_watch, sc);
}
