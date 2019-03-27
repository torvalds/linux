/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Alexander Motin <mav@FreeBSD.org>
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
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/endian.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <machine/stdarg.h>
#include <machine/resource.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <dev/led/led.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include "ahci.h"

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_sim.h>
#include <cam/cam_xpt_sim.h>
#include <cam/cam_debug.h>
#include <cam/scsi/scsi_ses.h>

/* local prototypes */
static void ahciemaction(struct cam_sim *sim, union ccb *ccb);
static void ahciempoll(struct cam_sim *sim);
static int ahci_em_reset(device_t dev);
static void ahci_em_led(void *priv, int onoff);
static void ahci_em_setleds(device_t dev, int c);

static int
ahci_em_probe(device_t dev)
{

	device_set_desc_copy(dev, "AHCI enclosure management bridge");
	return (BUS_PROBE_DEFAULT);
}

static int
ahci_em_attach(device_t dev)
{
	device_t parent = device_get_parent(dev);
	struct ahci_controller *ctlr = device_get_softc(parent);
	struct ahci_enclosure *enc = device_get_softc(dev);
	struct cam_devq *devq;
	int i, c, rid, error;
	char buf[32];

	enc->dev = dev;
	enc->quirks = ctlr->quirks;
	enc->channels = ctlr->channels;
	enc->ichannels = ctlr->ichannels;
	mtx_init(&enc->mtx, "AHCI enclosure lock", NULL, MTX_DEF);
	rid = 0;
	if (!(enc->r_memc = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &rid, RF_ACTIVE))) {
		mtx_destroy(&enc->mtx);
		return (ENXIO);
	}
	enc->capsem = ATA_INL(enc->r_memc, 0);
	rid = 1;
	if (!(enc->r_memt = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &rid, RF_ACTIVE))) {
		error = ENXIO;
		goto err0;
	}
	if ((enc->capsem & (AHCI_EM_XMT | AHCI_EM_SMB)) == 0) {
		rid = 2;
		if (!(enc->r_memr = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
		    &rid, RF_ACTIVE))) {
			error = ENXIO;
			goto err0;
		}
	} else
		enc->r_memr = NULL;
	mtx_lock(&enc->mtx);
	if (ahci_em_reset(dev) != 0) {
	    error = ENXIO;
	    goto err1;
	}
	rid = ATA_IRQ_RID;
	/* Create the device queue for our SIM. */
	devq = cam_simq_alloc(1);
	if (devq == NULL) {
		device_printf(dev, "Unable to allocate SIM queue\n");
		error = ENOMEM;
		goto err1;
	}
	/* Construct SIM entry */
	enc->sim = cam_sim_alloc(ahciemaction, ahciempoll, "ahciem", enc,
	    device_get_unit(dev), &enc->mtx,
	    1, 0, devq);
	if (enc->sim == NULL) {
		cam_simq_free(devq);
		device_printf(dev, "Unable to allocate SIM\n");
		error = ENOMEM;
		goto err1;
	}
	if (xpt_bus_register(enc->sim, dev, 0) != CAM_SUCCESS) {
		device_printf(dev, "unable to register xpt bus\n");
		error = ENXIO;
		goto err2;
	}
	if (xpt_create_path(&enc->path, /*periph*/NULL, cam_sim_path(enc->sim),
	    CAM_TARGET_WILDCARD, CAM_LUN_WILDCARD) != CAM_REQ_CMP) {
		device_printf(dev, "Unable to create path\n");
		error = ENXIO;
		goto err3;
	}
	mtx_unlock(&enc->mtx);
	if (bootverbose) {
		device_printf(dev, "Caps:%s%s%s%s%s%s%s%s\n",
		    (enc->capsem & AHCI_EM_PM) ? " PM":"",
		    (enc->capsem & AHCI_EM_ALHD) ? " ALHD":"",
		    (enc->capsem & AHCI_EM_XMT) ? " XMT":"",
		    (enc->capsem & AHCI_EM_SMB) ? " SMB":"",
		    (enc->capsem & AHCI_EM_SGPIO) ? " SGPIO":"",
		    (enc->capsem & AHCI_EM_SES2) ? " SES-2":"",
		    (enc->capsem & AHCI_EM_SAFTE) ? " SAF-TE":"",
		    (enc->capsem & AHCI_EM_LED) ? " LED":"");
	}
	if ((enc->capsem & AHCI_EM_LED)) {
		for (c = 0; c < enc->channels; c++) {
			if ((enc->ichannels & (1 << c)) == 0)
				continue;
			for (i = 0; i < AHCI_NUM_LEDS; i++) {
				enc->leds[c * AHCI_NUM_LEDS + i].dev = dev;
				enc->leds[c * AHCI_NUM_LEDS + i].num =
				    c * AHCI_NUM_LEDS + i;
			}
			if ((enc->capsem & AHCI_EM_ALHD) == 0) {
				snprintf(buf, sizeof(buf), "%s.%d.act",
				    device_get_nameunit(parent), c);
				enc->leds[c * AHCI_NUM_LEDS + 0].led =
				    led_create(ahci_em_led,
				    &enc->leds[c * AHCI_NUM_LEDS + 0], buf);
			}
			snprintf(buf, sizeof(buf), "%s.%d.locate",
			    device_get_nameunit(parent), c);
			enc->leds[c * AHCI_NUM_LEDS + 1].led =
			    led_create(ahci_em_led,
			    &enc->leds[c * AHCI_NUM_LEDS + 1], buf);
			snprintf(buf, sizeof(buf), "%s.%d.fault",
			    device_get_nameunit(parent), c);
			enc->leds[c * AHCI_NUM_LEDS + 2].led =
			    led_create(ahci_em_led,
			    &enc->leds[c * AHCI_NUM_LEDS + 2], buf);
		}
	}
	return (0);

