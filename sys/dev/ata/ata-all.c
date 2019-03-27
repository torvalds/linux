/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998 - 2008 Søren Schmidt <sos@FreeBSD.org>
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
#include <sys/systm.h>
#include <sys/ata.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/endian.h>
#include <sys/ctype.h>
#include <sys/conf.h>
#include <sys/bus.h>
#include <sys/bio.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include <sys/sema.h>
#include <sys/taskqueue.h>
#include <vm/uma.h>
#include <machine/stdarg.h>
#include <machine/resource.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <dev/ata/ata-all.h>
#include <dev/pci/pcivar.h>
#include <ata_if.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_sim.h>
#include <cam/cam_xpt_sim.h>
#include <cam/cam_debug.h>

/* prototypes */
static void ataaction(struct cam_sim *sim, union ccb *ccb);
static void atapoll(struct cam_sim *sim);
static void ata_cam_begin_transaction(device_t dev, union ccb *ccb);
static void ata_cam_end_transaction(device_t dev, struct ata_request *request);
static void ata_cam_request_sense(device_t dev, struct ata_request *request);
static int ata_check_ids(device_t dev, union ccb *ccb);
static void ata_conn_event(void *context, int dummy);
static void ata_interrupt_locked(void *data);
static int ata_module_event_handler(module_t mod, int what, void *arg);
static void ata_periodic_poll(void *data);
static int ata_str2mode(const char *str);

/* global vars */
MALLOC_DEFINE(M_ATA, "ata_generic", "ATA driver generic layer");
int (*ata_raid_ioctl_func)(u_long cmd, caddr_t data) = NULL;
devclass_t ata_devclass;
int ata_dma_check_80pin = 1;

/* sysctl vars */
static SYSCTL_NODE(_hw, OID_AUTO, ata, CTLFLAG_RD, 0, "ATA driver parameters");
SYSCTL_INT(_hw_ata, OID_AUTO, ata_dma_check_80pin,
	   CTLFLAG_RWTUN, &ata_dma_check_80pin, 0,
	   "Check for 80pin cable before setting ATA DMA mode");
FEATURE(ata_cam, "ATA devices are accessed through the cam(4) driver");

/*
 * newbus device interface related functions
 */
int
ata_probe(device_t dev)
{
    return (BUS_PROBE_LOW_PRIORITY);
}

int
ata_attach(device_t dev)
{
    struct ata_channel *ch = device_get_softc(dev);
    int error, rid;
    struct cam_devq *devq;
    const char *res;
    char buf[64];
    int i, mode;

    /* check that we have a virgin channel to attach */
    if (ch->r_irq)
	return EEXIST;

    /* initialize the softc basics */
    ch->dev = dev;
    ch->state = ATA_IDLE;
    bzero(&ch->state_mtx, sizeof(struct mtx));
    mtx_init(&ch->state_mtx, "ATA state lock", NULL, MTX_DEF);
    TASK_INIT(&ch->conntask, 0, ata_conn_event, dev);
	for (i = 0; i < 16; i++) {
		ch->user[i].revision = 0;
		snprintf(buf, sizeof(buf), "dev%d.sata_rev", i);
		if (resource_int_value(device_get_name(dev),
		    device_get_unit(dev), buf, &mode) != 0 &&
		    resource_int_value(device_get_name(dev),
		    device_get_unit(dev), "sata_rev", &mode) != 0)
			mode = -1;
		if (mode >= 0)
			ch->user[i].revision = mode;
		ch->user[i].mode = 0;
		snprintf(buf, sizeof(buf), "dev%d.mode", i);
		if (resource_string_value(device_get_name(dev),
		    device_get_unit(dev), buf, &res) == 0)
			mode = ata_str2mode(res);
		else if (resource_string_value(device_get_name(dev),
		    device_get_unit(dev), "mode", &res) == 0)
			mode = ata_str2mode(res);
		else
			mode = -1;
		if (mode >= 0)
			ch->user[i].mode = mode;
		if (ch->flags & ATA_SATA)
			ch->user[i].bytecount = 8192;
		else
			ch->user[i].bytecount = MAXPHYS;
		ch->user[i].caps = 0;
		ch->curr[i] = ch->user[i];
		if (ch->flags & ATA_SATA) {
			if (ch->pm_level > 0)
				ch->user[i].caps |= CTS_SATA_CAPS_H_PMREQ;
			if (ch->pm_level > 1)
				ch->user[i].caps |= CTS_SATA_CAPS_D_PMREQ;
		} else {
			if (!(ch->flags & ATA_NO_48BIT_DMA))
				ch->user[i].caps |= CTS_ATA_CAPS_H_DMA48;
		}
	}
	callout_init(&ch->poll_callout, 1);

    /* allocate DMA resources if DMA HW present*/
    if (ch->dma.alloc)
	ch->dma.alloc(dev);

    /* setup interrupt delivery */
    rid = ATA_IRQ_RID;
    ch->r_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
				       RF_SHAREABLE | RF_ACTIVE);
    if (!ch->r_irq) {
	device_printf(dev, "unable to allocate interrupt\n");
	return ENXIO;
    }
    if ((error = bus_setup_intr(dev, ch->r_irq, ATA_INTR_FLAGS, NULL,
				ata_interrupt, ch, &ch->ih))) {
	bus_release_resource(dev, SYS_RES_IRQ, rid, ch->r_irq);
	device_printf(dev, "unable to setup interrupt\n");
	return error;
    }

	if (ch->flags & ATA_PERIODIC_POLL)
		callout_reset(&ch->poll_callout, hz, ata_periodic_poll, ch);
	mtx_lock(&ch->state_mtx);
	/* Create the device queue for our SIM. */
	devq = cam_simq_alloc(1);
	if (devq == NULL) {
		device_printf(dev, "Unable to allocate simq\n");
		error = ENOMEM;
		goto err1;
	}
	/* Construct SIM entry */
	ch->sim = cam_sim_alloc(ataaction, atapoll, "ata", ch,
	    device_get_unit(dev), &ch->state_mtx, 1, 0, devq);
	if (ch->sim == NULL) {
		device_printf(dev, "unable to allocate sim\n");
		cam_simq_free(devq);
		error = ENOMEM;
		goto err1;
	}
	if (xpt_bus_register(ch->sim, dev, 0) != CAM_SUCCESS) {
		device_printf(dev, "unable to register xpt bus\n");
		error = ENXIO;
		goto err2;
	}
	if (xpt_create_path(&ch->path, /*periph*/NULL, cam_sim_path(ch->sim),
	    CAM_TARGET_WILDCARD, CAM_LUN_WILDCARD) != CAM_REQ_CMP) {
		device_printf(dev, "unable to create path\n");
		error = ENXIO;
		goto err3;
	}
	mtx_unlock(&ch->state_mtx);
	return (0);

