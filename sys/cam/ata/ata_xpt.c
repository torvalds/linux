/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Alexander Motin <mav@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/time.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/sbuf.h>

#include <sys/eventhandler.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_queue.h>
#include <cam/cam_periph.h>
#include <cam/cam_sim.h>
#include <cam/cam_xpt.h>
#include <cam/cam_xpt_sim.h>
#include <cam/cam_xpt_periph.h>
#include <cam/cam_xpt_internal.h>
#include <cam/cam_debug.h>

#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_message.h>
#include <cam/ata/ata_all.h>
#include <machine/stdarg.h>	/* for xpt_print below */
#include "opt_cam.h"

struct ata_quirk_entry {
	struct scsi_inquiry_pattern inq_pat;
	u_int8_t quirks;
#define	CAM_QUIRK_MAXTAGS	0x01
	u_int mintags;
	u_int maxtags;
};

static periph_init_t probe_periph_init;

static struct periph_driver probe_driver =
{
	probe_periph_init, "aprobe",
	TAILQ_HEAD_INITIALIZER(probe_driver.units), /* generation */ 0,
	CAM_PERIPH_DRV_EARLY
};

PERIPHDRIVER_DECLARE(aprobe, probe_driver);

typedef enum {
	PROBE_RESET,
	PROBE_IDENTIFY,
	PROBE_SPINUP,
	PROBE_SETMODE,
	PROBE_SETPM,
	PROBE_SETAPST,
	PROBE_SETDMAAA,
	PROBE_SETAN,
	PROBE_SET_MULTI,
	PROBE_INQUIRY,
	PROBE_FULL_INQUIRY,
	PROBE_PM_PID,
	PROBE_PM_PRV,
	PROBE_IDENTIFY_SES,
	PROBE_IDENTIFY_SAFTE,
	PROBE_DONE,
	PROBE_INVALID
} probe_action;

static char *probe_action_text[] = {
	"PROBE_RESET",
	"PROBE_IDENTIFY",
	"PROBE_SPINUP",
	"PROBE_SETMODE",
	"PROBE_SETPM",
	"PROBE_SETAPST",
	"PROBE_SETDMAAA",
	"PROBE_SETAN",
	"PROBE_SET_MULTI",
	"PROBE_INQUIRY",
	"PROBE_FULL_INQUIRY",
	"PROBE_PM_PID",
	"PROBE_PM_PRV",
	"PROBE_IDENTIFY_SES",
	"PROBE_IDENTIFY_SAFTE",
	"PROBE_DONE",
	"PROBE_INVALID"
};

#define PROBE_SET_ACTION(softc, newaction)	\
do {									\
	char **text;							\
	text = probe_action_text;					\
	CAM_DEBUG((softc)->periph->path, CAM_DEBUG_PROBE,		\
	    ("Probe %s to %s\n", text[(softc)->action],			\
	    text[(newaction)]));					\
	(softc)->action = (newaction);					\
} while(0)

typedef enum {
	PROBE_NO_ANNOUNCE	= 0x04
} probe_flags;

typedef struct {
	TAILQ_HEAD(, ccb_hdr) request_ccbs;
	struct ata_params	ident_data;
	probe_action	action;
	probe_flags	flags;
	uint32_t	pm_pid;
	uint32_t	pm_prv;
	int		restart;
	int		spinup;
	int		faults;
	u_int		caps;
	struct cam_periph *periph;
} probe_softc;

static struct ata_quirk_entry ata_quirk_table[] =
{
	{
		/* Default tagged queuing parameters for all devices */
		{
		  T_ANY, SIP_MEDIA_REMOVABLE|SIP_MEDIA_FIXED,
		  /*vendor*/"*", /*product*/"*", /*revision*/"*"
		},
		/*quirks*/0, /*mintags*/0, /*maxtags*/0
	},
};

static cam_status	proberegister(struct cam_periph *periph,
				      void *arg);
static void	 probeschedule(struct cam_periph *probe_periph);
static void	 probestart(struct cam_periph *periph, union ccb *start_ccb);
static void	 proberequestdefaultnegotiation(struct cam_periph *periph);
static void	 probedone(struct cam_periph *periph, union ccb *done_ccb);
static void	 probecleanup(struct cam_periph *periph);
static void	 ata_find_quirk(struct cam_ed *device);
static void	 ata_scan_bus(struct cam_periph *periph, union ccb *ccb);
static void	 ata_scan_lun(struct cam_periph *periph,
			       struct cam_path *path, cam_flags flags,
			       union ccb *ccb);
static void	 xptscandone(struct cam_periph *periph, union ccb *done_ccb);
static struct cam_ed *
		 ata_alloc_device(struct cam_eb *bus, struct cam_et *target,
				   lun_id_t lun_id);
static void	 ata_device_transport(struct cam_path *path);
static void	 ata_get_transfer_settings(struct ccb_trans_settings *cts);
static void	 ata_set_transfer_settings(struct ccb_trans_settings *cts,
					    struct cam_path *path,
					    int async_update);
static void	 ata_dev_async(u_int32_t async_code,
				struct cam_eb *bus,
				struct cam_et *target,
				struct cam_ed *device,
				void *async_arg);
static void	 ata_action(union ccb *start_ccb);
static void	 ata_announce_periph(struct cam_periph *periph);
static void	 ata_announce_periph_sbuf(struct cam_periph *periph, struct sbuf *sb);
static void	 ata_proto_announce(struct cam_ed *device);
static void	 ata_proto_announce_sbuf(struct cam_ed *device, struct sbuf *sb);
static void	 ata_proto_denounce(struct cam_ed *device);
static void	 ata_proto_denounce_sbuf(struct cam_ed *device, struct sbuf *sb);
static void	 ata_proto_debug_out(union ccb *ccb);
static void	 semb_proto_announce(struct cam_ed *device);
static void	 semb_proto_announce_sbuf(struct cam_ed *device, struct sbuf *sb);
static void	 semb_proto_denounce(struct cam_ed *device);
static void	 semb_proto_denounce_sbuf(struct cam_ed *device, struct sbuf *sb);

static int ata_dma = 1;
static int atapi_dma = 1;

TUNABLE_INT("hw.ata.ata_dma", &ata_dma);
TUNABLE_INT("hw.ata.atapi_dma", &atapi_dma);