err3:
	xpt_bus_deregister(cam_sim_path(enc->sim));
err2:
	cam_sim_free(enc->sim, /*free_devq*/TRUE);
err1:
	mtx_unlock(&enc->mtx);
	if (enc->r_memr)
		bus_release_resource(dev, SYS_RES_MEMORY, 2, enc->r_memr);
err0:
	if (enc->r_memt)
		bus_release_resource(dev, SYS_RES_MEMORY, 1, enc->r_memt);
	bus_release_resource(dev, SYS_RES_MEMORY, 0, enc->r_memc);
	mtx_destroy(&enc->mtx);
	return (error);
}

static int
ahci_em_detach(device_t dev)
{
	struct ahci_enclosure *enc = device_get_softc(dev);
	int i;

	for (i = 0; i < enc->channels * AHCI_NUM_LEDS; i++) {
		if (enc->leds[i].led)
			led_destroy(enc->leds[i].led);
	}
	mtx_lock(&enc->mtx);
	xpt_async(AC_LOST_DEVICE, enc->path, NULL);
	xpt_free_path(enc->path);
	xpt_bus_deregister(cam_sim_path(enc->sim));
	cam_sim_free(enc->sim, /*free_devq*/TRUE);
	mtx_unlock(&enc->mtx);

	bus_release_resource(dev, SYS_RES_MEMORY, 0, enc->r_memc);
	bus_release_resource(dev, SYS_RES_MEMORY, 1, enc->r_memt);
	if (enc->r_memr)
		bus_release_resource(dev, SYS_RES_MEMORY, 2, enc->r_memr);
	mtx_destroy(&enc->mtx);
	return (0);
}

static int
ahci_em_reset(device_t dev)
{
	struct ahci_enclosure *enc;
	int i, timeout;

	enc = device_get_softc(dev);
	ATA_OUTL(enc->r_memc, 0, AHCI_EM_RST);
	timeout = 1000;
	while ((ATA_INL(enc->r_memc, 0) & AHCI_EM_RST) &&
	    --timeout > 0)
		DELAY(1000);
	if (timeout == 0) {
		device_printf(dev, "EM timeout\n");
		return (1);
	}
	for (i = 0; i < enc->channels; i++)
		ahci_em_setleds(dev, i);
	return (0);
}

static int
ahci_em_suspend(device_t dev)
{
	struct ahci_enclosure *enc = device_get_softc(dev);

	mtx_lock(&enc->mtx);
	xpt_freeze_simq(enc->sim, 1);
	mtx_unlock(&enc->mtx);
	return (0);
}

static int
ahci_em_resume(device_t dev)
{
	struct ahci_enclosure *enc = device_get_softc(dev);

	mtx_lock(&enc->mtx);
	ahci_em_reset(dev);
	xpt_release_simq(enc->sim, TRUE);
	mtx_unlock(&enc->mtx);
	return (0);
}

devclass_t ahciem_devclass;
static device_method_t ahciem_methods[] = {
	DEVMETHOD(device_probe,     ahci_em_probe),
	DEVMETHOD(device_attach,    ahci_em_attach),
	DEVMETHOD(device_detach,    ahci_em_detach),
	DEVMETHOD(device_suspend,   ahci_em_suspend),
	DEVMETHOD(device_resume,    ahci_em_resume),
	DEVMETHOD_END
};
static driver_t ahciem_driver = {
        "ahciem",
        ahciem_methods,
        sizeof(struct ahci_enclosure)
};
DRIVER_MODULE(ahciem, ahci, ahciem_driver, ahciem_devclass, NULL, NULL);