err3:
	xpt_bus_deregister(cam_sim_path(ch->sim));
err2:
	cam_sim_free(ch->sim, /*free_devq*/TRUE);
	ch->sim = NULL;
err1:
	bus_release_resource(dev, SYS_RES_IRQ, rid, ch->r_irq);
	mtx_unlock(&ch->state_mtx);
	if (ch->flags & ATA_PERIODIC_POLL)
		callout_drain(&ch->poll_callout);
	return (error);
}

int
ata_detach(device_t dev)
{
    struct ata_channel *ch = device_get_softc(dev);

    /* check that we have a valid channel to detach */
    if (!ch->r_irq)
	return ENXIO;

    /* grap the channel lock so no new requests gets launched */
    mtx_lock(&ch->state_mtx);
    ch->state |= ATA_STALL_QUEUE;
    mtx_unlock(&ch->state_mtx);
    if (ch->flags & ATA_PERIODIC_POLL)
	callout_drain(&ch->poll_callout);

    taskqueue_drain(taskqueue_thread, &ch->conntask);

	mtx_lock(&ch->state_mtx);
	xpt_async(AC_LOST_DEVICE, ch->path, NULL);
	xpt_free_path(ch->path);
	xpt_bus_deregister(cam_sim_path(ch->sim));
	cam_sim_free(ch->sim, /*free_devq*/TRUE);
	ch->sim = NULL;
	mtx_unlock(&ch->state_mtx);

    /* release resources */
    bus_teardown_intr(dev, ch->r_irq, ch->ih);
    bus_release_resource(dev, SYS_RES_IRQ, ATA_IRQ_RID, ch->r_irq);
    ch->r_irq = NULL;

    /* free DMA resources if DMA HW present*/
    if (ch->dma.free)
	ch->dma.free(dev);

    mtx_destroy(&ch->state_mtx);
    return 0;
}

static void
ata_conn_event(void *context, int dummy)
{
	device_t dev = (device_t)context;
	struct ata_channel *ch = device_get_softc(dev);
	union ccb *ccb;

	mtx_lock(&ch->state_mtx);
	if (ch->sim == NULL) {
		mtx_unlock(&ch->state_mtx);
		return;
	}
	ata_reinit(dev);
	if ((ccb = xpt_alloc_ccb_nowait()) == NULL)
		return;
	if (xpt_create_path(&ccb->ccb_h.path, NULL,
	    cam_sim_path(ch->sim),
	    CAM_TARGET_WILDCARD, CAM_LUN_WILDCARD) != CAM_REQ_CMP) {
		xpt_free_ccb(ccb);
		return;
	}
	xpt_rescan(ccb);
	mtx_unlock(&ch->state_mtx);
}

int
ata_reinit(device_t dev)
{
    struct ata_channel *ch = device_get_softc(dev);
    struct ata_request *request;

	xpt_freeze_simq(ch->sim, 1);
	if ((request = ch->running)) {
		ch->running = NULL;
		if (ch->state == ATA_ACTIVE)
		    ch->state = ATA_IDLE;
		callout_stop(&request->callout);
		if (ch->dma.unload)
		    ch->dma.unload(request);
		request->result = ERESTART;
		ata_cam_end_transaction(dev, request);
	}
	/* reset the controller HW, the channel and device(s) */
	ATA_RESET(dev);
	/* Tell the XPT about the event */
	xpt_async(AC_BUS_RESET, ch->path, NULL);
	xpt_release_simq(ch->sim, TRUE);
	return(0);
}

int
ata_suspend(device_t dev)
{
    struct ata_channel *ch;

    /* check for valid device */
    if (!dev || !(ch = device_get_softc(dev)))
	return ENXIO;

    if (ch->flags & ATA_PERIODIC_POLL)
	callout_drain(&ch->poll_callout);
    mtx_lock(&ch->state_mtx);
    xpt_freeze_simq(ch->sim, 1);
    while (ch->state != ATA_IDLE)
	msleep(ch, &ch->state_mtx, PRIBIO, "atasusp", hz/100);
    mtx_unlock(&ch->state_mtx);
    return(0);
}

int
ata_resume(device_t dev)
{
    struct ata_channel *ch;
    int error;

    /* check for valid device */
    if (!dev || !(ch = device_get_softc(dev)))
	return ENXIO;

	mtx_lock(&ch->state_mtx);
	error = ata_reinit(dev);
	xpt_release_simq(ch->sim, TRUE);
	mtx_unlock(&ch->state_mtx);
	if (ch->flags & ATA_PERIODIC_POLL)
		callout_reset(&ch->poll_callout, hz, ata_periodic_poll, ch);
    return error;
}

void
ata_interrupt(void *data)
{
    struct ata_channel *ch = (struct ata_channel *)data;

    mtx_lock(&ch->state_mtx);
    ata_interrupt_locked(data);
    mtx_unlock(&ch->state_mtx);
}

static void
ata_interrupt_locked(void *data)
{
	struct ata_channel *ch = (struct ata_channel *)data;
	struct ata_request *request;

	/* ignore interrupt if its not for us */
	if (ch->hw.status && !ch->hw.status(ch->dev))
		return;

	/* do we have a running request */
	if (!(request = ch->running))
		return;

	ATA_DEBUG_RQ(request, "interrupt");

	/* safetycheck for the right state */
	if (ch->state == ATA_IDLE) {
		device_printf(request->dev, "interrupt on idle channel ignored\n");
		return;
	}

	/*
	 * we have the HW locks, so end the transaction for this request
	 * if it finishes immediately otherwise wait for next interrupt
	 */
	if (ch->hw.end_transaction(request) == ATA_OP_FINISHED) {
		ch->running = NULL;
		if (ch->state == ATA_ACTIVE)
			ch->state = ATA_IDLE;
		ata_cam_end_transaction(ch->dev, request);
		return;
	}
}

static void
ata_periodic_poll(void *data)
{
    struct ata_channel *ch = (struct ata_channel *)data;

    callout_reset(&ch->poll_callout, hz, ata_periodic_poll, ch);
    ata_interrupt(ch);
}