static struct xpt_xport_ops ata_xport_ops = {
	.alloc_device = ata_alloc_device,
	.action = ata_action,
	.async = ata_dev_async,
	.announce = ata_announce_periph,
	.announce_sbuf = ata_announce_periph_sbuf,
};
#define ATA_XPT_XPORT(x, X)			\
static struct xpt_xport ata_xport_ ## x = {	\
	.xport = XPORT_ ## X,			\
	.name = #x,				\
	.ops = &ata_xport_ops,			\
};						\
CAM_XPT_XPORT(ata_xport_ ## x);

ATA_XPT_XPORT(ata, ATA);
ATA_XPT_XPORT(sata, SATA);

#undef ATA_XPORT_XPORT

static struct xpt_proto_ops ata_proto_ops_ata = {
	.announce = ata_proto_announce,
	.announce_sbuf = ata_proto_announce_sbuf,
	.denounce = ata_proto_denounce,
	.denounce_sbuf = ata_proto_denounce_sbuf,
	.debug_out = ata_proto_debug_out,
};
static struct xpt_proto ata_proto_ata = {
	.proto = PROTO_ATA,
	.name = "ata",
	.ops = &ata_proto_ops_ata,
};

static struct xpt_proto_ops ata_proto_ops_satapm = {
	.announce = ata_proto_announce,
	.announce_sbuf = ata_proto_announce_sbuf,
	.denounce = ata_proto_denounce,
	.denounce_sbuf = ata_proto_denounce_sbuf,
	.debug_out = ata_proto_debug_out,
};
static struct xpt_proto ata_proto_satapm = {
	.proto = PROTO_SATAPM,
	.name = "satapm",
	.ops = &ata_proto_ops_satapm,
};

static struct xpt_proto_ops ata_proto_ops_semb = {
	.announce = semb_proto_announce,
	.announce_sbuf = semb_proto_announce_sbuf,
	.denounce = semb_proto_denounce,
	.denounce_sbuf = semb_proto_denounce_sbuf,
	.debug_out = ata_proto_debug_out,
};
static struct xpt_proto ata_proto_semb = {
	.proto = PROTO_SEMB,
	.name = "semb",
	.ops = &ata_proto_ops_semb,
};

CAM_XPT_PROTO(ata_proto_ata);
CAM_XPT_PROTO(ata_proto_satapm);
CAM_XPT_PROTO(ata_proto_semb);

static void
probe_periph_init()
{
}

static cam_status
proberegister(struct cam_periph *periph, void *arg)
{
	union ccb *request_ccb;	/* CCB representing the probe request */
	probe_softc *softc;

	request_ccb = (union ccb *)arg;
	if (request_ccb == NULL) {
		printf("proberegister: no probe CCB, "
		       "can't register device\n");
		return(CAM_REQ_CMP_ERR);
	}

	softc = (probe_softc *)malloc(sizeof(*softc), M_CAMXPT, M_ZERO | M_NOWAIT);

	if (softc == NULL) {
		printf("proberegister: Unable to probe new device. "
		       "Unable to allocate softc\n");
		return(CAM_REQ_CMP_ERR);
	}
	TAILQ_INIT(&softc->request_ccbs);
	TAILQ_INSERT_TAIL(&softc->request_ccbs, &request_ccb->ccb_h,
			  periph_links.tqe);
	softc->flags = 0;
	periph->softc = softc;
	softc->periph = periph;
	softc->action = PROBE_INVALID;
	if (cam_periph_acquire(periph) != 0)
		return (CAM_REQ_CMP_ERR);

	CAM_DEBUG(periph->path, CAM_DEBUG_PROBE, ("Probe started\n"));
	ata_device_transport(periph->path);
	probeschedule(periph);
	return(CAM_REQ_CMP);
}

static void
probeschedule(struct cam_periph *periph)
{
	union ccb *ccb;
	probe_softc *softc;

	softc = (probe_softc *)periph->softc;
	ccb = (union ccb *)TAILQ_FIRST(&softc->request_ccbs);

	if ((periph->path->device->flags & CAM_DEV_UNCONFIGURED) ||
	    periph->path->device->protocol == PROTO_SATAPM ||
	    periph->path->device->protocol == PROTO_SEMB)
		PROBE_SET_ACTION(softc, PROBE_RESET);
	else
		PROBE_SET_ACTION(softc, PROBE_IDENTIFY);

	if (ccb->crcn.flags & CAM_EXPECT_INQ_CHANGE)
		softc->flags |= PROBE_NO_ANNOUNCE;
	else
		softc->flags &= ~PROBE_NO_ANNOUNCE;

	xpt_schedule(periph, CAM_PRIORITY_XPT);
}

static void
probestart(struct cam_periph *periph, union ccb *start_ccb)
{
	struct ccb_trans_settings cts;
	struct ccb_ataio *ataio;
	struct ccb_scsiio *csio;
	probe_softc *softc;
	struct cam_path *path;
	struct ata_params *ident_buf;

	CAM_DEBUG(start_ccb->ccb_h.path, CAM_DEBUG_TRACE, ("probestart\n"));

	softc = (probe_softc *)periph->softc;
	path = start_ccb->ccb_h.path;
	ataio = &start_ccb->ataio;
	csio = &start_ccb->csio;
	ident_buf = &periph->path->device->ident_data;

	if (softc->restart) {
		softc->restart = 0;
		if ((path->device->flags & CAM_DEV_UNCONFIGURED) ||
		    path->device->protocol == PROTO_SATAPM ||
		    path->device->protocol == PROTO_SEMB)
			softc->action = PROBE_RESET;
		else
			softc->action = PROBE_IDENTIFY;
	}
	switch (softc->action) {
	case PROBE_RESET:
		cam_fill_ataio(ataio,
		      0,
		      probedone,
		      /*flags*/CAM_DIR_NONE,
		      0,
		      /*data_ptr*/NULL,
		      /*dxfer_len*/0,
		      15 * 1000);
		ata_reset_cmd(ataio);
		break;
	case PROBE_IDENTIFY:
		cam_fill_ataio(ataio,
		      1,
		      probedone,
		      /*flags*/CAM_DIR_IN,
		      0,
		      /*data_ptr*/(u_int8_t *)&softc->ident_data,
		      /*dxfer_len*/sizeof(softc->ident_data),
		      30 * 1000);
		if (periph->path->device->protocol == PROTO_ATA)
			ata_28bit_cmd(ataio, ATA_ATA_IDENTIFY, 0, 0, 0);
		else
			ata_28bit_cmd(ataio, ATA_ATAPI_IDENTIFY, 0, 0, 0);
		break;
	case PROBE_SPINUP:
		if (bootverbose)
			xpt_print(path, "Spinning up device\n");
		cam_fill_ataio(ataio,
		      1,
		      probedone,
		      /*flags*/CAM_DIR_NONE | CAM_HIGH_POWER,
		      0,
		      /*data_ptr*/NULL,
		      /*dxfer_len*/0,
		      30 * 1000);
		ata_28bit_cmd(ataio, ATA_SETFEATURES, ATA_SF_PUIS_SPINUP, 0, 0);
		break;
	case PROBE_SETMODE:
	{
		int mode, wantmode;

		mode = 0;
		/* Fetch user modes from SIM. */
		bzero(&cts, sizeof(cts));
		xpt_setup_ccb(&cts.ccb_h, path, CAM_PRIORITY_NONE);
		cts.ccb_h.func_code = XPT_GET_TRAN_SETTINGS;
		cts.type = CTS_TYPE_USER_SETTINGS;
		xpt_action((union ccb *)&cts);
		if (path->device->transport == XPORT_ATA) {
			if (cts.xport_specific.ata.valid & CTS_ATA_VALID_MODE)
				mode = cts.xport_specific.ata.mode;
		} else {
			if (cts.xport_specific.sata.valid & CTS_SATA_VALID_MODE)
				mode = cts.xport_specific.sata.mode;
		}
		if (periph->path->device->protocol == PROTO_ATA) {
			if (ata_dma == 0 && (mode == 0 || mode > ATA_PIO_MAX))
				mode = ATA_PIO_MAX;
		} else {
			if (atapi_dma == 0 && (mode == 0 || mode > ATA_PIO_MAX))
				mode = ATA_PIO_MAX;
		}
negotiate:
		/* Honor device capabilities. */
		wantmode = mode = ata_max_mode(ident_buf, mode);
		/* Report modes to SIM. */
		bzero(&cts, sizeof(cts));
		xpt_setup_ccb(&cts.ccb_h, path, CAM_PRIORITY_NONE);
		cts.ccb_h.func_code = XPT_SET_TRAN_SETTINGS;
		cts.type = CTS_TYPE_CURRENT_SETTINGS;
		if (path->device->transport == XPORT_ATA) {
			cts.xport_specific.ata.mode = mode;
			cts.xport_specific.ata.valid = CTS_ATA_VALID_MODE;
		} else {
			cts.xport_specific.sata.mode = mode;
			cts.xport_specific.sata.valid = CTS_SATA_VALID_MODE;
		}
		xpt_action((union ccb *)&cts);
		/* Fetch current modes from SIM. */
		bzero(&cts, sizeof(cts));
		xpt_setup_ccb(&cts.ccb_h, path, CAM_PRIORITY_NONE);
		cts.ccb_h.func_code = XPT_GET_TRAN_SETTINGS;
		cts.type = CTS_TYPE_CURRENT_SETTINGS;
		xpt_action((union ccb *)&cts);
		if (path->device->transport == XPORT_ATA) {
			if (cts.xport_specific.ata.valid & CTS_ATA_VALID_MODE)
				mode = cts.xport_specific.ata.mode;
		} else {
			if (cts.xport_specific.ata.valid & CTS_SATA_VALID_MODE)
				mode = cts.xport_specific.sata.mode;
		}
		/* If SIM disagree - renegotiate. */
		if (mode != wantmode)
			goto negotiate;
		/* Remember what transport thinks about DMA. */
		if (mode < ATA_DMA)
			path->device->inq_flags &= ~SID_DMA;
		else
			path->device->inq_flags |= SID_DMA;
		xpt_async(AC_GETDEV_CHANGED, path, NULL);
		cam_fill_ataio(ataio,
		      1,
		      probedone,
		      /*flags*/CAM_DIR_NONE,
		      0,
		      /*data_ptr*/NULL,
		      /*dxfer_len*/0,
		      30 * 1000);
		ata_28bit_cmd(ataio, ATA_SETFEATURES, ATA_SF_SETXFER, 0, mode);
		break;
	}
	case PROBE_SETPM:
		cam_fill_ataio(ataio,
		    1,
		    probedone,
		    CAM_DIR_NONE,
		    0,
		    NULL,
		    0,
		    30*1000);
		ata_28bit_cmd(ataio, ATA_SETFEATURES,
		    (softc->caps & CTS_SATA_CAPS_H_PMREQ) ? 0x10 : 0x90,
		    0, 0x03);
		break;
	case PROBE_SETAPST:
		cam_fill_ataio(ataio,
		    1,
		    probedone,
		    CAM_DIR_NONE,
		    0,
		    NULL,
		    0,
		    30*1000);
		ata_28bit_cmd(ataio, ATA_SETFEATURES,
		    (softc->caps & CTS_SATA_CAPS_H_APST) ? 0x10 : 0x90,
		    0, 0x07);
		break;
	case PROBE_SETDMAAA:
		cam_fill_ataio(ataio,
		    1,
		    probedone,
		    CAM_DIR_NONE,
		    0,
		    NULL,
		    0,
		    30*1000);
		ata_28bit_cmd(ataio, ATA_SETFEATURES,
		    (softc->caps & CTS_SATA_CAPS_H_DMAAA) ? 0x10 : 0x90,
		    0, 0x02);
		break;
	case PROBE_SETAN:
		/* Remember what transport thinks about AEN. */
		if (softc->caps & CTS_SATA_CAPS_H_AN)
			path->device->inq_flags |= SID_AEN;
		else
			path->device->inq_flags &= ~SID_AEN;
		xpt_async(AC_GETDEV_CHANGED, path, NULL);
		cam_fill_ataio(ataio,
		    1,
		    probedone,
		    CAM_DIR_NONE,
		    0,
		    NULL,
		    0,
		    30*1000);
		ata_28bit_cmd(ataio, ATA_SETFEATURES,
		    (softc->caps & CTS_SATA_CAPS_H_AN) ? 0x10 : 0x90,
		    0, 0x05);
		break;
	case PROBE_SET_MULTI:
	{
		u_int sectors, bytecount;

		bytecount = 8192;	/* SATA maximum */
		/* Fetch user bytecount from SIM. */
		bzero(&cts, sizeof(cts));
		xpt_setup_ccb(&cts.ccb_h, path, CAM_PRIORITY_NONE);
		cts.ccb_h.func_code = XPT_GET_TRAN_SETTINGS;
		cts.type = CTS_TYPE_USER_SETTINGS;
		xpt_action((union ccb *)&cts);
		if (path->device->transport == XPORT_ATA) {
			if (cts.xport_specific.ata.valid & CTS_ATA_VALID_BYTECOUNT)
				bytecount = cts.xport_specific.ata.bytecount;
		} else {
			if (cts.xport_specific.sata.valid & CTS_SATA_VALID_BYTECOUNT)
				bytecount = cts.xport_specific.sata.bytecount;
		}
		/* Honor device capabilities. */
		sectors = max(1, min(ident_buf->sectors_intr & 0xff,
		    bytecount / ata_logical_sector_size(ident_buf)));
		/* Report bytecount to SIM. */
		bzero(&cts, sizeof(cts));
		xpt_setup_ccb(&cts.ccb_h, path, CAM_PRIORITY_NONE);
		cts.ccb_h.func_code = XPT_SET_TRAN_SETTINGS;
		cts.type = CTS_TYPE_CURRENT_SETTINGS;
		if (path->device->transport == XPORT_ATA) {
			cts.xport_specific.ata.bytecount = sectors *
			    ata_logical_sector_size(ident_buf);
			cts.xport_specific.ata.valid = CTS_ATA_VALID_BYTECOUNT;
		} else {
			cts.xport_specific.sata.bytecount = sectors *
			    ata_logical_sector_size(ident_buf);
			cts.xport_specific.sata.valid = CTS_SATA_VALID_BYTECOUNT;
		}
		xpt_action((union ccb *)&cts);
		/* Fetch current bytecount from SIM. */
		bzero(&cts, sizeof(cts));
		xpt_setup_ccb(&cts.ccb_h, path, CAM_PRIORITY_NONE);
		cts.ccb_h.func_code = XPT_GET_TRAN_SETTINGS;
		cts.type = CTS_TYPE_CURRENT_SETTINGS;
		xpt_action((union ccb *)&cts);
		if (path->device->transport == XPORT_ATA) {
			if (cts.xport_specific.ata.valid & CTS_ATA_VALID_BYTECOUNT)
				bytecount = cts.xport_specific.ata.bytecount;
		} else {
			if (cts.xport_specific.sata.valid & CTS_SATA_VALID_BYTECOUNT)
				bytecount = cts.xport_specific.sata.bytecount;
		}
		sectors = bytecount / ata_logical_sector_size(ident_buf);

		cam_fill_ataio(ataio,
		    1,
		    probedone,
		    CAM_DIR_NONE,
		    0,
		    NULL,
		    0,
		    30*1000);
		ata_28bit_cmd(ataio, ATA_SET_MULTI, 0, 0, sectors);
		break;
	}
	case PROBE_INQUIRY:
	{
		u_int bytecount;

		bytecount = 8192;	/* SATA maximum */
		/* Fetch user bytecount from SIM. */
		bzero(&cts, sizeof(cts));
		xpt_setup_ccb(&cts.ccb_h, path, CAM_PRIORITY_NONE);
		cts.ccb_h.func_code = XPT_GET_TRAN_SETTINGS;
		cts.type = CTS_TYPE_USER_SETTINGS;
		xpt_action((union ccb *)&cts);
		if (path->device->transport == XPORT_ATA) {
			if (cts.xport_specific.ata.valid & CTS_ATA_VALID_BYTECOUNT)
				bytecount = cts.xport_specific.ata.bytecount;
		} else {
			if (cts.xport_specific.sata.valid & CTS_SATA_VALID_BYTECOUNT)
				bytecount = cts.xport_specific.sata.bytecount;
		}
		/* Honor device capabilities. */
		bytecount &= ~1;
		bytecount = max(2, min(65534, bytecount));
		if (ident_buf->satacapabilities != 0x0000 &&
		    ident_buf->satacapabilities != 0xffff) {
			bytecount = min(8192, bytecount);
		}
		/* Report bytecount to SIM. */
		bzero(&cts, sizeof(cts));
		xpt_setup_ccb(&cts.ccb_h, path, CAM_PRIORITY_NONE);
		cts.ccb_h.func_code = XPT_SET_TRAN_SETTINGS;
		cts.type = CTS_TYPE_CURRENT_SETTINGS;
		if (path->device->transport == XPORT_ATA) {
			cts.xport_specific.ata.bytecount = bytecount;
			cts.xport_specific.ata.valid = CTS_ATA_VALID_BYTECOUNT;
		} else {
			cts.xport_specific.sata.bytecount = bytecount;
			cts.xport_specific.sata.valid = CTS_SATA_VALID_BYTECOUNT;
		}
		xpt_action((union ccb *)&cts);
		/* FALLTHROUGH */
	}
	case PROBE_FULL_INQUIRY:
	{
		u_int inquiry_len;
		struct scsi_inquiry_data *inq_buf =
		    &periph->path->device->inq_data;

		if (softc->action == PROBE_INQUIRY)
			inquiry_len = SHORT_INQUIRY_LENGTH;
		else
			inquiry_len = SID_ADDITIONAL_LENGTH(inq_buf);
		/*
		 * Some parallel SCSI devices fail to send an
		 * ignore wide residue message when dealing with
		 * odd length inquiry requests.  Round up to be
		 * safe.
		 */
		inquiry_len = roundup2(inquiry_len, 2);
		scsi_inquiry(csio,
			     /*retries*/1,
			     probedone,
			     MSG_SIMPLE_Q_TAG,
			     (u_int8_t *)inq_buf,
			     inquiry_len,
			     /*evpd*/FALSE,
			     /*page_code*/0,
			     SSD_MIN_SIZE,
			     /*timeout*/60 * 1000);
		break;
	}
	case PROBE_PM_PID:
		cam_fill_ataio(ataio,
		      1,
		      probedone,
		      /*flags*/CAM_DIR_NONE,
		      0,
		      /*data_ptr*/NULL,
		      /*dxfer_len*/0,
		      10 * 1000);
		ata_pm_read_cmd(ataio, 0, 15);
		break;
	case PROBE_PM_PRV:
		cam_fill_ataio(ataio,
		      1,
		      probedone,
		      /*flags*/CAM_DIR_NONE,
		      0,
		      /*data_ptr*/NULL,
		      /*dxfer_len*/0,
		      10 * 1000);
		ata_pm_read_cmd(ataio, 1, 15);
		break;
	case PROBE_IDENTIFY_SES:
		cam_fill_ataio(ataio,
		      1,
		      probedone,
		      /*flags*/CAM_DIR_IN,
		      0,
		      /*data_ptr*/(u_int8_t *)&softc->ident_data,
		      /*dxfer_len*/sizeof(softc->ident_data),
		      30 * 1000);
		ata_28bit_cmd(ataio, ATA_SEP_ATTN, 0xEC, 0x02,
		    sizeof(softc->ident_data) / 4);
		break;
	case PROBE_IDENTIFY_SAFTE:
		cam_fill_ataio(ataio,
		      1,
		      probedone,
		      /*flags*/CAM_DIR_IN,
		      0,
		      /*data_ptr*/(u_int8_t *)&softc->ident_data,
		      /*dxfer_len*/sizeof(softc->ident_data),
		      30 * 1000);
		ata_28bit_cmd(ataio, ATA_SEP_ATTN, 0xEC, 0x00,
		    sizeof(softc->ident_data) / 4);
		break;
	default:
		panic("probestart: invalid action state 0x%x\n", softc->action);
	}
	start_ccb->ccb_h.flags |= CAM_DEV_QFREEZE;
	xpt_action(start_ccb);
}

static void
proberequestdefaultnegotiation(struct cam_periph *periph)
{
	struct ccb_trans_settings cts;

	xpt_setup_ccb(&cts.ccb_h, periph->path, CAM_PRIORITY_NONE);
	cts.ccb_h.func_code = XPT_GET_TRAN_SETTINGS;
	cts.type = CTS_TYPE_USER_SETTINGS;
	xpt_action((union ccb *)&cts);
	if ((cts.ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP)
		return;
	cts.xport_specific.valid = 0;
	cts.ccb_h.func_code = XPT_SET_TRAN_SETTINGS;
	cts.type = CTS_TYPE_CURRENT_SETTINGS;
	xpt_action((union ccb *)&cts);
}

static void
probedone(struct cam_periph *periph, union ccb *done_ccb)
{
	struct ccb_trans_settings cts;
	struct ata_params *ident_buf;
	struct scsi_inquiry_data *inq_buf;
	probe_softc *softc;
	struct cam_path *path;
	cam_status status;
	u_int32_t  priority;
	u_int caps;
	int changed = 1, found = 1;
	static const uint8_t fake_device_id_hdr[8] =
	    {0, SVPD_DEVICE_ID, 0, 12,
	     SVPD_ID_CODESET_BINARY, SVPD_ID_TYPE_NAA, 0, 8};

	CAM_DEBUG(done_ccb->ccb_h.path, CAM_DEBUG_TRACE, ("probedone\n"));

	softc = (probe_softc *)periph->softc;
	path = done_ccb->ccb_h.path;
	priority = done_ccb->ccb_h.pinfo.priority;
	ident_buf = &path->device->ident_data;
	inq_buf = &path->device->inq_data;

	if ((done_ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
		if (cam_periph_error(done_ccb,
			0, softc->restart ? (SF_NO_RECOVERY | SF_NO_RETRY) : 0
		    ) == ERESTART) {
out:
			/* Drop freeze taken due to CAM_DEV_QFREEZE flag set. */
			cam_release_devq(path, 0, 0, 0, FALSE);
			return;
		}
		if ((done_ccb->ccb_h.status & CAM_DEV_QFRZN) != 0) {
			/* Don't wedge the queue */
			xpt_release_devq(path, /*count*/1, /*run_queue*/TRUE);
		}
		status = done_ccb->ccb_h.status & CAM_STATUS_MASK;
		if (softc->restart) {
			softc->faults++;
			if ((done_ccb->ccb_h.status & CAM_STATUS_MASK) ==
			    CAM_CMD_TIMEOUT)
				softc->faults += 4;
			if (softc->faults < 10)
				goto done;
			else
				softc->restart = 0;

		/* Old PIO2 devices may not support mode setting. */
		} else if (softc->action == PROBE_SETMODE &&
		    status == CAM_ATA_STATUS_ERROR &&
		    ata_max_pmode(ident_buf) <= ATA_PIO2 &&
		    (ident_buf->capabilities1 & ATA_SUPPORT_IORDY) == 0) {
			goto noerror;

		/*
		 * Some old WD SATA disks report supported and enabled
		 * device-initiated interface power management, but return
		 * ABORT on attempt to disable it.
		 */
		} else if (softc->action == PROBE_SETPM &&
		    status == CAM_ATA_STATUS_ERROR) {
			goto noerror;

		/*
		 * Some old WD SATA disks have broken SPINUP handling.
		 * If we really fail to spin up the disk, then there will be
		 * some media access errors later on, but at least we will
		 * have a device to interact with for recovery attempts.
		 */
		} else if (softc->action == PROBE_SPINUP &&
		    status == CAM_ATA_STATUS_ERROR) {
			goto noerror;

		/*
		 * Some HP SATA disks report supported DMA Auto-Activation,
		 * but return ABORT on attempt to enable it.
		 */
		} else if (softc->action == PROBE_SETDMAAA &&
		    status == CAM_ATA_STATUS_ERROR) {
			goto noerror;

		/*
		 * SES and SAF-TE SEPs have different IDENTIFY commands,
		 * but SATA specification doesn't tell how to identify them.
		 * Until better way found, just try another if first fail.
		 */
		} else if (softc->action == PROBE_IDENTIFY_SES &&
		    status == CAM_ATA_STATUS_ERROR) {
			PROBE_SET_ACTION(softc, PROBE_IDENTIFY_SAFTE);
			xpt_release_ccb(done_ccb);
			xpt_schedule(periph, priority);
			goto out;
		}

		/*
		 * If we get to this point, we got an error status back
		 * from the inquiry and the error status doesn't require
		 * automatically retrying the command.  Therefore, the
		 * inquiry failed.  If we had inquiry information before
		 * for this device, but this latest inquiry command failed,
		 * the device has probably gone away.  If this device isn't
		 * already marked unconfigured, notify the peripheral
		 * drivers that this device is no more.
		 */
device_fail:	if ((path->device->flags & CAM_DEV_UNCONFIGURED) == 0)
			xpt_async(AC_LOST_DEVICE, path, NULL);
		PROBE_SET_ACTION(softc, PROBE_INVALID);
		found = 0;
		goto done;
	}
noerror:
	if (softc->restart)
		goto done;
	switch (softc->action) {
	case PROBE_RESET:
	{
		int sign = (done_ccb->ataio.res.lba_high << 8) +
		    done_ccb->ataio.res.lba_mid;
		CAM_DEBUG(path, CAM_DEBUG_PROBE,
		    ("SIGNATURE: %04x\n", sign));
		if (sign == 0x0000 &&
		    done_ccb->ccb_h.target_id != 15) {
			path->device->protocol = PROTO_ATA;
			PROBE_SET_ACTION(softc, PROBE_IDENTIFY);
		} else if (sign == 0x9669 &&
		    done_ccb->ccb_h.target_id == 15) {
			/* Report SIM that PM is present. */
			bzero(&cts, sizeof(cts));
			xpt_setup_ccb(&cts.ccb_h, path, CAM_PRIORITY_NONE);
			cts.ccb_h.func_code = XPT_SET_TRAN_SETTINGS;
			cts.type = CTS_TYPE_CURRENT_SETTINGS;
			cts.xport_specific.sata.pm_present = 1;
			cts.xport_specific.sata.valid = CTS_SATA_VALID_PM;
			xpt_action((union ccb *)&cts);
			path->device->protocol = PROTO_SATAPM;
			PROBE_SET_ACTION(softc, PROBE_PM_PID);
		} else if (sign == 0xc33c &&
		    done_ccb->ccb_h.target_id != 15) {
			path->device->protocol = PROTO_SEMB;
			PROBE_SET_ACTION(softc, PROBE_IDENTIFY_SES);
		} else if (sign == 0xeb14 &&
		    done_ccb->ccb_h.target_id != 15) {
			path->device->protocol = PROTO_SCSI;
			PROBE_SET_ACTION(softc, PROBE_IDENTIFY);
		} else {
			if (done_ccb->ccb_h.target_id != 15) {
				xpt_print(path,
				    "Unexpected signature 0x%04x\n", sign);
			}
			goto device_fail;
		}
		xpt_release_ccb(done_ccb);
		xpt_schedule(periph, priority);
		goto out;
	}
	case PROBE_IDENTIFY:
	{
		struct ccb_pathinq cpi;
		int16_t *ptr;
		int veto = 0;

		ident_buf = &softc->ident_data;
		for (ptr = (int16_t *)ident_buf;
		     ptr < (int16_t *)ident_buf + sizeof(struct ata_params)/2; ptr++) {
			*ptr = le16toh(*ptr);
		}

		/*
		 * Allow others to veto this ATA disk attachment.  This
		 * is mainly used by VMs, whose disk controllers may
		 * share the disks with the simulated ATA controllers.
		 */
		EVENTHANDLER_INVOKE(ada_probe_veto, path, ident_buf, &veto);
		if (veto) {
			goto device_fail;
		}

		if (strncmp(ident_buf->model, "FX", 2) &&
		    strncmp(ident_buf->model, "NEC", 3) &&
		    strncmp(ident_buf->model, "Pioneer", 7) &&
		    strncmp(ident_buf->model, "SHARP", 5)) {
			ata_bswap(ident_buf->model, sizeof(ident_buf->model));
			ata_bswap(ident_buf->revision, sizeof(ident_buf->revision));
			ata_bswap(ident_buf->serial, sizeof(ident_buf->serial));
		}
		ata_btrim(ident_buf->model, sizeof(ident_buf->model));
		ata_bpack(ident_buf->model, ident_buf->model, sizeof(ident_buf->model));
		ata_btrim(ident_buf->revision, sizeof(ident_buf->revision));
		ata_bpack(ident_buf->revision, ident_buf->revision, sizeof(ident_buf->revision));
		ata_btrim(ident_buf->serial, sizeof(ident_buf->serial));
		ata_bpack(ident_buf->serial, ident_buf->serial, sizeof(ident_buf->serial));
		/* Device may need spin-up before IDENTIFY become valid. */
		if ((ident_buf->specconf == 0x37c8 ||
		     ident_buf->specconf == 0x738c) &&
		    ((ident_buf->config & ATA_RESP_INCOMPLETE) ||
		     softc->spinup == 0)) {
			PROBE_SET_ACTION(softc, PROBE_SPINUP);
			xpt_release_ccb(done_ccb);
			xpt_schedule(periph, priority);
			goto out;
		}
		ident_buf = &path->device->ident_data;
		if ((periph->path->device->flags & CAM_DEV_UNCONFIGURED) == 0) {
			/* Check that it is the same device. */
			if (bcmp(softc->ident_data.model, ident_buf->model,
			     sizeof(ident_buf->model)) ||
			    bcmp(softc->ident_data.revision, ident_buf->revision,
			     sizeof(ident_buf->revision)) ||
			    bcmp(softc->ident_data.serial, ident_buf->serial,
			     sizeof(ident_buf->serial))) {
				/* Device changed. */
				xpt_async(AC_LOST_DEVICE, path, NULL);
			} else {
				bcopy(&softc->ident_data, ident_buf, sizeof(struct ata_params));
				changed = 0;
			}
		}
		if (changed) {
			bcopy(&softc->ident_data, ident_buf, sizeof(struct ata_params));
			/* Clean up from previous instance of this device */
			if (path->device->serial_num != NULL) {
				free(path->device->serial_num, M_CAMXPT);
				path->device->serial_num = NULL;
				path->device->serial_num_len = 0;
			}
			if (path->device->device_id != NULL) {
				free(path->device->device_id, M_CAMXPT);
				path->device->device_id = NULL;
				path->device->device_id_len = 0;
			}
			path->device->serial_num =
				(u_int8_t *)malloc((sizeof(ident_buf->serial) + 1),
					   M_CAMXPT, M_NOWAIT);
			if (path->device->serial_num != NULL) {
				bcopy(ident_buf->serial,
				      path->device->serial_num,
				      sizeof(ident_buf->serial));
				path->device->serial_num[sizeof(ident_buf->serial)]
				    = '\0';
				path->device->serial_num_len =
				    strlen(path->device->serial_num);
			}
			if (ident_buf->enabled.extension &
			    ATA_SUPPORT_64BITWWN) {
				path->device->device_id =
				    malloc(16, M_CAMXPT, M_NOWAIT);
				if (path->device->device_id != NULL) {
					path->device->device_id_len = 16;
					bcopy(&fake_device_id_hdr,
					    path->device->device_id, 8);
					bcopy(ident_buf->wwn,
					    path->device->device_id + 8, 8);
					ata_bswap(path->device->device_id + 8, 8);
				}
			}

			path->device->flags |= CAM_DEV_IDENTIFY_DATA_VALID;
			xpt_async(AC_GETDEV_CHANGED, path, NULL);
		}
		if (ident_buf->satacapabilities & ATA_SUPPORT_NCQ) {
			path->device->mintags = 2;
			path->device->maxtags =
			    ATA_QUEUE_LEN(ident_buf->queue) + 1;
		}
		ata_find_quirk(path->device);
		if (path->device->mintags != 0 &&
		    path->bus->sim->max_tagged_dev_openings != 0) {
			/* Check if the SIM does not want queued commands. */
			xpt_path_inq(&cpi, path);
			if (cpi.ccb_h.status == CAM_REQ_CMP &&
			    (cpi.hba_inquiry & PI_TAG_ABLE)) {
				/* Report SIM which tags are allowed. */
				bzero(&cts, sizeof(cts));
				xpt_setup_ccb(&cts.ccb_h, path, CAM_PRIORITY_NONE);
				cts.ccb_h.func_code = XPT_SET_TRAN_SETTINGS;
				cts.type = CTS_TYPE_CURRENT_SETTINGS;
				cts.xport_specific.sata.tags = path->device->maxtags;
				cts.xport_specific.sata.valid = CTS_SATA_VALID_TAGS;
				xpt_action((union ccb *)&cts);
			}
		}
		ata_device_transport(path);
		if (changed)
			proberequestdefaultnegotiation(periph);
		PROBE_SET_ACTION(softc, PROBE_SETMODE);
		xpt_release_ccb(done_ccb);
		xpt_schedule(periph, priority);
		goto out;
	}
	case PROBE_SPINUP:
		if (bootverbose)
			xpt_print(path, "Spin-up done\n");
		softc->spinup = 1;
		PROBE_SET_ACTION(softc, PROBE_IDENTIFY);
		xpt_release_ccb(done_ccb);
		xpt_schedule(periph, priority);
		goto out;
	case PROBE_SETMODE:
		/* Set supported bits. */
		bzero(&cts, sizeof(cts));
		xpt_setup_ccb(&cts.ccb_h, path, CAM_PRIORITY_NONE);
		cts.ccb_h.func_code = XPT_GET_TRAN_SETTINGS;
		cts.type = CTS_TYPE_CURRENT_SETTINGS;
		xpt_action((union ccb *)&cts);
		if (path->device->transport == XPORT_SATA &&
		    cts.xport_specific.sata.valid & CTS_SATA_VALID_CAPS)
			caps = cts.xport_specific.sata.caps & CTS_SATA_CAPS_H;
		else if (path->device->transport == XPORT_ATA &&
		    cts.xport_specific.ata.valid & CTS_ATA_VALID_CAPS)
			caps = cts.xport_specific.ata.caps & CTS_ATA_CAPS_H;
		else
			caps = 0;
		if (path->device->transport == XPORT_SATA &&
		    ident_buf->satacapabilities != 0xffff) {
			if (ident_buf->satacapabilities & ATA_SUPPORT_IFPWRMNGTRCV)
				caps |= CTS_SATA_CAPS_D_PMREQ;
			if (ident_buf->satacapabilities & ATA_SUPPORT_HAPST)
				caps |= CTS_SATA_CAPS_D_APST;
		}
		/* Mask unwanted bits. */
		bzero(&cts, sizeof(cts));
		xpt_setup_ccb(&cts.ccb_h, path, CAM_PRIORITY_NONE);
		cts.ccb_h.func_code = XPT_GET_TRAN_SETTINGS;
		cts.type = CTS_TYPE_USER_SETTINGS;
		xpt_action((union ccb *)&cts);
		if (path->device->transport == XPORT_SATA &&
		    cts.xport_specific.sata.valid & CTS_SATA_VALID_CAPS)
			caps &= cts.xport_specific.sata.caps;
		else if (path->device->transport == XPORT_ATA &&
		    cts.xport_specific.ata.valid & CTS_ATA_VALID_CAPS)
			caps &= cts.xport_specific.ata.caps;
		else
			caps = 0;
		/*
		 * Remember what transport thinks about 48-bit DMA.  If
		 * capability information is not provided or transport is
		 * SATA, we take support for granted.
		 */
		if (!(path->device->inq_flags & SID_DMA) ||
		    (path->device->transport == XPORT_ATA &&
		    (cts.xport_specific.ata.valid & CTS_ATA_VALID_CAPS) &&
		    !(caps & CTS_ATA_CAPS_H_DMA48)))
			path->device->inq_flags &= ~SID_DMA48;
		else
			path->device->inq_flags |= SID_DMA48;
		/* Store result to SIM. */
		bzero(&cts, sizeof(cts));
		xpt_setup_ccb(&cts.ccb_h, path, CAM_PRIORITY_NONE);
		cts.ccb_h.func_code = XPT_SET_TRAN_SETTINGS;
		cts.type = CTS_TYPE_CURRENT_SETTINGS;
		if (path->device->transport == XPORT_SATA) {
			cts.xport_specific.sata.caps = caps;
			cts.xport_specific.sata.valid = CTS_SATA_VALID_CAPS;
		} else {
			cts.xport_specific.ata.caps = caps;
			cts.xport_specific.ata.valid = CTS_ATA_VALID_CAPS;
		}
		xpt_action((union ccb *)&cts);
		softc->caps = caps;
		if (path->device->transport != XPORT_SATA)
			goto notsata;
		if ((ident_buf->satasupport & ATA_SUPPORT_IFPWRMNGT) &&
		    (!(softc->caps & CTS_SATA_CAPS_H_PMREQ)) !=
		    (!(ident_buf->sataenabled & ATA_SUPPORT_IFPWRMNGT))) {
			PROBE_SET_ACTION(softc, PROBE_SETPM);
			xpt_release_ccb(done_ccb);
			xpt_schedule(periph, priority);
			goto out;
		}
		/* FALLTHROUGH */
	case PROBE_SETPM:
		if (ident_buf->satacapabilities != 0xffff &&
		    (ident_buf->satacapabilities & ATA_SUPPORT_DAPST) &&
		    (!(softc->caps & CTS_SATA_CAPS_H_APST)) !=
		    (!(ident_buf->sataenabled & ATA_ENABLED_DAPST))) {
			PROBE_SET_ACTION(softc, PROBE_SETAPST);
			xpt_release_ccb(done_ccb);
			xpt_schedule(periph, priority);
			goto out;
		}
		/* FALLTHROUGH */
	case PROBE_SETAPST:
		if ((ident_buf->satasupport & ATA_SUPPORT_AUTOACTIVATE) &&
		    (!(softc->caps & CTS_SATA_CAPS_H_DMAAA)) !=
		    (!(ident_buf->sataenabled & ATA_SUPPORT_AUTOACTIVATE))) {
			PROBE_SET_ACTION(softc, PROBE_SETDMAAA);
			xpt_release_ccb(done_ccb);
			xpt_schedule(periph, priority);
			goto out;
		}
		/* FALLTHROUGH */
	case PROBE_SETDMAAA:
		if (path->device->protocol != PROTO_ATA &&
		    (ident_buf->satasupport & ATA_SUPPORT_ASYNCNOTIF) &&
		    (!(softc->caps & CTS_SATA_CAPS_H_AN)) !=
		    (!(ident_buf->sataenabled & ATA_SUPPORT_ASYNCNOTIF))) {
			PROBE_SET_ACTION(softc, PROBE_SETAN);
			xpt_release_ccb(done_ccb);
			xpt_schedule(periph, priority);
			goto out;
		}
		/* FALLTHROUGH */
	case PROBE_SETAN:
notsata:
		if (path->device->protocol == PROTO_ATA) {
			PROBE_SET_ACTION(softc, PROBE_SET_MULTI);
		} else {
			PROBE_SET_ACTION(softc, PROBE_INQUIRY);
		}
		xpt_release_ccb(done_ccb);
		xpt_schedule(periph, priority);
		goto out;
	case PROBE_SET_MULTI:
		if (periph->path->device->flags & CAM_DEV_UNCONFIGURED) {
			path->device->flags &= ~CAM_DEV_UNCONFIGURED;
			xpt_acquire_device(path->device);
			done_ccb->ccb_h.func_code = XPT_GDEV_TYPE;
			xpt_action(done_ccb);
			xpt_async(AC_FOUND_DEVICE, path, done_ccb);
		}
		PROBE_SET_ACTION(softc, PROBE_DONE);
		break;
	case PROBE_INQUIRY:
	case PROBE_FULL_INQUIRY:
	{
		u_int8_t periph_qual, len;

		path->device->flags |= CAM_DEV_INQUIRY_DATA_VALID;

		periph_qual = SID_QUAL(inq_buf);

		if (periph_qual != SID_QUAL_LU_CONNECTED &&
		    periph_qual != SID_QUAL_LU_OFFLINE)
			break;

		/*
		 * We conservatively request only
		 * SHORT_INQUIRY_LEN bytes of inquiry
		 * information during our first try
		 * at sending an INQUIRY. If the device
		 * has more information to give,
		 * perform a second request specifying
		 * the amount of information the device
		 * is willing to give.
		 */
		len = inq_buf->additional_length
		    + offsetof(struct scsi_inquiry_data, additional_length) + 1;
		if (softc->action == PROBE_INQUIRY
		    && len > SHORT_INQUIRY_LENGTH) {
			PROBE_SET_ACTION(softc, PROBE_FULL_INQUIRY);
			xpt_release_ccb(done_ccb);
			xpt_schedule(periph, priority);
			goto out;
		}

		ata_device_transport(path);
		if (periph->path->device->flags & CAM_DEV_UNCONFIGURED) {
			path->device->flags &= ~CAM_DEV_UNCONFIGURED;
			xpt_acquire_device(path->device);
			done_ccb->ccb_h.func_code = XPT_GDEV_TYPE;
			xpt_action(done_ccb);
			xpt_async(AC_FOUND_DEVICE, path, done_ccb);
		}
		PROBE_SET_ACTION(softc, PROBE_DONE);
		break;
	}
	case PROBE_PM_PID:
		if ((path->device->flags & CAM_DEV_IDENTIFY_DATA_VALID) == 0)
			bzero(ident_buf, sizeof(*ident_buf));
		softc->pm_pid = (done_ccb->ataio.res.lba_high << 24) +
		    (done_ccb->ataio.res.lba_mid << 16) +
		    (done_ccb->ataio.res.lba_low << 8) +
		    done_ccb->ataio.res.sector_count;
		((uint32_t *)ident_buf)[0] = softc->pm_pid;
		snprintf(ident_buf->model, sizeof(ident_buf->model),
		    "Port Multiplier %08x", softc->pm_pid);
		PROBE_SET_ACTION(softc, PROBE_PM_PRV);
		xpt_release_ccb(done_ccb);
		xpt_schedule(periph, priority);
		goto out;
	case PROBE_PM_PRV:
		softc->pm_prv = (done_ccb->ataio.res.lba_high << 24) +
		    (done_ccb->ataio.res.lba_mid << 16) +
		    (done_ccb->ataio.res.lba_low << 8) +
		    done_ccb->ataio.res.sector_count;
		((uint32_t *)ident_buf)[1] = softc->pm_prv;
		snprintf(ident_buf->revision, sizeof(ident_buf->revision),
		    "%04x", softc->pm_prv);
		path->device->flags |= CAM_DEV_IDENTIFY_DATA_VALID;
		ata_device_transport(path);
		if (periph->path->device->flags & CAM_DEV_UNCONFIGURED)
			proberequestdefaultnegotiation(periph);
		/* Set supported bits. */
		bzero(&cts, sizeof(cts));
		xpt_setup_ccb(&cts.ccb_h, path, CAM_PRIORITY_NONE);
		cts.ccb_h.func_code = XPT_GET_TRAN_SETTINGS;
		cts.type = CTS_TYPE_CURRENT_SETTINGS;
		xpt_action((union ccb *)&cts);
		if (cts.xport_specific.sata.valid & CTS_SATA_VALID_CAPS)
			caps = cts.xport_specific.sata.caps & CTS_SATA_CAPS_H;
		else
			caps = 0;
		/* All PMPs must support PM requests. */
		caps |= CTS_SATA_CAPS_D_PMREQ;
		/* Mask unwanted bits. */
		bzero(&cts, sizeof(cts));
		xpt_setup_ccb(&cts.ccb_h, path, CAM_PRIORITY_NONE);
		cts.ccb_h.func_code = XPT_GET_TRAN_SETTINGS;
		cts.type = CTS_TYPE_USER_SETTINGS;
		xpt_action((union ccb *)&cts);
		if (cts.xport_specific.sata.valid & CTS_SATA_VALID_CAPS)
			caps &= cts.xport_specific.sata.caps;
		else
			caps = 0;
		/* Remember what transport thinks about AEN. */
		if ((caps & CTS_SATA_CAPS_H_AN) && path->device->protocol != PROTO_ATA)
			path->device->inq_flags |= SID_AEN;
		else
			path->device->inq_flags &= ~SID_AEN;
		/* Store result to SIM. */
		bzero(&cts, sizeof(cts));
		xpt_setup_ccb(&cts.ccb_h, path, CAM_PRIORITY_NONE);
		cts.ccb_h.func_code = XPT_SET_TRAN_SETTINGS;
		cts.type = CTS_TYPE_CURRENT_SETTINGS;
		cts.xport_specific.sata.caps = caps;
		cts.xport_specific.sata.valid = CTS_SATA_VALID_CAPS;
		xpt_action((union ccb *)&cts);
		softc->caps = caps;
		xpt_async(AC_GETDEV_CHANGED, path, NULL);
		if (periph->path->device->flags & CAM_DEV_UNCONFIGURED) {
			path->device->flags &= ~CAM_DEV_UNCONFIGURED;
			xpt_acquire_device(path->device);
			done_ccb->ccb_h.func_code = XPT_GDEV_TYPE;
			xpt_action(done_ccb);
			xpt_async(AC_FOUND_DEVICE, path, done_ccb);
		} else {
			done_ccb->ccb_h.func_code = XPT_GDEV_TYPE;
			xpt_action(done_ccb);
			xpt_async(AC_SCSI_AEN, path, done_ccb);
		}
		PROBE_SET_ACTION(softc, PROBE_DONE);
		break;
	case PROBE_IDENTIFY_SES:
	case PROBE_IDENTIFY_SAFTE:
		if ((periph->path->device->flags & CAM_DEV_UNCONFIGURED) == 0) {
			/* Check that it is the same device. */
			if (bcmp(&softc->ident_data, ident_buf, 53)) {
				/* Device changed. */
				xpt_async(AC_LOST_DEVICE, path, NULL);
			} else {
				bcopy(&softc->ident_data, ident_buf, sizeof(struct ata_params));
				changed = 0;
			}
		}
		if (changed) {
			bcopy(&softc->ident_data, ident_buf, sizeof(struct ata_params));
			/* Clean up from previous instance of this device */
			if (path->device->device_id != NULL) {
				free(path->device->device_id, M_CAMXPT);
				path->device->device_id = NULL;
				path->device->device_id_len = 0;
			}
			path->device->device_id =
			    malloc(16, M_CAMXPT, M_NOWAIT);
			if (path->device->device_id != NULL) {
				path->device->device_id_len = 16;
				bcopy(&fake_device_id_hdr,
				    path->device->device_id, 8);
				bcopy(((uint8_t*)ident_buf) + 2,
				    path->device->device_id + 8, 8);
			}

			path->device->flags |= CAM_DEV_IDENTIFY_DATA_VALID;
		}
		ata_device_transport(path);
		if (changed)
			proberequestdefaultnegotiation(periph);

		if (periph->path->device->flags & CAM_DEV_UNCONFIGURED) {
			path->device->flags &= ~CAM_DEV_UNCONFIGURED;
			xpt_acquire_device(path->device);
			done_ccb->ccb_h.func_code = XPT_GDEV_TYPE;
			xpt_action(done_ccb);
			xpt_async(AC_FOUND_DEVICE, path, done_ccb);
		}
		PROBE_SET_ACTION(softc, PROBE_DONE);
		break;
	default:
		panic("probedone: invalid action state 0x%x\n", softc->action);
	}
done:
	if (softc->restart) {
		softc->restart = 0;
		xpt_release_ccb(done_ccb);
		probeschedule(periph);
		goto out;
	}
	xpt_release_ccb(done_ccb);
	CAM_DEBUG(periph->path, CAM_DEBUG_PROBE, ("Probe completed\n"));
	while ((done_ccb = (union ccb *)TAILQ_FIRST(&softc->request_ccbs))) {
		TAILQ_REMOVE(&softc->request_ccbs,
		    &done_ccb->ccb_h, periph_links.tqe);
		done_ccb->ccb_h.status = found ? CAM_REQ_CMP : CAM_REQ_CMP_ERR;
		xpt_done(done_ccb);
	}
	/* Drop freeze taken due to CAM_DEV_QFREEZE flag set. */
	cam_release_devq(path, 0, 0, 0, FALSE);
	cam_periph_invalidate(periph);
	cam_periph_release_locked(periph);
}

static void
probecleanup(struct cam_periph *periph)
{
	free(periph->softc, M_CAMXPT);
}

static void
ata_find_quirk(struct cam_ed *device)
{
	struct ata_quirk_entry *quirk;
	caddr_t	match;

	match = cam_quirkmatch((caddr_t)&device->ident_data,
			       (caddr_t)ata_quirk_table,
			       nitems(ata_quirk_table),
			       sizeof(*ata_quirk_table), ata_identify_match);

	if (match == NULL)
		panic("xpt_find_quirk: device didn't match wildcard entry!!");

	quirk = (struct ata_quirk_entry *)match;
	device->quirk = quirk;
	if (quirk->quirks & CAM_QUIRK_MAXTAGS) {
		device->mintags = quirk->mintags;
		device->maxtags = quirk->maxtags;
	}
}

typedef struct {
	union	ccb *request_ccb;
	struct 	ccb_pathinq *cpi;
	int	counter;
} ata_scan_bus_info;

/*
 * To start a scan, request_ccb is an XPT_SCAN_BUS ccb.
 * As the scan progresses, xpt_scan_bus is used as the
 * callback on completion function.
 */
static void
ata_scan_bus(struct cam_periph *periph, union ccb *request_ccb)
{
	struct	cam_path *path;
	ata_scan_bus_info *scan_info;
	union	ccb *work_ccb, *reset_ccb;
	struct mtx *mtx;
	cam_status status;

	CAM_DEBUG(request_ccb->ccb_h.path, CAM_DEBUG_TRACE,
		  ("xpt_scan_bus\n"));
	switch (request_ccb->ccb_h.func_code) {
	case XPT_SCAN_BUS:
	case XPT_SCAN_TGT:
		/* Find out the characteristics of the bus */
		work_ccb = xpt_alloc_ccb_nowait();
		if (work_ccb == NULL) {
			request_ccb->ccb_h.status = CAM_RESRC_UNAVAIL;
			xpt_done(request_ccb);
			return;
		}
		xpt_path_inq(&work_ccb->cpi, request_ccb->ccb_h.path);
		if (work_ccb->ccb_h.status != CAM_REQ_CMP) {
			request_ccb->ccb_h.status = work_ccb->ccb_h.status;
			xpt_free_ccb(work_ccb);
			xpt_done(request_ccb);
			return;
		}

		/* We may need to reset bus first, if we haven't done it yet. */
		if ((work_ccb->cpi.hba_inquiry &
		    (PI_WIDE_32|PI_WIDE_16|PI_SDTR_ABLE)) &&
		    !(work_ccb->cpi.hba_misc & PIM_NOBUSRESET) &&
		    !timevalisset(&request_ccb->ccb_h.path->bus->last_reset)) {
			reset_ccb = xpt_alloc_ccb_nowait();
			if (reset_ccb == NULL) {
				request_ccb->ccb_h.status = CAM_RESRC_UNAVAIL;
				xpt_free_ccb(work_ccb);
				xpt_done(request_ccb);
				return;
			}
			xpt_setup_ccb(&reset_ccb->ccb_h, request_ccb->ccb_h.path,
			      CAM_PRIORITY_NONE);
			reset_ccb->ccb_h.func_code = XPT_RESET_BUS;
			xpt_action(reset_ccb);
			if (reset_ccb->ccb_h.status != CAM_REQ_CMP) {
				request_ccb->ccb_h.status = reset_ccb->ccb_h.status;
				xpt_free_ccb(reset_ccb);
				xpt_free_ccb(work_ccb);
				xpt_done(request_ccb);
				return;
			}
			xpt_free_ccb(reset_ccb);
		}

		/* Save some state for use while we probe for devices */
		scan_info = (ata_scan_bus_info *)
		    malloc(sizeof(ata_scan_bus_info), M_CAMXPT, M_NOWAIT);
		if (scan_info == NULL) {
			request_ccb->ccb_h.status = CAM_RESRC_UNAVAIL;
			xpt_free_ccb(work_ccb);
			xpt_done(request_ccb);
			return;
		}
		scan_info->request_ccb = request_ccb;
		scan_info->cpi = &work_ccb->cpi;
		/* If PM supported, probe it first. */
		if (scan_info->cpi->hba_inquiry & PI_SATAPM)
			scan_info->counter = scan_info->cpi->max_target;
		else
			scan_info->counter = 0;

		work_ccb = xpt_alloc_ccb_nowait();
		if (work_ccb == NULL) {
			free(scan_info, M_CAMXPT);
			request_ccb->ccb_h.status = CAM_RESRC_UNAVAIL;
			xpt_done(request_ccb);
			break;
		}
		mtx = xpt_path_mtx(scan_info->request_ccb->ccb_h.path);
		goto scan_next;
	case XPT_SCAN_LUN:
		work_ccb = request_ccb;
		/* Reuse the same CCB to query if a device was really found */
		scan_info = (ata_scan_bus_info *)work_ccb->ccb_h.ppriv_ptr0;
		mtx = xpt_path_mtx(scan_info->request_ccb->ccb_h.path);
		mtx_lock(mtx);
		/* If there is PMP... */
		if ((scan_info->cpi->hba_inquiry & PI_SATAPM) &&
		    (scan_info->counter == scan_info->cpi->max_target)) {
			if (work_ccb->ccb_h.status == CAM_REQ_CMP) {
				/* everything else will be probed by it */
				/* Free the current request path- we're done with it. */
				xpt_free_path(work_ccb->ccb_h.path);
				goto done;
			} else {
				struct ccb_trans_settings cts;

				/* Report SIM that PM is absent. */
				bzero(&cts, sizeof(cts));
				xpt_setup_ccb(&cts.ccb_h,
				    work_ccb->ccb_h.path, CAM_PRIORITY_NONE);
				cts.ccb_h.func_code = XPT_SET_TRAN_SETTINGS;
				cts.type = CTS_TYPE_CURRENT_SETTINGS;
				cts.xport_specific.sata.pm_present = 0;
				cts.xport_specific.sata.valid = CTS_SATA_VALID_PM;
				xpt_action((union ccb *)&cts);
			}
		}
		/* Free the current request path- we're done with it. */
		xpt_free_path(work_ccb->ccb_h.path);
		if (scan_info->counter ==
		    ((scan_info->cpi->hba_inquiry & PI_SATAPM) ?
		    0 : scan_info->cpi->max_target)) {
done:
			mtx_unlock(mtx);
			xpt_free_ccb(work_ccb);
			xpt_free_ccb((union ccb *)scan_info->cpi);
			request_ccb = scan_info->request_ccb;
			free(scan_info, M_CAMXPT);
			request_ccb->ccb_h.status = CAM_REQ_CMP;
			xpt_done(request_ccb);
			break;
		}
		/* Take next device. Wrap from max (PMP) to 0. */
		scan_info->counter = (scan_info->counter + 1 ) %
		    (scan_info->cpi->max_target + 1);
scan_next:
		status = xpt_create_path(&path, NULL,
		    scan_info->request_ccb->ccb_h.path_id,
		    scan_info->counter, 0);
		if (status != CAM_REQ_CMP) {
			if (request_ccb->ccb_h.func_code == XPT_SCAN_LUN)
				mtx_unlock(mtx);
			printf("xpt_scan_bus: xpt_create_path failed"
			    " with status %#x, bus scan halted\n",
			    status);
			xpt_free_ccb(work_ccb);
			xpt_free_ccb((union ccb *)scan_info->cpi);
			request_ccb = scan_info->request_ccb;
			free(scan_info, M_CAMXPT);
			request_ccb->ccb_h.status = status;
			xpt_done(request_ccb);
			break;
		}
		xpt_setup_ccb(&work_ccb->ccb_h, path,
		    scan_info->request_ccb->ccb_h.pinfo.priority);
		work_ccb->ccb_h.func_code = XPT_SCAN_LUN;
		work_ccb->ccb_h.cbfcnp = ata_scan_bus;
		work_ccb->ccb_h.flags |= CAM_UNLOCKED;
		work_ccb->ccb_h.ppriv_ptr0 = scan_info;
		work_ccb->crcn.flags = scan_info->request_ccb->crcn.flags;
		mtx_unlock(mtx);
		if (request_ccb->ccb_h.func_code == XPT_SCAN_LUN)
			mtx = NULL;
		xpt_action(work_ccb);
		if (mtx != NULL)
			mtx_lock(mtx);
		break;
	default:
		break;
	}
}

static void
ata_scan_lun(struct cam_periph *periph, struct cam_path *path,
	     cam_flags flags, union ccb *request_ccb)
{
	struct ccb_pathinq cpi;
	cam_status status;
	struct cam_path *new_path;
	struct cam_periph *old_periph;
	int lock;

	CAM_DEBUG(path, CAM_DEBUG_TRACE, ("xpt_scan_lun\n"));

	xpt_path_inq(&cpi, path);
	if (cpi.ccb_h.status != CAM_REQ_CMP) {
		if (request_ccb != NULL) {
			request_ccb->ccb_h.status = cpi.ccb_h.status;
			xpt_done(request_ccb);
		}
		return;
	}

	if (request_ccb == NULL) {
		request_ccb = xpt_alloc_ccb_nowait();
		if (request_ccb == NULL) {
			xpt_print(path, "xpt_scan_lun: can't allocate CCB, "
			    "can't continue\n");
			return;
		}
		status = xpt_create_path(&new_path, NULL,
					  path->bus->path_id,
					  path->target->target_id,
					  path->device->lun_id);
		if (status != CAM_REQ_CMP) {
			xpt_print(path, "xpt_scan_lun: can't create path, "
			    "can't continue\n");
			xpt_free_ccb(request_ccb);
			return;
		}
		xpt_setup_ccb(&request_ccb->ccb_h, new_path, CAM_PRIORITY_XPT);
		request_ccb->ccb_h.cbfcnp = xptscandone;
		request_ccb->ccb_h.flags |= CAM_UNLOCKED;
		request_ccb->ccb_h.func_code = XPT_SCAN_LUN;
		request_ccb->crcn.flags = flags;
	}

	lock = (xpt_path_owned(path) == 0);
	if (lock)
		xpt_path_lock(path);
	if ((old_periph = cam_periph_find(path, "aprobe")) != NULL) {
		if ((old_periph->flags & CAM_PERIPH_INVALID) == 0) {
			probe_softc *softc;

			softc = (probe_softc *)old_periph->softc;
			TAILQ_INSERT_TAIL(&softc->request_ccbs,
				&request_ccb->ccb_h, periph_links.tqe);
			softc->restart = 1;
		} else {
			request_ccb->ccb_h.status = CAM_REQ_CMP_ERR;
			xpt_done(request_ccb);
		}
	} else {
		status = cam_periph_alloc(proberegister, NULL, probecleanup,
					  probestart, "aprobe",
					  CAM_PERIPH_BIO,
					  request_ccb->ccb_h.path, NULL, 0,
					  request_ccb);

		if (status != CAM_REQ_CMP) {
			xpt_print(path, "xpt_scan_lun: cam_alloc_periph "
			    "returned an error, can't continue probe\n");
			request_ccb->ccb_h.status = status;
			xpt_done(request_ccb);
		}
	}
	if (lock)
		xpt_path_unlock(path);
}

static void
xptscandone(struct cam_periph *periph, union ccb *done_ccb)
{

	xpt_free_path(done_ccb->ccb_h.path);
	xpt_free_ccb(done_ccb);
}

static struct cam_ed *
ata_alloc_device(struct cam_eb *bus, struct cam_et *target, lun_id_t lun_id)
{
	struct ata_quirk_entry *quirk;
	struct cam_ed *device;

	device = xpt_alloc_device(bus, target, lun_id);
	if (device == NULL)
		return (NULL);

	/*
	 * Take the default quirk entry until we have inquiry
	 * data and can determine a better quirk to use.
	 */
	quirk = &ata_quirk_table[nitems(ata_quirk_table) - 1];
	device->quirk = (void *)quirk;
	device->mintags = 0;
	device->maxtags = 0;
	bzero(&device->inq_data, sizeof(device->inq_data));
	device->inq_flags = 0;
	device->queue_flags = 0;
	device->serial_num = NULL;
	device->serial_num_len = 0;
	return (device);
}

static void
ata_device_transport(struct cam_path *path)
{
	struct ccb_pathinq cpi;
	struct ccb_trans_settings cts;
	struct scsi_inquiry_data *inq_buf = NULL;
	struct ata_params *ident_buf = NULL;

	/* Get transport information from the SIM */
	xpt_path_inq(&cpi, path);

	path->device->transport = cpi.transport;
	if ((path->device->flags & CAM_DEV_INQUIRY_DATA_VALID) != 0)
		inq_buf = &path->device->inq_data;
	if ((path->device->flags & CAM_DEV_IDENTIFY_DATA_VALID) != 0)
		ident_buf = &path->device->ident_data;
	if (path->device->protocol == PROTO_ATA) {
		path->device->protocol_version = ident_buf ?
		    ata_version(ident_buf->version_major) : cpi.protocol_version;
	} else if (path->device->protocol == PROTO_SCSI) {
		path->device->protocol_version = inq_buf ?
		    SID_ANSI_REV(inq_buf) : cpi.protocol_version;
	}
	path->device->transport_version = ident_buf ?
	    ata_version(ident_buf->version_major) : cpi.transport_version;

	/* Tell the controller what we think */
	xpt_setup_ccb(&cts.ccb_h, path, CAM_PRIORITY_NONE);
	cts.ccb_h.func_code = XPT_SET_TRAN_SETTINGS;
	cts.type = CTS_TYPE_CURRENT_SETTINGS;
	cts.transport = path->device->transport;
	cts.transport_version = path->device->transport_version;
	cts.protocol = path->device->protocol;
	cts.protocol_version = path->device->protocol_version;
	cts.proto_specific.valid = 0;
	if (ident_buf) {
		if (path->device->transport == XPORT_ATA) {
			cts.xport_specific.ata.atapi =
			    (ident_buf->config == ATA_PROTO_CFA) ? 0 :
			    ((ident_buf->config & ATA_PROTO_MASK) == ATA_PROTO_ATAPI_16) ? 16 :
			    ((ident_buf->config & ATA_PROTO_MASK) == ATA_PROTO_ATAPI_12) ? 12 : 0;
			cts.xport_specific.ata.valid = CTS_ATA_VALID_ATAPI;
		} else {
			cts.xport_specific.sata.atapi =
			    (ident_buf->config == ATA_PROTO_CFA) ? 0 :
			    ((ident_buf->config & ATA_PROTO_MASK) == ATA_PROTO_ATAPI_16) ? 16 :
			    ((ident_buf->config & ATA_PROTO_MASK) == ATA_PROTO_ATAPI_12) ? 12 : 0;
			cts.xport_specific.sata.valid = CTS_SATA_VALID_ATAPI;
		}
	} else
		cts.xport_specific.valid = 0;
	xpt_action((union ccb *)&cts);
}

static void
ata_dev_advinfo(union ccb *start_ccb)
{
	struct cam_ed *device;
	struct ccb_dev_advinfo *cdai;
	off_t amt; 

	start_ccb->ccb_h.status = CAM_REQ_INVALID;
	device = start_ccb->ccb_h.path->device;
	cdai = &start_ccb->cdai;
	switch(cdai->buftype) {
	case CDAI_TYPE_SCSI_DEVID:
		if (cdai->flags & CDAI_FLAG_STORE)
			return;
		cdai->provsiz = device->device_id_len;
		if (device->device_id_len == 0)
			break;
		amt = device->device_id_len;
		if (cdai->provsiz > cdai->bufsiz)
			amt = cdai->bufsiz;
		memcpy(cdai->buf, device->device_id, amt);
		break;
	case CDAI_TYPE_SERIAL_NUM:
		if (cdai->flags & CDAI_FLAG_STORE)
			return;
		cdai->provsiz = device->serial_num_len;
		if (device->serial_num_len == 0)
			break;
		amt = device->serial_num_len;
		if (cdai->provsiz > cdai->bufsiz)
			amt = cdai->bufsiz;
		memcpy(cdai->buf, device->serial_num, amt);
		break;
	case CDAI_TYPE_PHYS_PATH:
		if (cdai->flags & CDAI_FLAG_STORE) {
			if (device->physpath != NULL)
				free(device->physpath, M_CAMXPT);
			device->physpath_len = cdai->bufsiz;
			/* Clear existing buffer if zero length */
			if (cdai->bufsiz == 0)
				break;
			device->physpath = malloc(cdai->bufsiz, M_CAMXPT, M_NOWAIT);
			if (device->physpath == NULL) {
				start_ccb->ccb_h.status = CAM_REQ_ABORTED;
				return;
			}
			memcpy(device->physpath, cdai->buf, cdai->bufsiz);
		} else {
			cdai->provsiz = device->physpath_len;
			if (device->physpath_len == 0)
				break;
			amt = device->physpath_len;
			if (cdai->provsiz > cdai->bufsiz)
				amt = cdai->bufsiz;
			memcpy(cdai->buf, device->physpath, amt);
		}
		break;
	default:
		return;
	}
	start_ccb->ccb_h.status = CAM_REQ_CMP;

	if (cdai->flags & CDAI_FLAG_STORE) {
		xpt_async(AC_ADVINFO_CHANGED, start_ccb->ccb_h.path,
			  (void *)(uintptr_t)cdai->buftype);
	}
}

static void
ata_action(union ccb *start_ccb)
{

	switch (start_ccb->ccb_h.func_code) {
	case XPT_SET_TRAN_SETTINGS:
	{
		ata_set_transfer_settings(&start_ccb->cts,
					   start_ccb->ccb_h.path,
					   /*async_update*/FALSE);
		break;
	}
	case XPT_SCAN_BUS:
	case XPT_SCAN_TGT:
		ata_scan_bus(start_ccb->ccb_h.path->periph, start_ccb);
		break;
	case XPT_SCAN_LUN:
		ata_scan_lun(start_ccb->ccb_h.path->periph,
			      start_ccb->ccb_h.path, start_ccb->crcn.flags,
			      start_ccb);
		break;
	case XPT_GET_TRAN_SETTINGS:
	{
		ata_get_transfer_settings(&start_ccb->cts);
		break;
	}
	case XPT_SCSI_IO:
	{
		struct cam_ed *device;
		u_int	maxlen = 0;

		device = start_ccb->ccb_h.path->device;
		if (device->protocol == PROTO_SCSI &&
		    (device->flags & CAM_DEV_IDENTIFY_DATA_VALID)) {
			uint16_t p =
			    device->ident_data.config & ATA_PROTO_MASK;

			maxlen =
			    (device->ident_data.config == ATA_PROTO_CFA) ? 0 :
			    (p == ATA_PROTO_ATAPI_16) ? 16 :
			    (p == ATA_PROTO_ATAPI_12) ? 12 : 0;
		}
		if (start_ccb->csio.cdb_len > maxlen) {
			start_ccb->ccb_h.status = CAM_REQ_INVALID;
			xpt_done(start_ccb);
			break;
		}
		xpt_action_default(start_ccb);
		break;
	}
	case XPT_DEV_ADVINFO:
	{
		ata_dev_advinfo(start_ccb);
		break;
	}
	default:
		xpt_action_default(start_ccb);
		break;
	}
}

static void
ata_get_transfer_settings(struct ccb_trans_settings *cts)
{
	struct	ccb_trans_settings_ata *ata;
	struct	ccb_trans_settings_scsi *scsi;
	struct	cam_ed *device;

	device = cts->ccb_h.path->device;
	xpt_action_default((union ccb *)cts);

	if (cts->protocol == PROTO_UNKNOWN ||
	    cts->protocol == PROTO_UNSPECIFIED) {
		cts->protocol = device->protocol;
		cts->protocol_version = device->protocol_version;
	}

	if (cts->protocol == PROTO_ATA) {
		ata = &cts->proto_specific.ata;
		if ((ata->valid & CTS_ATA_VALID_TQ) == 0) {
			ata->valid |= CTS_ATA_VALID_TQ;
			if (cts->type == CTS_TYPE_USER_SETTINGS ||
			    (device->flags & CAM_DEV_TAG_AFTER_COUNT) != 0 ||
			    (device->inq_flags & SID_CmdQue) != 0)
				ata->flags |= CTS_ATA_FLAGS_TAG_ENB;
		}
	}
	if (cts->protocol == PROTO_SCSI) {
		scsi = &cts->proto_specific.scsi;
		if ((scsi->valid & CTS_SCSI_VALID_TQ) == 0) {
			scsi->valid |= CTS_SCSI_VALID_TQ;
			if (cts->type == CTS_TYPE_USER_SETTINGS ||
			    (device->flags & CAM_DEV_TAG_AFTER_COUNT) != 0 ||
			    (device->inq_flags & SID_CmdQue) != 0)
				scsi->flags |= CTS_SCSI_FLAGS_TAG_ENB;
		}
	}

	if (cts->transport == XPORT_UNKNOWN ||
	    cts->transport == XPORT_UNSPECIFIED) {
		cts->transport = device->transport;
		cts->transport_version = device->transport_version;
	}
}

static void
ata_set_transfer_settings(struct ccb_trans_settings *cts, struct cam_path *path,
			   int async_update)
{
	struct	ccb_pathinq cpi;
	struct	ccb_trans_settings_ata *ata;
	struct	ccb_trans_settings_scsi *scsi;
	struct	ata_params *ident_data;
	struct	scsi_inquiry_data *inq_data;
	struct	cam_ed *device;

	if (path == NULL || (device = path->device) == NULL) {
		cts->ccb_h.status = CAM_PATH_INVALID;
		xpt_done((union ccb *)cts);
		return;
	}

	if (cts->protocol == PROTO_UNKNOWN
	 || cts->protocol == PROTO_UNSPECIFIED) {
		cts->protocol = device->protocol;
		cts->protocol_version = device->protocol_version;
	}

	if (cts->protocol_version == PROTO_VERSION_UNKNOWN
	 || cts->protocol_version == PROTO_VERSION_UNSPECIFIED)
		cts->protocol_version = device->protocol_version;

	if (cts->protocol != device->protocol) {
		xpt_print(path, "Uninitialized Protocol %x:%x?\n",
		       cts->protocol, device->protocol);
		cts->protocol = device->protocol;
	}

	if (cts->protocol_version > device->protocol_version) {
		if (bootverbose) {
			xpt_print(path, "Down reving Protocol "
			    "Version from %d to %d?\n", cts->protocol_version,
			    device->protocol_version);
		}
		cts->protocol_version = device->protocol_version;
	}

	if (cts->transport == XPORT_UNKNOWN
	 || cts->transport == XPORT_UNSPECIFIED) {
		cts->transport = device->transport;
		cts->transport_version = device->transport_version;
	}

	if (cts->transport_version == XPORT_VERSION_UNKNOWN
	 || cts->transport_version == XPORT_VERSION_UNSPECIFIED)
		cts->transport_version = device->transport_version;

	if (cts->transport != device->transport) {
		xpt_print(path, "Uninitialized Transport %x:%x?\n",
		    cts->transport, device->transport);
		cts->transport = device->transport;
	}

	if (cts->transport_version > device->transport_version) {
		if (bootverbose) {
			xpt_print(path, "Down reving Transport "
			    "Version from %d to %d?\n", cts->transport_version,
			    device->transport_version);
		}
		cts->transport_version = device->transport_version;
	}

	ident_data = &device->ident_data;
	inq_data = &device->inq_data;
	if (cts->protocol == PROTO_ATA)
		ata = &cts->proto_specific.ata;
	else
		ata = NULL;
	if (cts->protocol == PROTO_SCSI)
		scsi = &cts->proto_specific.scsi;
	else
		scsi = NULL;
	xpt_path_inq(&cpi, path);

	/* Sanity checking */
	if ((cpi.hba_inquiry & PI_TAG_ABLE) == 0
	 || (ata && (ident_data->satacapabilities & ATA_SUPPORT_NCQ) == 0)
	 || (scsi && (INQ_DATA_TQ_ENABLED(inq_data)) == 0)
	 || (device->queue_flags & SCP_QUEUE_DQUE) != 0
	 || (device->mintags == 0)) {
		/*
		 * Can't tag on hardware that doesn't support tags,
		 * doesn't have it enabled, or has broken tag support.
		 */
		if (ata)
			ata->flags &= ~CTS_ATA_FLAGS_TAG_ENB;
		if (scsi)
			scsi->flags &= ~CTS_SCSI_FLAGS_TAG_ENB;
	}

	/* Start/stop tags use. */
	if (cts->type == CTS_TYPE_CURRENT_SETTINGS &&
	    ((ata && (ata->valid & CTS_ATA_VALID_TQ) != 0) ||
	     (scsi && (scsi->valid & CTS_SCSI_VALID_TQ) != 0))) {
		int nowt, newt = 0;

		nowt = ((device->flags & CAM_DEV_TAG_AFTER_COUNT) != 0 ||
			(device->inq_flags & SID_CmdQue) != 0);
		if (ata)
			newt = (ata->flags & CTS_ATA_FLAGS_TAG_ENB) != 0;
		if (scsi)
			newt = (scsi->flags & CTS_SCSI_FLAGS_TAG_ENB) != 0;

		if (newt && !nowt) {
			/*
			 * Delay change to use tags until after a
			 * few commands have gone to this device so
			 * the controller has time to perform transfer
			 * negotiations without tagged messages getting
			 * in the way.
			 */
			device->tag_delay_count = CAM_TAG_DELAY_COUNT;
			device->flags |= CAM_DEV_TAG_AFTER_COUNT;
		} else if (nowt && !newt)
			xpt_stop_tags(path);
	}

	if (async_update == FALSE)
		xpt_action_default((union ccb *)cts);
}

/*
 * Handle any per-device event notifications that require action by the XPT.
 */
static void
ata_dev_async(u_int32_t async_code, struct cam_eb *bus, struct cam_et *target,
	      struct cam_ed *device, void *async_arg)
{
	cam_status status;
	struct cam_path newpath;

	/*
	 * We only need to handle events for real devices.
	 */
	if (target->target_id == CAM_TARGET_WILDCARD
	 || device->lun_id == CAM_LUN_WILDCARD)
		return;

	/*
	 * We need our own path with wildcards expanded to
	 * handle certain types of events.
	 */
	if ((async_code == AC_SENT_BDR)
	 || (async_code == AC_BUS_RESET)
	 || (async_code == AC_INQ_CHANGED))
		status = xpt_compile_path(&newpath, NULL,
					  bus->path_id,
					  target->target_id,
					  device->lun_id);
	else
		status = CAM_REQ_CMP_ERR;

	if (status == CAM_REQ_CMP) {
		if (async_code == AC_INQ_CHANGED) {
			/*
			 * We've sent a start unit command, or
			 * something similar to a device that
			 * may have caused its inquiry data to
			 * change. So we re-scan the device to
			 * refresh the inquiry data for it.
			 */
			ata_scan_lun(newpath.periph, &newpath,
				     CAM_EXPECT_INQ_CHANGE, NULL);
		} else {
			/* We need to reinitialize device after reset. */
			ata_scan_lun(newpath.periph, &newpath,
				     0, NULL);
		}
		xpt_release_path(&newpath);
	} else if (async_code == AC_LOST_DEVICE &&
	    (device->flags & CAM_DEV_UNCONFIGURED) == 0) {
		device->flags |= CAM_DEV_UNCONFIGURED;
		xpt_release_device(device);
	} else if (async_code == AC_TRANSFER_NEG) {
		struct ccb_trans_settings *settings;
		struct cam_path path;

		settings = (struct ccb_trans_settings *)async_arg;
		xpt_compile_path(&path, NULL, bus->path_id, target->target_id,
				 device->lun_id);
		ata_set_transfer_settings(settings, &path,
					  /*async_update*/TRUE);
		xpt_release_path(&path);
	}
}

static void
_ata_announce_periph(struct cam_periph *periph, struct ccb_trans_settings *cts, u_int *speed)
{
	struct	ccb_pathinq cpi;
	struct	cam_path *path = periph->path;

	cam_periph_assert(periph, MA_OWNED);

	xpt_setup_ccb(&cts->ccb_h, path, CAM_PRIORITY_NORMAL);
	cts->ccb_h.func_code = XPT_GET_TRAN_SETTINGS;
	cts->type = CTS_TYPE_CURRENT_SETTINGS;
	xpt_action((union ccb*)cts);
	if ((cts->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP)
		return;
	/* Ask the SIM for its base transfer speed */
	xpt_path_inq(&cpi, path);
	/* Report connection speed */
	*speed = cpi.base_transfer_speed;
	if (cts->transport == XPORT_ATA) {
		struct	ccb_trans_settings_pata *pata =
		    &cts->xport_specific.ata;

		if (pata->valid & CTS_ATA_VALID_MODE)
			*speed = ata_mode2speed(pata->mode);
	}
	if (cts->transport == XPORT_SATA) {
		struct	ccb_trans_settings_sata *sata =
		    &cts->xport_specific.sata;

		if (sata->valid & CTS_SATA_VALID_REVISION)
			*speed = ata_revision2speed(sata->revision);
	}
}

static void
ata_announce_periph(struct cam_periph *periph)
{
	struct ccb_trans_settings cts;
	u_int speed, mb;

	_ata_announce_periph(periph, &cts, &speed);
	if ((cts.ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP)
		return;

	mb = speed / 1000;
	if (mb > 0)
		printf("%s%d: %d.%03dMB/s transfers",
		       periph->periph_name, periph->unit_number,
		       mb, speed % 1000);
	else
		printf("%s%d: %dKB/s transfers", periph->periph_name,
		       periph->unit_number, speed);
	/* Report additional information about connection */
	if (cts.transport == XPORT_ATA) {
		struct ccb_trans_settings_pata *pata =
		    &cts.xport_specific.ata;

		printf(" (");
		if (pata->valid & CTS_ATA_VALID_MODE)
			printf("%s, ", ata_mode2string(pata->mode));
		if ((pata->valid & CTS_ATA_VALID_ATAPI) && pata->atapi != 0)
			printf("ATAPI %dbytes, ", pata->atapi);
		if (pata->valid & CTS_ATA_VALID_BYTECOUNT)
			printf("PIO %dbytes", pata->bytecount);
		printf(")");
	}
	if (cts.transport == XPORT_SATA) {
		struct ccb_trans_settings_sata *sata =
		    &cts.xport_specific.sata;

		printf(" (");
		if (sata->valid & CTS_SATA_VALID_REVISION)
			printf("SATA %d.x, ", sata->revision);
		else
			printf("SATA, ");
		if (sata->valid & CTS_SATA_VALID_MODE)
			printf("%s, ", ata_mode2string(sata->mode));
		if ((sata->valid & CTS_ATA_VALID_ATAPI) && sata->atapi != 0)
			printf("ATAPI %dbytes, ", sata->atapi);
		if (sata->valid & CTS_SATA_VALID_BYTECOUNT)
			printf("PIO %dbytes", sata->bytecount);
		printf(")");
	}
	printf("\n");
}

static void
ata_announce_periph_sbuf(struct cam_periph *periph, struct sbuf *sb)
{
	struct ccb_trans_settings cts;
	u_int speed, mb;

	_ata_announce_periph(periph, &cts, &speed);
	if ((cts.ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP)
		return;

	mb = speed / 1000;
	if (mb > 0)
		sbuf_printf(sb, "%s%d: %d.%03dMB/s transfers",
		       periph->periph_name, periph->unit_number,
		       mb, speed % 1000);
	else
		sbuf_printf(sb, "%s%d: %dKB/s transfers", periph->periph_name,
		       periph->unit_number, speed);
	/* Report additional information about connection */
	if (cts.transport == XPORT_ATA) {
		struct ccb_trans_settings_pata *pata =
		    &cts.xport_specific.ata;

		sbuf_printf(sb, " (");
		if (pata->valid & CTS_ATA_VALID_MODE)
			sbuf_printf(sb, "%s, ", ata_mode2string(pata->mode));
		if ((pata->valid & CTS_ATA_VALID_ATAPI) && pata->atapi != 0)
			sbuf_printf(sb, "ATAPI %dbytes, ", pata->atapi);
		if (pata->valid & CTS_ATA_VALID_BYTECOUNT)
			sbuf_printf(sb, "PIO %dbytes", pata->bytecount);
		sbuf_printf(sb, ")");
	}
	if (cts.transport == XPORT_SATA) {
		struct ccb_trans_settings_sata *sata =
		    &cts.xport_specific.sata;

		sbuf_printf(sb, " (");
		if (sata->valid & CTS_SATA_VALID_REVISION)
			sbuf_printf(sb, "SATA %d.x, ", sata->revision);
		else
			sbuf_printf(sb, "SATA, ");
		if (sata->valid & CTS_SATA_VALID_MODE)
			sbuf_printf(sb, "%s, ", ata_mode2string(sata->mode));
		if ((sata->valid & CTS_ATA_VALID_ATAPI) && sata->atapi != 0)
			sbuf_printf(sb, "ATAPI %dbytes, ", sata->atapi);
		if (sata->valid & CTS_SATA_VALID_BYTECOUNT)
			sbuf_printf(sb, "PIO %dbytes", sata->bytecount);
		sbuf_printf(sb, ")");
	}
	sbuf_printf(sb, "\n");
}

static void
ata_proto_announce_sbuf(struct cam_ed *device, struct sbuf *sb)
{
	ata_print_ident_sbuf(&device->ident_data, sb);
}

static void
ata_proto_announce(struct cam_ed *device)
{
	ata_print_ident(&device->ident_data);
}

static void
ata_proto_denounce(struct cam_ed *device)
{
	ata_print_ident_short(&device->ident_data);
}

static void
ata_proto_denounce_sbuf(struct cam_ed *device, struct sbuf *sb)
{
	ata_print_ident_short_sbuf(&device->ident_data, sb);
}

static void
semb_proto_announce_sbuf(struct cam_ed *device, struct sbuf *sb)
{
	semb_print_ident_sbuf((struct sep_identify_data *)&device->ident_data, sb);
}

static void
semb_proto_announce(struct cam_ed *device)
{
	semb_print_ident((struct sep_identify_data *)&device->ident_data);
}

static void
semb_proto_denounce(struct cam_ed *device)
{
	semb_print_ident_short((struct sep_identify_data *)&device->ident_data);
}

static void
semb_proto_denounce_sbuf(struct cam_ed *device, struct sbuf *sb)
{
	semb_print_ident_short_sbuf((struct sep_identify_data *)&device->ident_data, sb);
}

static void
ata_proto_debug_out(union ccb *ccb)
{
	char cdb_str[(sizeof(struct ata_cmd) * 3) + 1];

	if (ccb->ccb_h.func_code != XPT_ATA_IO)
		return;

	CAM_DEBUG(ccb->ccb_h.path,
	    CAM_DEBUG_CDB,("%s. ACB: %s\n", ata_op_string(&ccb->ataio.cmd),
		ata_cmd_string(&ccb->ataio.cmd, cdb_str, sizeof(cdb_str))));
}