static void
ahci_em_setleds(device_t dev, int c)
{
	struct ahci_enclosure *enc;
	int timeout;
	int16_t val;

	enc = device_get_softc(dev);

	val = 0;
	if (enc->status[c][2] & 0x80)		/* Activity */
		val |= (1 << 0);
	if (enc->status[c][2] & SESCTL_RQSID)	/* Identification */
		val |= (1 << 3);
	else if (enc->status[c][3] & SESCTL_RQSFLT)	/* Fault */
		val |= (1 << 6);
	else if (enc->status[c][1] & 0x02)		/* Rebuild */
		val |= (1 << 6) | (1 << 3);

	timeout = 10000;
	while (ATA_INL(enc->r_memc, 0) & (AHCI_EM_TM | AHCI_EM_RST) &&
	    --timeout > 0)
		DELAY(100);
	if (timeout == 0)
		device_printf(dev, "Transmit timeout\n");
	ATA_OUTL(enc->r_memt, 0, (1 << 8) | (0 << 16) | (0 << 24));
	ATA_OUTL(enc->r_memt, 4, c | (0 << 8) | (val << 16));
	ATA_OUTL(enc->r_memc, 0, AHCI_EM_TM);
}

static void
ahci_em_led(void *priv, int onoff)
{
	struct ahci_led *led;
	struct ahci_enclosure *enc;
	int c, l;

	led = (struct ahci_led *)priv;
	enc = device_get_softc(led->dev);
	c = led->num / AHCI_NUM_LEDS;
	l = led->num % AHCI_NUM_LEDS;

	if (l == 0) {
		if (onoff)
			enc->status[c][2] |= 0x80;
		else
			enc->status[c][2] &= ~0x80;
	} else if (l == 1) {
		if (onoff)
			enc->status[c][2] |= SESCTL_RQSID;
		else
			enc->status[c][2] &= ~SESCTL_RQSID;
	} else if (l == 2) {
		if (onoff)
			enc->status[c][3] |= SESCTL_RQSFLT;
		else
			enc->status[c][3] &= SESCTL_RQSFLT;
	}
	ahci_em_setleds(led->dev, c);
}

static int
ahci_check_ids(union ccb *ccb)
{

	if (ccb->ccb_h.target_id != 0) {
		ccb->ccb_h.status = CAM_TID_INVALID;
		xpt_done(ccb);
		return (-1);
	}
	if (ccb->ccb_h.target_lun != 0) {
		ccb->ccb_h.status = CAM_LUN_INVALID;
		xpt_done(ccb);
		return (-1);
	}
	return (0);
}