void
ata_print_cable(device_t dev, u_int8_t *who)
{
    device_printf(dev,
                  "DMA limited to UDMA33, %s found non-ATA66 cable\n", who);
}

/*
 * misc support functions
 */
void
ata_default_registers(device_t dev)
{
    struct ata_channel *ch = device_get_softc(dev);

    /* fill in the defaults from whats setup already */
    ch->r_io[ATA_ERROR].res = ch->r_io[ATA_FEATURE].res;
    ch->r_io[ATA_ERROR].offset = ch->r_io[ATA_FEATURE].offset;
    ch->r_io[ATA_IREASON].res = ch->r_io[ATA_COUNT].res;
    ch->r_io[ATA_IREASON].offset = ch->r_io[ATA_COUNT].offset;
    ch->r_io[ATA_STATUS].res = ch->r_io[ATA_COMMAND].res;
    ch->r_io[ATA_STATUS].offset = ch->r_io[ATA_COMMAND].offset;
    ch->r_io[ATA_ALTSTAT].res = ch->r_io[ATA_CONTROL].res;
    ch->r_io[ATA_ALTSTAT].offset = ch->r_io[ATA_CONTROL].offset;
}

void
ata_udelay(int interval)
{
    /* for now just use DELAY, the timer/sleep subsystems are not there yet */
    if (1 || interval < (1000000/hz) || ata_delayed_attach)
	DELAY(interval);
    else
	pause("ataslp", interval/(1000000/hz));
}

const char *
ata_cmd2str(struct ata_request *request)
{
	static char buffer[20];

	if (request->flags & ATA_R_ATAPI) {
		switch (request->u.atapi.sense.key ?
		    request->u.atapi.saved_cmd : request->u.atapi.ccb[0]) {
		case 0x00: return ("TEST_UNIT_READY");
		case 0x01: return ("REZERO");
		case 0x03: return ("REQUEST_SENSE");
		case 0x04: return ("FORMAT");
		case 0x08: return ("READ");
		case 0x0a: return ("WRITE");
		case 0x10: return ("WEOF");
		case 0x11: return ("SPACE");
		case 0x12: return ("INQUIRY");
		case 0x15: return ("MODE_SELECT");
		case 0x19: return ("ERASE");
		case 0x1a: return ("MODE_SENSE");
		case 0x1b: return ("START_STOP");
		case 0x1e: return ("PREVENT_ALLOW");
		case 0x23: return ("ATAPI_READ_FORMAT_CAPACITIES");
		case 0x25: return ("READ_CAPACITY");
		case 0x28: return ("READ_BIG");
		case 0x2a: return ("WRITE_BIG");
		case 0x2b: return ("LOCATE");
		case 0x34: return ("READ_POSITION");
		case 0x35: return ("SYNCHRONIZE_CACHE");
		case 0x3b: return ("WRITE_BUFFER");
		case 0x3c: return ("READ_BUFFER");
		case 0x42: return ("READ_SUBCHANNEL");
		case 0x43: return ("READ_TOC");
		case 0x45: return ("PLAY_10");
		case 0x47: return ("PLAY_MSF");
		case 0x48: return ("PLAY_TRACK");
		case 0x4b: return ("PAUSE");
		case 0x51: return ("READ_DISK_INFO");
		case 0x52: return ("READ_TRACK_INFO");
		case 0x53: return ("RESERVE_TRACK");
		case 0x54: return ("SEND_OPC_INFO");
		case 0x55: return ("MODE_SELECT_BIG");
		case 0x58: return ("REPAIR_TRACK");
		case 0x59: return ("READ_MASTER_CUE");
		case 0x5a: return ("MODE_SENSE_BIG");
		case 0x5b: return ("CLOSE_TRACK/SESSION");
		case 0x5c: return ("READ_BUFFER_CAPACITY");
		case 0x5d: return ("SEND_CUE_SHEET");
		case 0x96: return ("SERVICE_ACTION_IN");
		case 0xa1: return ("BLANK_CMD");
		case 0xa3: return ("SEND_KEY");
		case 0xa4: return ("REPORT_KEY");
		case 0xa5: return ("PLAY_12");
		case 0xa6: return ("LOAD_UNLOAD");
		case 0xad: return ("READ_DVD_STRUCTURE");
		case 0xb4: return ("PLAY_CD");
		case 0xbb: return ("SET_SPEED");
		case 0xbd: return ("MECH_STATUS");
		case 0xbe: return ("READ_CD");
		case 0xff: return ("POLL_DSC");
		}
	} else {
		switch (request->u.ata.command) {
		case 0x00:
			switch (request->u.ata.feature) {
			case 0x00: return ("NOP FLUSHQUEUE");
			case 0x01: return ("NOP AUTOPOLL");
			}
			return ("NOP");
		case 0x03: return ("CFA_REQUEST_EXTENDED_ERROR");
		case 0x06:
			switch (request->u.ata.feature) {
			case 0x01: return ("DSM TRIM");
			}
			return "DSM";
		case 0x08: return ("DEVICE_RESET");
		case 0x20: return ("READ");
		case 0x24: return ("READ48");
		case 0x25: return ("READ_DMA48");
		case 0x26: return ("READ_DMA_QUEUED48");
		case 0x27: return ("READ_NATIVE_MAX_ADDRESS48");
		case 0x29: return ("READ_MUL48");
		case 0x2a: return ("READ_STREAM_DMA48");
		case 0x2b: return ("READ_STREAM48");
		case 0x2f: return ("READ_LOG_EXT");
		case 0x30: return ("WRITE");
		case 0x34: return ("WRITE48");
		case 0x35: return ("WRITE_DMA48");
		case 0x36: return ("WRITE_DMA_QUEUED48");
		case 0x37: return ("SET_MAX_ADDRESS48");
		case 0x39: return ("WRITE_MUL48");
		case 0x3a: return ("WRITE_STREAM_DMA48");
		case 0x3b: return ("WRITE_STREAM48");
		case 0x3d: return ("WRITE_DMA_FUA48");
		case 0x3e: return ("WRITE_DMA_QUEUED_FUA48");
		case 0x3f: return ("WRITE_LOG_EXT");
		case 0x40: return ("READ_VERIFY");
		case 0x42: return ("READ_VERIFY48");
		case 0x45:
			switch (request->u.ata.feature) {
			case 0x55: return ("WRITE_UNCORRECTABLE48 PSEUDO");
			case 0xaa: return ("WRITE_UNCORRECTABLE48 FLAGGED");
			}
			return "WRITE_UNCORRECTABLE48";
		case 0x51: return ("CONFIGURE_STREAM");
		case 0x60: return ("READ_FPDMA_QUEUED");
		case 0x61: return ("WRITE_FPDMA_QUEUED");
		case 0x63: return ("NCQ_NON_DATA");
		case 0x64: return ("SEND_FPDMA_QUEUED");
		case 0x65: return ("RECEIVE_FPDMA_QUEUED");
		case 0x67:
			if (request->u.ata.feature == 0xec)
				return ("SEP_ATTN IDENTIFY");
			switch (request->u.ata.lba) {
			case 0x00: return ("SEP_ATTN READ BUFFER");
			case 0x02: return ("SEP_ATTN RECEIVE DIAGNOSTIC RESULTS");
			case 0x80: return ("SEP_ATTN WRITE BUFFER");
			case 0x82: return ("SEP_ATTN SEND DIAGNOSTIC");
			}
			return ("SEP_ATTN");
		case 0x70: return ("SEEK");
		case 0x87: return ("CFA_TRANSLATE_SECTOR");
		case 0x90: return ("EXECUTE_DEVICE_DIAGNOSTIC");
		case 0x92: return ("DOWNLOAD_MICROCODE");
		case 0xa0: return ("PACKET");
		case 0xa1: return ("ATAPI_IDENTIFY");
		case 0xa2: return ("SERVICE");
		case 0xb0:
			switch(request->u.ata.feature) {
			case 0xd0: return ("SMART READ ATTR VALUES");
			case 0xd1: return ("SMART READ ATTR THRESHOLDS");
			case 0xd3: return ("SMART SAVE ATTR VALUES");
			case 0xd4: return ("SMART EXECUTE OFFLINE IMMEDIATE");
			case 0xd5: return ("SMART READ LOG DATA");
			case 0xd8: return ("SMART ENABLE OPERATION");
			case 0xd9: return ("SMART DISABLE OPERATION");
			case 0xda: return ("SMART RETURN STATUS");
			}
			return ("SMART");
		case 0xb1: return ("DEVICE CONFIGURATION");
		case 0xc0: return ("CFA_ERASE");
		case 0xc4: return ("READ_MUL");
		case 0xc5: return ("WRITE_MUL");
		case 0xc6: return ("SET_MULTI");
		case 0xc7: return ("READ_DMA_QUEUED");
		case 0xc8: return ("READ_DMA");
		case 0xca: return ("WRITE_DMA");
		case 0xcc: return ("WRITE_DMA_QUEUED");
		case 0xcd: return ("CFA_WRITE_MULTIPLE_WITHOUT_ERASE");
		case 0xce: return ("WRITE_MUL_FUA48");
		case 0xd1: return ("CHECK_MEDIA_CARD_TYPE");
		case 0xda: return ("GET_MEDIA_STATUS");
		case 0xde: return ("MEDIA_LOCK");
		case 0xdf: return ("MEDIA_UNLOCK");
		case 0xe0: return ("STANDBY_IMMEDIATE");
		case 0xe1: return ("IDLE_IMMEDIATE");
		case 0xe2: return ("STANDBY");
		case 0xe3: return ("IDLE");
		case 0xe4: return ("READ_BUFFER/PM");
		case 0xe5: return ("CHECK_POWER_MODE");
		case 0xe6: return ("SLEEP");
		case 0xe7: return ("FLUSHCACHE");
		case 0xe8: return ("WRITE_PM");
		case 0xea: return ("FLUSHCACHE48");
		case 0xec: return ("ATA_IDENTIFY");
		case 0xed: return ("MEDIA_EJECT");
		case 0xef:
			switch (request->u.ata.feature) {
			case 0x03: return ("SETFEATURES SET TRANSFER MODE");
			case 0x02: return ("SETFEATURES ENABLE WCACHE");
			case 0x82: return ("SETFEATURES DISABLE WCACHE");
			case 0x06: return ("SETFEATURES ENABLE PUIS");
			case 0x86: return ("SETFEATURES DISABLE PUIS");
			case 0x07: return ("SETFEATURES SPIN-UP");
			case 0x10: return ("SETFEATURES ENABLE SATA FEATURE");
			case 0x90: return ("SETFEATURES DISABLE SATA FEATURE");
			case 0xaa: return ("SETFEATURES ENABLE RCACHE");
			case 0x55: return ("SETFEATURES DISABLE RCACHE");
			case 0x5d: return ("SETFEATURES ENABLE RELIRQ");
			case 0xdd: return ("SETFEATURES DISABLE RELIRQ");
			case 0x5e: return ("SETFEATURES ENABLE SRVIRQ");
			case 0xde: return ("SETFEATURES DISABLE SRVIRQ");
			}
			return "SETFEATURES";
		case 0xf1: return ("SECURITY_SET_PASSWORD");
		case 0xf2: return ("SECURITY_UNLOCK");
		case 0xf3: return ("SECURITY_ERASE_PREPARE");
		case 0xf4: return ("SECURITY_ERASE_UNIT");
		case 0xf5: return ("SECURITY_FREEZE_LOCK");
		case 0xf6: return ("SECURITY_DISABLE_PASSWORD");
		case 0xf8: return ("READ_NATIVE_MAX_ADDRESS");
		case 0xf9: return ("SET_MAX_ADDRESS");
		}
	}
	sprintf(buffer, "unknown CMD (0x%02x)", request->u.ata.command);
	return (buffer);
}

const char *
ata_mode2str(int mode)
{
    switch (mode) {
    case -1: return "UNSUPPORTED";
    case ATA_PIO0: return "PIO0";
    case ATA_PIO1: return "PIO1";
    case ATA_PIO2: return "PIO2";
    case ATA_PIO3: return "PIO3";
    case ATA_PIO4: return "PIO4";
    case ATA_WDMA0: return "WDMA0";
    case ATA_WDMA1: return "WDMA1";
    case ATA_WDMA2: return "WDMA2";
    case ATA_UDMA0: return "UDMA16";
    case ATA_UDMA1: return "UDMA25";
    case ATA_UDMA2: return "UDMA33";
    case ATA_UDMA3: return "UDMA40";
    case ATA_UDMA4: return "UDMA66";
    case ATA_UDMA5: return "UDMA100";
    case ATA_UDMA6: return "UDMA133";
    case ATA_SA150: return "SATA150";
    case ATA_SA300: return "SATA300";
    case ATA_SA600: return "SATA600";
    default:
	if (mode & ATA_DMA_MASK)
	    return "BIOSDMA";
	else
	    return "BIOSPIO";
    }
}