static void
ahci_em_emulate_ses_on_led(device_t dev, union ccb *ccb)
{
	struct ahci_enclosure *enc;
	struct ses_status_page *page;
	struct ses_status_array_dev_slot *ads, *ads0;
	struct ses_elm_desc_hdr *elmd;
	uint8_t *buf;
	int i;

	enc = device_get_softc(dev);
	buf = ccb->ataio.data_ptr;

	/* General request validation. */
	if (ccb->ataio.cmd.command != ATA_SEP_ATTN ||
	    ccb->ataio.dxfer_len < ccb->ataio.cmd.sector_count * 4) {
		ccb->ccb_h.status = CAM_REQ_INVALID;
		goto out;
	}

	/* SEMB IDENTIFY */
	if (ccb->ataio.cmd.features == 0xEC &&
	    ccb->ataio.cmd.sector_count >= 16) {
		bzero(buf, ccb->ataio.dxfer_len);
		buf[0] = 64;		/* Valid bytes. */
		buf[2] = 0x30;		/* NAA Locally Assigned. */
		strncpy(&buf[3], device_get_nameunit(dev), 7);
		strncpy(&buf[10], "AHCI    ", SID_VENDOR_SIZE);
		strncpy(&buf[18], "SGPIO Enclosure ", SID_PRODUCT_SIZE);
		strncpy(&buf[34], "1.00", SID_REVISION_SIZE);
		strncpy(&buf[39], "0001", 4);
		strncpy(&buf[43], "S-E-S ", 6);
		strncpy(&buf[49], "2.00", 4);
		ccb->ccb_h.status = CAM_REQ_CMP;
		goto out;
	}

	/* SEMB RECEIVE DIAGNOSTIC RESULT (0) */
	page = (struct ses_status_page *)buf;
	if (ccb->ataio.cmd.lba_low == 0x02 &&
	    ccb->ataio.cmd.features == 0x00 &&
	    ccb->ataio.cmd.sector_count >= 2) {
		bzero(buf, ccb->ataio.dxfer_len);
		page->hdr.page_code = 0;
		scsi_ulto2b(4, page->hdr.length);
		buf[4] = 0;
		buf[5] = 1;
		buf[6] = 2;
		buf[7] = 7;
		ccb->ccb_h.status = CAM_REQ_CMP;
		goto out;
	}

	/* SEMB RECEIVE DIAGNOSTIC RESULT (1) */
	if (ccb->ataio.cmd.lba_low == 0x02 &&
	    ccb->ataio.cmd.features == 0x01 &&
	    ccb->ataio.cmd.sector_count >= 13) {
		struct ses_enc_desc *ed;
		struct ses_elm_type_desc *td;

		bzero(buf, ccb->ataio.dxfer_len);
		page->hdr.page_code = 0x01;
		scsi_ulto2b(4 + 4 + 36 + 4, page->hdr.length);
		ed = (struct ses_enc_desc *)&buf[8];
		ed->byte0 = 0x11;
		ed->subenc_id = 0;
		ed->num_types = 1;
		ed->length = 36;
		strncpy(ed->vendor_id, "AHCI    ", SID_VENDOR_SIZE);
		strncpy(ed->product_id, "SGPIO Enclosure ", SID_PRODUCT_SIZE);
		strncpy(ed->product_rev, "    ", SID_REVISION_SIZE);
		td = (struct ses_elm_type_desc *)ses_enc_desc_next(ed);
		td->etype_elm_type = 0x17;
		td->etype_maxelt = enc->channels;
		td->etype_subenc = 0;
		td->etype_txt_len = 0;
		ccb->ccb_h.status = CAM_REQ_CMP;
		goto out;
	}

	/* SEMB RECEIVE DIAGNOSTIC RESULT (2) */
	if (ccb->ataio.cmd.lba_low == 0x02 &&
	    ccb->ataio.cmd.features == 0x02 &&
	    ccb->ataio.cmd.sector_count >= (3 + enc->channels)) {
		bzero(buf, ccb->ataio.dxfer_len);
		page->hdr.page_code = 0x02;
		scsi_ulto2b(4 + 4 * (1 + enc->channels),
		    page->hdr.length);
		for (i = 0; i < enc->channels; i++) {
			ads = &page->elements[i + 1].array_dev_slot;
			memcpy(ads, enc->status[i], 4);
			ads->common.bytes[0] |=
			    (enc->ichannels & (1 << i)) ?
			     SES_OBJSTAT_UNKNOWN :
			     SES_OBJSTAT_NOTINSTALLED;
		}
		ccb->ccb_h.status = CAM_REQ_CMP;
		goto out;
	}

	/* SEMB SEND DIAGNOSTIC (2) */
	if (ccb->ataio.cmd.lba_low == 0x82 &&
	    ccb->ataio.cmd.features == 0x02 &&
	    ccb->ataio.cmd.sector_count >= (3 + enc->channels)) {
		ads0 = &page->elements[0].array_dev_slot;
		for (i = 0; i < enc->channels; i++) {
			ads = &page->elements[i + 1].array_dev_slot;
			if (ads->common.bytes[0] & SESCTL_CSEL) {
				enc->status[i][0] = 0;
				enc->status[i][1] = 
				    ads->bytes[0] & 0x02;
				enc->status[i][2] =
				    ads->bytes[1] & (0x80 | SESCTL_RQSID);
				enc->status[i][3] =
				    ads->bytes[2] & SESCTL_RQSFLT;
				ahci_em_setleds(dev, i);
			} else if (ads0->common.bytes[0] & SESCTL_CSEL) {
				enc->status[i][0] = 0;
				enc->status[i][1] = 
				    ads0->bytes[0] & 0x02;
				enc->status[i][2] =
				    ads0->bytes[1] & (0x80 | SESCTL_RQSID);
				enc->status[i][3] =
				    ads0->bytes[2] & SESCTL_RQSFLT;
				ahci_em_setleds(dev, i);
			}
		}
		ccb->ccb_h.status = CAM_REQ_CMP;
		goto out;
	}

	/* SEMB RECEIVE DIAGNOSTIC RESULT (7) */
	if (ccb->ataio.cmd.lba_low == 0x02 &&
	    ccb->ataio.cmd.features == 0x07 &&
	    ccb->ataio.cmd.sector_count >= (3 + 3 * enc->channels)) {
		bzero(buf, ccb->ataio.dxfer_len);
		page->hdr.page_code = 0x07;
		scsi_ulto2b(4 + 4 + 12 * enc->channels,
		    page->hdr.length);
		for (i = 0; i < enc->channels; i++) {
			elmd = (struct ses_elm_desc_hdr *)&buf[8 + 4 + 12 * i];
			scsi_ulto2b(8, elmd->length);
			snprintf((char *)(elmd + 1), 9, "SLOT %03d", i);
		}
		ccb->ccb_h.status = CAM_REQ_CMP;
		goto out;
	}

	ccb->ccb_h.status = CAM_REQ_INVALID;
out:
	xpt_done(ccb);
}