static int
ata_str2mode(const char *str)
{

	if (!strcasecmp(str, "PIO0")) return (ATA_PIO0);
	if (!strcasecmp(str, "PIO1")) return (ATA_PIO1);
	if (!strcasecmp(str, "PIO2")) return (ATA_PIO2);
	if (!strcasecmp(str, "PIO3")) return (ATA_PIO3);
	if (!strcasecmp(str, "PIO4")) return (ATA_PIO4);
	if (!strcasecmp(str, "WDMA0")) return (ATA_WDMA0);
	if (!strcasecmp(str, "WDMA1")) return (ATA_WDMA1);
	if (!strcasecmp(str, "WDMA2")) return (ATA_WDMA2);
	if (!strcasecmp(str, "UDMA0")) return (ATA_UDMA0);
	if (!strcasecmp(str, "UDMA16")) return (ATA_UDMA0);
	if (!strcasecmp(str, "UDMA1")) return (ATA_UDMA1);
	if (!strcasecmp(str, "UDMA25")) return (ATA_UDMA1);
	if (!strcasecmp(str, "UDMA2")) return (ATA_UDMA2);
	if (!strcasecmp(str, "UDMA33")) return (ATA_UDMA2);
	if (!strcasecmp(str, "UDMA3")) return (ATA_UDMA3);
	if (!strcasecmp(str, "UDMA44")) return (ATA_UDMA3);
	if (!strcasecmp(str, "UDMA4")) return (ATA_UDMA4);
	if (!strcasecmp(str, "UDMA66")) return (ATA_UDMA4);
	if (!strcasecmp(str, "UDMA5")) return (ATA_UDMA5);
	if (!strcasecmp(str, "UDMA100")) return (ATA_UDMA5);
	if (!strcasecmp(str, "UDMA6")) return (ATA_UDMA6);
	if (!strcasecmp(str, "UDMA133")) return (ATA_UDMA6);
	return (-1);
}

int
ata_atapi(device_t dev, int target)
{
    struct ata_channel *ch = device_get_softc(dev);

    return (ch->devices & (ATA_ATAPI_MASTER << target));
}

void
ata_timeout(struct ata_request *request)
{
	struct ata_channel *ch;

	ch = device_get_softc(request->parent);
	//request->flags |= ATA_R_DEBUG;
	ATA_DEBUG_RQ(request, "timeout");

	/*
	 * If we have an ATA_ACTIVE request running, we flag the request
	 * ATA_R_TIMEOUT so ata_cam_end_transaction() will handle it correctly.
	 * Also, NULL out the running request so we wont loose the race with
	 * an eventual interrupt arriving late.
	 */
	if (ch->state == ATA_ACTIVE) {
		request->flags |= ATA_R_TIMEOUT;
		if (ch->dma.unload)
			ch->dma.unload(request);
		ch->running = NULL;
		ch->state = ATA_IDLE;
		ata_cam_end_transaction(ch->dev, request);
	}
	mtx_unlock(&ch->state_mtx);
}

static void
ata_cam_begin_transaction(device_t dev, union ccb *ccb)
{
	struct ata_channel *ch = device_get_softc(dev);
	struct ata_request *request;

	request = &ch->request;
	bzero(request, sizeof(*request));

	/* setup request */
	request->dev = NULL;
	request->parent = dev;
	request->unit = ccb->ccb_h.target_id;
	if (ccb->ccb_h.func_code == XPT_ATA_IO) {
		request->data = ccb->ataio.data_ptr;
		request->bytecount = ccb->ataio.dxfer_len;
		request->u.ata.command = ccb->ataio.cmd.command;
		request->u.ata.feature = ((uint16_t)ccb->ataio.cmd.features_exp << 8) |
					  (uint16_t)ccb->ataio.cmd.features;
		request->u.ata.count = ((uint16_t)ccb->ataio.cmd.sector_count_exp << 8) |
					(uint16_t)ccb->ataio.cmd.sector_count;
		if (ccb->ataio.cmd.flags & CAM_ATAIO_48BIT) {
			request->flags |= ATA_R_48BIT;
			request->u.ata.lba =
				     ((uint64_t)ccb->ataio.cmd.lba_high_exp << 40) |
				     ((uint64_t)ccb->ataio.cmd.lba_mid_exp << 32) |
				     ((uint64_t)ccb->ataio.cmd.lba_low_exp << 24);
		} else {
			request->u.ata.lba =
				     ((uint64_t)(ccb->ataio.cmd.device & 0x0f) << 24);
		}
		request->u.ata.lba |= ((uint64_t)ccb->ataio.cmd.lba_high << 16) |
				      ((uint64_t)ccb->ataio.cmd.lba_mid << 8) |
				       (uint64_t)ccb->ataio.cmd.lba_low;
		if (ccb->ataio.cmd.flags & CAM_ATAIO_NEEDRESULT)
			request->flags |= ATA_R_NEEDRESULT;
		if ((ccb->ccb_h.flags & CAM_DIR_MASK) != CAM_DIR_NONE &&
		    ccb->ataio.cmd.flags & CAM_ATAIO_DMA)
			request->flags |= ATA_R_DMA;
		if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_IN)
			request->flags |= ATA_R_READ;
		if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_OUT)
			request->flags |= ATA_R_WRITE;
		if (ccb->ataio.cmd.command == ATA_READ_MUL ||
		    ccb->ataio.cmd.command == ATA_READ_MUL48 ||
		    ccb->ataio.cmd.command == ATA_WRITE_MUL ||
		    ccb->ataio.cmd.command == ATA_WRITE_MUL48) {
			request->transfersize = min(request->bytecount,
			    ch->curr[ccb->ccb_h.target_id].bytecount);
		} else
			request->transfersize = min(request->bytecount, 512);
	} else {
		request->data = ccb->csio.data_ptr;
		request->bytecount = ccb->csio.dxfer_len;
		bcopy((ccb->ccb_h.flags & CAM_CDB_POINTER) ?
		    ccb->csio.cdb_io.cdb_ptr : ccb->csio.cdb_io.cdb_bytes,
		    request->u.atapi.ccb, ccb->csio.cdb_len);
		request->flags |= ATA_R_ATAPI;
		if (ch->curr[ccb->ccb_h.target_id].atapi == 16)
			request->flags |= ATA_R_ATAPI16;
		if ((ccb->ccb_h.flags & CAM_DIR_MASK) != CAM_DIR_NONE &&
		    ch->curr[ccb->ccb_h.target_id].mode >= ATA_DMA)
			request->flags |= ATA_R_DMA;
		if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_IN)
			request->flags |= ATA_R_READ;
		if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_OUT)
			request->flags |= ATA_R_WRITE;
		request->transfersize = min(request->bytecount,
		    ch->curr[ccb->ccb_h.target_id].bytecount);
	}
	request->retries = 0;
	request->timeout = (ccb->ccb_h.timeout + 999) / 1000;
	callout_init_mtx(&request->callout, &ch->state_mtx, CALLOUT_RETURNUNLOCKED);
	request->ccb = ccb;
	request->flags |= ATA_R_DATA_IN_CCB;

	ch->running = request;
	ch->state = ATA_ACTIVE;
	if (ch->hw.begin_transaction(request) == ATA_OP_FINISHED) {
	    ch->running = NULL;
	    ch->state = ATA_IDLE;
	    ata_cam_end_transaction(dev, request);
	    return;
	}
}