static void
ahci_em_begin_transaction(device_t dev, union ccb *ccb)
{
	struct ahci_enclosure *enc;
	struct ata_res *res;

	enc = device_get_softc(dev);
	res = &ccb->ataio.res;
	bzero(res, sizeof(*res));
	if ((ccb->ataio.cmd.flags & CAM_ATAIO_CONTROL) &&
	    (ccb->ataio.cmd.control & ATA_A_RESET)) {
		res->lba_high = 0xc3;
		res->lba_mid = 0x3c;
		ccb->ccb_h.status = CAM_REQ_CMP;
		xpt_done(ccb);
		return;
	}

	if (enc->capsem & AHCI_EM_LED) {
		ahci_em_emulate_ses_on_led(dev, ccb);
		return;
	} else
		device_printf(dev, "Unsupported enclosure interface\n");

	ccb->ccb_h.status = CAM_REQ_INVALID;
	xpt_done(ccb);
}

static void
ahciemaction(struct cam_sim *sim, union ccb *ccb)
{
	device_t dev, parent;
	struct ahci_enclosure *enc;

	CAM_DEBUG(ccb->ccb_h.path, CAM_DEBUG_TRACE,
	    ("ahciemaction func_code=%x\n", ccb->ccb_h.func_code));

	enc = cam_sim_softc(sim);
	dev = enc->dev;
	switch (ccb->ccb_h.func_code) {
	case XPT_ATA_IO:	/* Execute the requested I/O operation */
		if (ahci_check_ids(ccb))
			return;
		ahci_em_begin_transaction(dev, ccb);
		return;
	case XPT_RESET_BUS:		/* Reset the specified bus */
	case XPT_RESET_DEV:	/* Bus Device Reset the specified device */
		ahci_em_reset(dev);
		ccb->ccb_h.status = CAM_REQ_CMP;
		break;
	case XPT_PATH_INQ:		/* Path routing inquiry */
	{
		struct ccb_pathinq *cpi = &ccb->cpi;

		parent = device_get_parent(dev);
		cpi->version_num = 1; /* XXX??? */
		cpi->hba_inquiry = PI_SDTR_ABLE;
		cpi->target_sprt = 0;
		cpi->hba_misc = PIM_SEQSCAN;
		cpi->hba_eng_cnt = 0;
		cpi->max_target = 0;
		cpi->max_lun = 0;
		cpi->initiator_id = 0;
		cpi->bus_id = cam_sim_bus(sim);
		cpi->base_transfer_speed = 150000;
		strlcpy(cpi->sim_vid, "FreeBSD", SIM_IDLEN);
		strlcpy(cpi->hba_vid, "AHCI", HBA_IDLEN);
		strlcpy(cpi->dev_name, cam_sim_name(sim), DEV_IDLEN);
		cpi->unit_number = cam_sim_unit(sim);
		cpi->transport = XPORT_SATA;
		cpi->transport_version = XPORT_VERSION_UNSPECIFIED;
		cpi->protocol = PROTO_ATA;
		cpi->protocol_version = PROTO_VERSION_UNSPECIFIED;
		cpi->maxio = MAXPHYS;
		cpi->hba_vendor = pci_get_vendor(parent);
		cpi->hba_device = pci_get_device(parent);
		cpi->hba_subvendor = pci_get_subvendor(parent);
		cpi->hba_subdevice = pci_get_subdevice(parent);
		cpi->ccb_h.status = CAM_REQ_CMP;
		break;
	}
	default:
		ccb->ccb_h.status = CAM_REQ_INVALID;
		break;
	}
	xpt_done(ccb);
}

static void
ahciempoll(struct cam_sim *sim)
{

}