static void
ata_cam_request_sense(device_t dev, struct ata_request *request)
{
	struct ata_channel *ch = device_get_softc(dev);
	union ccb *ccb = request->ccb;

	ch->requestsense = 1;

	bzero(request, sizeof(*request));
	request->dev = NULL;
	request->parent = dev;
	request->unit = ccb->ccb_h.target_id;
	request->data = (void *)&ccb->csio.sense_data;
	request->bytecount = ccb->csio.sense_len;
	request->u.atapi.ccb[0] = ATAPI_REQUEST_SENSE;
	request->u.atapi.ccb[4] = ccb->csio.sense_len;
	request->flags |= ATA_R_ATAPI;
	if (ch->curr[ccb->ccb_h.target_id].atapi == 16)
		request->flags |= ATA_R_ATAPI16;
	if (ch->curr[ccb->ccb_h.target_id].mode >= ATA_DMA)
		request->flags |= ATA_R_DMA;
	request->flags |= ATA_R_READ;
	request->transfersize = min(request->bytecount,
	    ch->curr[ccb->ccb_h.target_id].bytecount);
	request->retries = 0;
	request->timeout = (ccb->ccb_h.timeout + 999) / 1000;
	callout_init_mtx(&request->callout, &ch->state_mtx, CALLOUT_RETURNUNLOCKED);
	request->ccb = ccb;

	ch->running = request;
	ch->state = ATA_ACTIVE;
	if (ch->hw.begin_transaction(request) == ATA_OP_FINISHED) {
		ch->running = NULL;
		ch->state = ATA_IDLE;
		ata_cam_end_transaction(dev, request);
		return;
	}
}

static void
ata_cam_process_sense(device_t dev, struct ata_request *request)
{
	struct ata_channel *ch = device_get_softc(dev);
	union ccb *ccb = request->ccb;
	int fatalerr = 0;

	ch->requestsense = 0;

	if (request->flags & ATA_R_TIMEOUT)
		fatalerr = 1;
	if ((request->flags & ATA_R_TIMEOUT) == 0 &&
	    (request->status & ATA_S_ERROR) == 0 &&
	    request->result == 0) {
		ccb->ccb_h.status |= CAM_AUTOSNS_VALID;
	} else {
		ccb->ccb_h.status &= ~CAM_STATUS_MASK;
		ccb->ccb_h.status |= CAM_AUTOSENSE_FAIL;
	}

	xpt_done(ccb);
	/* Do error recovery if needed. */
	if (fatalerr)
		ata_reinit(dev);
}

static void
ata_cam_end_transaction(device_t dev, struct ata_request *request)
{
	struct ata_channel *ch = device_get_softc(dev);
	union ccb *ccb = request->ccb;
	int fatalerr = 0;

	if (ch->requestsense) {
		ata_cam_process_sense(dev, request);
		return;
	}

	ccb->ccb_h.status &= ~CAM_STATUS_MASK;
	if (request->flags & ATA_R_TIMEOUT) {
		xpt_freeze_simq(ch->sim, 1);
		ccb->ccb_h.status &= ~CAM_STATUS_MASK;
		ccb->ccb_h.status |= CAM_CMD_TIMEOUT | CAM_RELEASE_SIMQ;
		fatalerr = 1;
	} else if (request->status & ATA_S_ERROR) {
		if (ccb->ccb_h.func_code == XPT_ATA_IO) {
			ccb->ccb_h.status |= CAM_ATA_STATUS_ERROR;
		} else {
			ccb->ccb_h.status |= CAM_SCSI_STATUS_ERROR;
			ccb->csio.scsi_status = SCSI_STATUS_CHECK_COND;
		}
	} else if (request->result == ERESTART)
		ccb->ccb_h.status |= CAM_REQUEUE_REQ;
	else if (request->result != 0)
		ccb->ccb_h.status |= CAM_REQ_CMP_ERR;
	else
		ccb->ccb_h.status |= CAM_REQ_CMP;
	if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP &&
	    !(ccb->ccb_h.status & CAM_DEV_QFRZN)) {
		xpt_freeze_devq(ccb->ccb_h.path, 1);
		ccb->ccb_h.status |= CAM_DEV_QFRZN;
	}
	if (ccb->ccb_h.func_code == XPT_ATA_IO &&
	    ((request->status & ATA_S_ERROR) ||
	    (ccb->ataio.cmd.flags & CAM_ATAIO_NEEDRESULT))) {
		struct ata_res *res = &ccb->ataio.res;
		res->status = request->status;
		res->error = request->error;
		res->lba_low = request->u.ata.lba;
		res->lba_mid = request->u.ata.lba >> 8;
		res->lba_high = request->u.ata.lba >> 16;
		res->device = request->u.ata.lba >> 24;
		res->lba_low_exp = request->u.ata.lba >> 24;
		res->lba_mid_exp = request->u.ata.lba >> 32;
		res->lba_high_exp = request->u.ata.lba >> 40;
		res->sector_count = request->u.ata.count;
		res->sector_count_exp = request->u.ata.count >> 8;
	}
	if ((ccb->ccb_h.flags & CAM_DIR_MASK) != CAM_DIR_NONE) {
		if (ccb->ccb_h.func_code == XPT_ATA_IO) {
			ccb->ataio.resid =
			    ccb->ataio.dxfer_len - request->donecount;
		} else {
			ccb->csio.resid =
			    ccb->csio.dxfer_len - request->donecount;
		}
	}
	if ((ccb->ccb_h.status & CAM_STATUS_MASK) == CAM_SCSI_STATUS_ERROR &&
	    (ccb->ccb_h.flags & CAM_DIS_AUTOSENSE) == 0)
		ata_cam_request_sense(dev, request);
	else
		xpt_done(ccb);
	/* Do error recovery if needed. */
	if (fatalerr)
		ata_reinit(dev);
}

static int
ata_check_ids(device_t dev, union ccb *ccb)
{
	struct ata_channel *ch = device_get_softc(dev);

	if (ccb->ccb_h.target_id > ((ch->flags & ATA_NO_SLAVE) ? 0 : 1)) {
		ccb->ccb_h.status = CAM_TID_INVALID;
		xpt_done(ccb);
		return (-1);
	}
	if (ccb->ccb_h.target_lun != 0) {
		ccb->ccb_h.status = CAM_LUN_INVALID;
		xpt_done(ccb);
		return (-1);
	}
	/*
	 * It's a programming error to see AUXILIARY register requests.
	 */
	KASSERT(ccb->ccb_h.func_code != XPT_ATA_IO ||
	    ((ccb->ataio.ata_flags & ATA_FLAG_AUX) == 0),
	    ("AUX register unsupported"));
	return (0);
}

static void
ataaction(struct cam_sim *sim, union ccb *ccb)
{
	device_t dev, parent;
	struct ata_channel *ch;

	CAM_DEBUG(ccb->ccb_h.path, CAM_DEBUG_TRACE, ("ataaction func_code=%x\n",
	    ccb->ccb_h.func_code));

	ch = (struct ata_channel *)cam_sim_softc(sim);
	dev = ch->dev;
	switch (ccb->ccb_h.func_code) {
	/* Common cases first */
	case XPT_ATA_IO:	/* Execute the requested I/O operation */
	case XPT_SCSI_IO:
		if (ata_check_ids(dev, ccb))
			return;
		if ((ch->devices & ((ATA_ATA_MASTER | ATA_ATAPI_MASTER)
		    << ccb->ccb_h.target_id)) == 0) {
			ccb->ccb_h.status = CAM_SEL_TIMEOUT;
			break;
		}
		if (ch->running)
			device_printf(dev, "already running!\n");
		if (ccb->ccb_h.func_code == XPT_ATA_IO &&
		    (ccb->ataio.cmd.flags & CAM_ATAIO_CONTROL) &&
		    (ccb->ataio.cmd.control & ATA_A_RESET)) {
			struct ata_res *res = &ccb->ataio.res;
			
			bzero(res, sizeof(*res));
			if (ch->devices & (ATA_ATA_MASTER << ccb->ccb_h.target_id)) {
				res->lba_high = 0;
				res->lba_mid = 0;
			} else {
				res->lba_high = 0xeb;
				res->lba_mid = 0x14;
			}
			ccb->ccb_h.status = CAM_REQ_CMP;
			break;
		}
		ata_cam_begin_transaction(dev, ccb);
		return;
	case XPT_ABORT:			/* Abort the specified CCB */
		/* XXX Implement */
		ccb->ccb_h.status = CAM_REQ_INVALID;
		break;
	case XPT_SET_TRAN_SETTINGS:
	{
		struct	ccb_trans_settings *cts = &ccb->cts;
		struct	ata_cam_device *d; 

		if (ata_check_ids(dev, ccb))
			return;
		if (cts->type == CTS_TYPE_CURRENT_SETTINGS)
			d = &ch->curr[ccb->ccb_h.target_id];
		else
			d = &ch->user[ccb->ccb_h.target_id];
		if (ch->flags & ATA_SATA) {
			if (cts->xport_specific.sata.valid & CTS_SATA_VALID_REVISION)
				d->revision = cts->xport_specific.sata.revision;
			if (cts->xport_specific.sata.valid & CTS_SATA_VALID_MODE) {
				if (cts->type == CTS_TYPE_CURRENT_SETTINGS) {
					d->mode = ATA_SETMODE(ch->dev,
					    ccb->ccb_h.target_id,
					    cts->xport_specific.sata.mode);
				} else
					d->mode = cts->xport_specific.sata.mode;
			}
			if (cts->xport_specific.sata.valid & CTS_SATA_VALID_BYTECOUNT)
				d->bytecount = min(8192, cts->xport_specific.sata.bytecount);
			if (cts->xport_specific.sata.valid & CTS_SATA_VALID_ATAPI)
				d->atapi = cts->xport_specific.sata.atapi;
			if (cts->xport_specific.sata.valid & CTS_SATA_VALID_CAPS)
				d->caps = cts->xport_specific.sata.caps;
		} else {
			if (cts->xport_specific.ata.valid & CTS_ATA_VALID_MODE) {
				if (cts->type == CTS_TYPE_CURRENT_SETTINGS) {
					d->mode = ATA_SETMODE(ch->dev,
					    ccb->ccb_h.target_id,
					    cts->xport_specific.ata.mode);
				} else
					d->mode = cts->xport_specific.ata.mode;
			}
			if (cts->xport_specific.ata.valid & CTS_ATA_VALID_BYTECOUNT)
				d->bytecount = cts->xport_specific.ata.bytecount;
			if (cts->xport_specific.ata.valid & CTS_ATA_VALID_ATAPI)
				d->atapi = cts->xport_specific.ata.atapi;
			if (cts->xport_specific.ata.valid & CTS_ATA_VALID_CAPS)
				d->caps = cts->xport_specific.ata.caps;
		}
		ccb->ccb_h.status = CAM_REQ_CMP;
		break;
	}
	case XPT_GET_TRAN_SETTINGS:
	{
		struct	ccb_trans_settings *cts = &ccb->cts;
		struct  ata_cam_device *d;

		if (ata_check_ids(dev, ccb))
			return;
		if (cts->type == CTS_TYPE_CURRENT_SETTINGS)
			d = &ch->curr[ccb->ccb_h.target_id];
		else
			d = &ch->user[ccb->ccb_h.target_id];
		cts->protocol = PROTO_UNSPECIFIED;
		cts->protocol_version = PROTO_VERSION_UNSPECIFIED;
		if (ch->flags & ATA_SATA) {
			cts->transport = XPORT_SATA;
			cts->transport_version = XPORT_VERSION_UNSPECIFIED;
			cts->xport_specific.sata.valid = 0;
			cts->xport_specific.sata.mode = d->mode;
			cts->xport_specific.sata.valid |= CTS_SATA_VALID_MODE;
			cts->xport_specific.sata.bytecount = d->bytecount;
			cts->xport_specific.sata.valid |= CTS_SATA_VALID_BYTECOUNT;
			if (cts->type == CTS_TYPE_CURRENT_SETTINGS) {
				cts->xport_specific.sata.revision =
				    ATA_GETREV(dev, ccb->ccb_h.target_id);
				if (cts->xport_specific.sata.revision != 0xff) {
					cts->xport_specific.sata.valid |=
					    CTS_SATA_VALID_REVISION;
				}
				cts->xport_specific.sata.caps =
				    d->caps & CTS_SATA_CAPS_D;
				if (ch->pm_level) {
					cts->xport_specific.sata.caps |=
					    CTS_SATA_CAPS_H_PMREQ;
				}
				cts->xport_specific.sata.caps &=
				    ch->user[ccb->ccb_h.target_id].caps;
			} else {
				cts->xport_specific.sata.revision = d->revision;
				cts->xport_specific.sata.valid |= CTS_SATA_VALID_REVISION;
				cts->xport_specific.sata.caps = d->caps;
			}
			cts->xport_specific.sata.valid |= CTS_SATA_VALID_CAPS;
			cts->xport_specific.sata.atapi = d->atapi;
			cts->xport_specific.sata.valid |= CTS_SATA_VALID_ATAPI;
		} else {
			cts->transport = XPORT_ATA;
			cts->transport_version = XPORT_VERSION_UNSPECIFIED;
			cts->xport_specific.ata.valid = 0;
			cts->xport_specific.ata.mode = d->mode;
			cts->xport_specific.ata.valid |= CTS_ATA_VALID_MODE;
			cts->xport_specific.ata.bytecount = d->bytecount;
			cts->xport_specific.ata.valid |= CTS_ATA_VALID_BYTECOUNT;
			if (cts->type == CTS_TYPE_CURRENT_SETTINGS) {
				cts->xport_specific.ata.caps =
				    d->caps & CTS_ATA_CAPS_D;
				if (!(ch->flags & ATA_NO_48BIT_DMA))
					cts->xport_specific.ata.caps |=
					    CTS_ATA_CAPS_H_DMA48;
				cts->xport_specific.ata.caps &=
				    ch->user[ccb->ccb_h.target_id].caps;
			} else
				cts->xport_specific.ata.caps = d->caps;
			cts->xport_specific.ata.valid |= CTS_ATA_VALID_CAPS;
			cts->xport_specific.ata.atapi = d->atapi;
			cts->xport_specific.ata.valid |= CTS_ATA_VALID_ATAPI;
		}
		ccb->ccb_h.status = CAM_REQ_CMP;
		break;
	}
	case XPT_RESET_BUS:		/* Reset the specified SCSI bus */
	case XPT_RESET_DEV:	/* Bus Device Reset the specified SCSI device */
		ata_reinit(dev);
		ccb->ccb_h.status = CAM_REQ_CMP;
		break;
	case XPT_TERM_IO:		/* Terminate the I/O process */
		/* XXX Implement */
		ccb->ccb_h.status = CAM_REQ_INVALID;
		break;
	case XPT_PATH_INQ:		/* Path routing inquiry */
	{
		struct ccb_pathinq *cpi = &ccb->cpi;

		parent = device_get_parent(dev);
		cpi->version_num = 1; /* XXX??? */
		cpi->hba_inquiry = PI_SDTR_ABLE;
		cpi->target_sprt = 0;
		cpi->hba_misc = PIM_SEQSCAN | PIM_UNMAPPED;
		cpi->hba_eng_cnt = 0;
		if (ch->flags & ATA_NO_SLAVE)
			cpi->max_target = 0;
		else
			cpi->max_target = 1;
		cpi->max_lun = 0;
		cpi->initiator_id = 0;
		cpi->bus_id = cam_sim_bus(sim);
		if (ch->flags & ATA_SATA)
			cpi->base_transfer_speed = 150000;
		else
			cpi->base_transfer_speed = 3300;
		strlcpy(cpi->sim_vid, "FreeBSD", SIM_IDLEN);
		strlcpy(cpi->hba_vid, "ATA", HBA_IDLEN);
		strlcpy(cpi->dev_name, cam_sim_name(sim), DEV_IDLEN);
		cpi->unit_number = cam_sim_unit(sim);
		if (ch->flags & ATA_SATA)
			cpi->transport = XPORT_SATA;
		else
			cpi->transport = XPORT_ATA;
		cpi->transport_version = XPORT_VERSION_UNSPECIFIED;
		cpi->protocol = PROTO_ATA;
		cpi->protocol_version = PROTO_VERSION_UNSPECIFIED;
		cpi->maxio = ch->dma.max_iosize ? ch->dma.max_iosize : DFLTPHYS;
		if (device_get_devclass(device_get_parent(parent)) ==
		    devclass_find("pci")) {
			cpi->hba_vendor = pci_get_vendor(parent);
			cpi->hba_device = pci_get_device(parent);
			cpi->hba_subvendor = pci_get_subvendor(parent);
			cpi->hba_subdevice = pci_get_subdevice(parent);
		}
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
atapoll(struct cam_sim *sim)
{
	struct ata_channel *ch = (struct ata_channel *)cam_sim_softc(sim);

	ata_interrupt_locked(ch);
}

/*
 * module handeling
 */
static int
ata_module_event_handler(module_t mod, int what, void *arg)
{

    switch (what) {
    case MOD_LOAD:
	return 0;

    case MOD_UNLOAD:
	return 0;

    default:
	return EOPNOTSUPP;
    }
}

static moduledata_t ata_moduledata = { "ata", ata_module_event_handler, NULL };
DECLARE_MODULE(ata, ata_moduledata, SI_SUB_CONFIGURE, SI_ORDER_SECOND);
MODULE_VERSION(ata, 1);
MODULE_DEPEND(ata, cam, 1, 1, 1);
